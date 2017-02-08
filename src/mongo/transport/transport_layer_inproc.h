/**
 *    Copyright (C) 2017 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <vector>

#include "mongo/stdx/list.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/mutex.h"
#include "mongo/stdx/thread.h"
#include "mongo/transport/ticket_impl.h"
#include "mongo/transport/transport_layer.h"
#include "mongo/util/net/listen.h"
#include "mongo/util/net/sock.h"
#include "mongo/util/inproc_pipe.h"
#include "mongo/stdx/chrono.h"

namespace mongo {

class ServiceEntryPoint;

namespace transport {

class TransportLayerInProc final : public TransportLayer {
    MONGO_DISALLOW_COPYING(TransportLayerInProc);

public:
    class Connection {
    public:
        Connection(TransportLayerInProc* parent);

        void close();

        bool closed() { return _closed; }

        Status serverSendMsg(const Message& toSend, stdx::chrono::milliseconds timeout);
        Status serverRecvMsg(Message* toRecv, stdx::chrono::milliseconds timeout);

        bool clientSend(const void* buffer, size_t bufferLen, stdx::chrono::milliseconds timeout);
        bool clientRecv(void* buffer, size_t bufferLen, stdx::chrono::milliseconds timeout);

        bool clientCanRead();
        bool clientCanWrite();

    private:
        TransportLayerInProc* _parent;

        bool _closed = false;

        InprocPipe _clientToServerPipe;
        InprocPipe _serverToClientPipe;
    };

    struct Options {
    };

    TransportLayerInProc(const Options& opts, ServiceEntryPoint* sep);

    ~TransportLayerInProc();

    Status setup();
    Status start() override;

    Ticket sourceMessage(const SessionHandle& session,
                         Message* message,
                         Date_t expiration = Ticket::kNoExpirationDate) override;

    Ticket sinkMessage(const SessionHandle& session,
                       const Message& message,
                       Date_t expiration = Ticket::kNoExpirationDate) override;

    Status wait(Ticket&& ticket) override;
    void asyncWait(Ticket&& ticket, TicketCallback callback) override;

    Stats sessionStats() override;

    void end(const SessionHandle& session) override;
    void endAllSessions(transport::Session::TagMask tags) override;

    void shutdown() override;

    static TransportLayerInProc* getTLByAddr(StringData addr);
    Connection* connectInProcClient();

private:

    class InProcSession;
    using InProcSessionHandle = std::shared_ptr<InProcSession>;
    using ConstInProcSessionHandle = std::shared_ptr<const InProcSession>;
    using SessionEntry = std::list<std::weak_ptr<InProcSession>>::iterator;

    void _destroy(InProcSession& session);

    void _handleNewConnection(std::unique_ptr<Connection> conn);

    Status _runTicket(Ticket ticket);

    using WorkHandle = stdx::function<Status(Connection*)>;

    std::vector<InProcSessionHandle> lockAllSessions(const stdx::unique_lock<stdx::mutex>&) const;

    /**
     * An implementation of the Session interface for this TransportLayer.
     */
    class InProcSession : public Session {
        MONGO_DISALLOW_COPYING(InProcSession);

    public:
        ~InProcSession();

        static InProcSessionHandle create(std::unique_ptr<Connection> conn,
                                          TransportLayerInProc* tl);

        TransportLayer* getTransportLayer() const override {
            return _tl;
        }

        const HostAndPort& remote() const override {
            return _remote;
        }

        const HostAndPort& local() const override {
            return _local;
        }

        Connection* conn() const {
            return _connection.get();
        }

        void setIter(SessionEntry it) {
            _entry = std::move(it);
        }

        SessionEntry getIter() const {
            return _entry;
        }

    private:
        explicit InProcSession(std::unique_ptr<Connection> conn,
                               TransportLayerInProc* tl);

        HostAndPort _remote;
        HostAndPort _local;

        TransportLayerInProc* _tl;

        TagMask _tags;

        std::unique_ptr<Connection> _connection;

        // A handle to this session's entry in the TL's session list
        SessionEntry _entry;
    };

    /**
     * A TicketImpl implementation for this TransportLayer. WorkHandle is a callable that
     * can be invoked to fill this ticket.
     */
    class InProcTicket : public TicketImpl {
        MONGO_DISALLOW_COPYING(InProcTicket);

    public:
        InProcTicket(const InProcSessionHandle& session, Date_t expiration, WorkHandle work);

        SessionId sessionId() const override;
        Date_t expiration() const override;

        /**
         * If this ticket's session is still alive, return a shared_ptr. Otherwise,
         * return nullptr.
         */
        InProcSessionHandle getSession();

        /**
         * Run this ticket's work item.
         */
        Status fill(Connection* conn);

    private:
        std::weak_ptr<InProcSession> _session;

        SessionId _sessionId;
        Date_t _expiration;

        WorkHandle _fill;
    };

    void _closeConnection(Connection* conn);

    ServiceEntryPoint* _sep;

    // TransportLayerInProc holds non-owning pointers to all of its sessions.
    mutable stdx::mutex _sessionsMutex;
    stdx::list<std::weak_ptr<InProcSession>> _sessions;

    AtomicWord<bool> _running;

    Options _options;
};

}  // namespace transport
}  // namespace mongo
