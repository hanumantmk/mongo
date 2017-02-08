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

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kNetwork

#include "mongo/platform/basic.h"

#include <algorithm>
#include <iterator>
#include <memory>

#include "mongo/transport/transport_layer_inproc.h"

#include "mongo/base/checked_cast.h"
#include "mongo/config.h"
#include "mongo/db/service_context.h"
#include "mongo/db/stats/counters.h"
#include "mongo/stdx/functional.h"
#include "mongo/stdx/memory.h"
#include "mongo/transport/message_compressor_manager.h"
#include "mongo/transport/service_entry_point.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/log.h"
#include "mongo/util/net/abstract_message_port.h"
#include "mongo/util/net/socket_exception.h"
#include "mongo/util/net/ssl_types.h"

namespace mongo {
namespace transport {
namespace {
struct lock_weak {
    template <typename T>
    std::shared_ptr<T> operator()(const std::weak_ptr<T>& p) const {
        return p.lock();
    }
};

stdx::mutex globalInProcTLMutex;
stdx::condition_variable globalInProcTLCondvar;
TransportLayerInProc* globalInProcTL = nullptr;

}  // namespace

TransportLayerInProc::TransportLayerInProc(const TransportLayerInProc::Options& opts,
                                           ServiceEntryPoint* sep)
    : _sep(sep),
      _running(false),
      _options(opts) {
}

std::shared_ptr<TransportLayerInProc::InProcSession> TransportLayerInProc::InProcSession::create(
    std::unique_ptr<Connection> conn, TransportLayerInProc* tl) {
    std::shared_ptr<InProcSession> handle(new InProcSession(std::move(conn), tl));
    return handle;
}

TransportLayerInProc::InProcSession::InProcSession(std::unique_ptr<Connection> conn,
                                                   TransportLayerInProc* tl)
    : _remote(),
      _local(),
      _tl(tl),
      _tags(kEmptyTagMask),
      _connection(std::move(conn)) {}

TransportLayerInProc::InProcSession::~InProcSession() {
    _tl->_destroy(*this);
}

TransportLayerInProc::InProcTicket::InProcTicket(const InProcSessionHandle& session,
                                                 Date_t expiration,
                                                 WorkHandle work)
    : _session(session),
      _sessionId(session->id()),
      _expiration(expiration),
      _fill(std::move(work)) {}

TransportLayerInProc::InProcSessionHandle TransportLayerInProc::InProcTicket::getSession() {
    return _session.lock();
}

Session::Id TransportLayerInProc::InProcTicket::sessionId() const {
    return _sessionId;
}

Date_t TransportLayerInProc::InProcTicket::expiration() const {
    return _expiration;
}

Status TransportLayerInProc::InProcTicket::fill(Connection* amp) {
    return _fill(amp);
}

Status TransportLayerInProc::setup() {
    return Status::OK();
}

TransportLayerInProc* TransportLayerInProc::getTLByAddr(StringData addr) {
    stdx::unique_lock<stdx::mutex> lk(globalInProcTLMutex);

    globalInProcTLCondvar.wait(lk, [&]{
        return globalInProcTL && globalInProcTL->_running.load();
    });

    return globalInProcTL;
}

TransportLayerInProc::Connection* TransportLayerInProc::connectInProcClient() {
    auto conn = std::unique_ptr<Connection>(new Connection(this));

    auto rval = conn.get();

    _handleNewConnection(std::move(conn));

    return rval;
}

Status TransportLayerInProc::start() {
    if (_running.swap(true)) {
        return {ErrorCodes::InternalError, "TransportLayer is already running"};
    }

    stdx::lock_guard<stdx::mutex> lk(globalInProcTLMutex);
    invariant(!globalInProcTL);
    globalInProcTL = this;
    globalInProcTLCondvar.notify_all();

    return Status::OK();
}

TransportLayerInProc::~TransportLayerInProc() = default;

Ticket TransportLayerInProc::sourceMessage(const SessionHandle& session,
                                           Message* message,
                                           Date_t expiration) {
    auto& compressorMgr = MessageCompressorManager::forSession(session);
    auto sourceCb = [message, &compressorMgr](Connection* conn) -> Status {
        if (!conn->serverRecvMsg(message, stdx::chrono::seconds(10)).isOK()) {
            return {ErrorCodes::HostUnreachable, "Recv failed"};
        }

        networkCounter.hitPhysical(message->size(), 0);
        if (message->operation() == dbCompressed) {
            auto swm = compressorMgr.decompressMessage(*message);
            if (!swm.isOK())
                return swm.getStatus();
            *message = swm.getValue();
        }
        networkCounter.hitLogical(message->size(), 0);
        return Status::OK();
    };

    auto inprocSession = checked_pointer_cast<InProcSession>(session);
    return Ticket(
        this,
        std::unique_ptr<InProcTicket>(new InProcTicket(std::move(inprocSession), expiration, std::move(sourceCb))));
}

TransportLayer::Stats TransportLayerInProc::sessionStats() {
    Stats stats;
    {
        stdx::lock_guard<stdx::mutex> lk(_sessionsMutex);
        stats.numOpenSessions = _sessions.size();
    }

    stats.numAvailableSessions = Listener::globalTicketHolder.available();
    stats.numCreatedSessions = Listener::globalConnectionNumber.load();

    return stats;
}

Ticket TransportLayerInProc::sinkMessage(const SessionHandle& session,
                                         const Message& message,
                                         Date_t expiration) {
    auto& compressorMgr = MessageCompressorManager::forSession(session);
    auto sinkCb = [&message, &compressorMgr](Connection* conn) -> Status {
        try {
            networkCounter.hitLogical(0, message.size());
            auto swm = compressorMgr.compressMessage(message);
            if (!swm.isOK())
                return swm.getStatus();
            const auto& compressedMessage = swm.getValue();
            conn->serverSendMsg(compressedMessage, stdx::chrono::seconds(5));
            networkCounter.hitPhysical(0, compressedMessage.size());

            return Status::OK();
        } catch (const SocketException& e) {
            return {ErrorCodes::HostUnreachable, e.what()};
        }
    };

    auto inprocSession = checked_pointer_cast<InProcSession>(session);
    return Ticket(
        this,
        std::unique_ptr<InProcTicket>(new InProcTicket(std::move(inprocSession), expiration, std::move(sinkCb))));
}

Status TransportLayerInProc::wait(Ticket&& ticket) {
    return _runTicket(std::move(ticket));
}

void TransportLayerInProc::asyncWait(Ticket&& ticket, TicketCallback callback) {
    // Left unimplemented because there is no reasonable way to offer general async waiting besides
    // offering a background thread that can handle waits for multiple tickets. We may never
    // implement this for the inproc TL.
    MONGO_UNREACHABLE;
}

void TransportLayerInProc::end(const SessionHandle& session) {
    auto inprocSession = checked_pointer_cast<const InProcSession>(session);
    _closeConnection(inprocSession->conn());
}

void TransportLayerInProc::_closeConnection(Connection* conn) {
    conn->close();
    Listener::globalTicketHolder.release();
}

// Capture all of the weak pointers behind the lock, to delay their expiry until we leave the
// locking context. This function requires proof of locking, by passing the lock guard.
auto TransportLayerInProc::lockAllSessions(const stdx::unique_lock<stdx::mutex>&) const
    -> std::vector<InProcSessionHandle> {
    using std::begin;
    using std::end;
    std::vector<std::shared_ptr<InProcSession>> result;
    std::transform(begin(_sessions), end(_sessions), std::back_inserter(result), lock_weak());
    // Skip expired weak pointers.
    result.erase(std::remove(begin(result), end(result), nullptr), end(result));
    return result;
}

void TransportLayerInProc::endAllSessions(Session::TagMask tags) {
    log() << "inproc transport layer closing all connections";
    {
        stdx::unique_lock<stdx::mutex> lk(_sessionsMutex);
        // We want to capture the shared_ptrs to our sessions in a way which lets us destroy them
        // outside of the lock.
        const auto sessions = lockAllSessions(lk);

        for (auto&& session : sessions) {
            if (session->getTags() & tags) {
                log() << "Skip closing connection for connection # ";
            } else {
                _closeConnection(session->conn());
            }
        }
        // TODO(SERVER-27069): Revamp this lock to not cover the loop. This unlock was put here
        // specifically to minimize risk, just before the release of 3.4. The risk is that we would
        // be in the loop without the lock, which most of our testing didn't do. We must unlock
        // manually here, because the `sessions` vector must be destroyed *outside* of the lock.
        lk.unlock();
    }
}

void TransportLayerInProc::shutdown() {
    _running.store(false);
    endAllSessions(Session::kEmptyTagMask);
}

void TransportLayerInProc::_destroy(InProcSession& session) {
    if (!session.conn()->closed()) {
        _closeConnection(session.conn());
    }

    stdx::lock_guard<stdx::mutex> lk(_sessionsMutex);
    _sessions.erase(session.getIter());
}

Status TransportLayerInProc::_runTicket(Ticket ticket) {
    if (!_running.load()) {
        return TransportLayer::ShutdownStatus;
    }

    if (ticket.expiration() < Date_t::now()) {
        return Ticket::ExpiredStatus;
    }

    // get the weak_ptr out of the ticket
    // attempt to make it into a shared_ptr
    auto inprocTicket = checked_cast<InProcTicket*>(getTicketImpl(ticket));
    auto session = inprocTicket->getSession();
    if (!session) {
        return TransportLayer::TicketSessionClosedStatus;
    }

    auto conn = session->conn();
    if (conn->closed()) {
        return TransportLayer::TicketSessionClosedStatus;
    }

    Status res = Status::OK();
    try {
        res = inprocTicket->fill(conn);
    } catch (...) {
        res = exceptionToStatus();
    }

#ifdef MONGO_CONFIG_SSL
#error "we don't support ssl"
#endif

    return res;
}

void TransportLayerInProc::_handleNewConnection(std::unique_ptr<Connection> conn) {
    if (!Listener::globalTicketHolder.tryAcquire()) {
        log() << "connection refused because too many open connections: "
              << Listener::globalTicketHolder.used();
        conn->close();
        return;
    }

//    amp->setLogLevel(logger::LogSeverity::Debug(1));
    auto session = InProcSession::create(std::move(conn), this);

    stdx::list<std::weak_ptr<InProcSession>> list;
    auto it = list.emplace(list.begin(), session);

    {
        // Add the new session to our list
        stdx::lock_guard<stdx::mutex> lk(_sessionsMutex);
        session->setIter(it);
        _sessions.splice(_sessions.begin(), list, it);
    }

    invariant(_sep);
    _sep->startSession(std::move(session));
}

TransportLayerInProc::Connection::Connection(TransportLayerInProc* parent) : _parent(parent), _clientToServerPipe(1 << 27), _serverToClientPipe(1 << 27) {
}

void TransportLayerInProc::Connection::close() {
    _closed = true;

    _clientToServerPipe.close();
    _serverToClientPipe.close();
}

Status TransportLayerInProc::Connection::serverSendMsg(const Message& toSend,
                                                     stdx::chrono::milliseconds timeout) {
    switch (_serverToClientPipe.send(reinterpret_cast<const uint8_t*>(toSend.buf()),
                                     toSend.size(),
                                     timeout)) {
        case InprocPipe::Result::Timeout:{
            return Status(ErrorCodes::NetworkTimeout, "timeout in server response");
        }
        case InprocPipe::Result::Closed: {
            return Status(ErrorCodes::HostUnreachable, "disconnect");
        }
        case InprocPipe::Result::Success: {}
    }

    return Status::OK();
}

Status TransportLayerInProc::Connection::serverRecvMsg(Message* toRecv,
                                                     stdx::chrono::milliseconds timeout) {
    MSGHEADER::Value header;
    switch (
        _clientToServerPipe.recv(reinterpret_cast<uint8_t*>(&header), sizeof(header), timeout)) {
            case InprocPipe::Result::Timeout: {
                return Status(ErrorCodes::NetworkTimeout, "timeout in server response");
            }
            case InprocPipe::Result::Closed: {
                return Status(ErrorCodes::HostUnreachable, "disconnect");
            }
            case InprocPipe::Result::Success: {
            }
    }

    int len = header.constView().getMessageLength();

    auto buf = SharedBuffer::allocate(len);
    MsgData::View md = buf.get();
    std::memcpy(md.view2ptr(), &header, sizeof(header));

    const int left = len - sizeof(header);
    if (left) {
        switch (_clientToServerPipe.recv(reinterpret_cast<uint8_t*>(md.data()), left, timeout)) {
            case InprocPipe::Result::Timeout: {
                return Status(ErrorCodes::NetworkTimeout, "timeout in server response");
            }
            case InprocPipe::Result::Closed: {
                return Status(ErrorCodes::HostUnreachable, "disconnect");
            }
            case InprocPipe::Result::Success: {
            }
        }
    }

    toRecv->setData(std::move(buf));

    return Status::OK();
}

bool TransportLayerInProc::Connection::clientSend(const void* buffer,
                                                size_t bufferLen,
                                                stdx::chrono::milliseconds timeout) {
    switch (
        _clientToServerPipe.send(reinterpret_cast<const uint8_t*>(buffer), bufferLen, timeout)) {
        case InprocPipe::Result::Timeout:
        case InprocPipe::Result::Closed:
            return false;
        case InprocPipe::Result::Success: {}
    }

    return true;
}

bool TransportLayerInProc::Connection::clientRecv(void* buffer,
                                                size_t bufferLen,
                                                stdx::chrono::milliseconds timeout) {
    switch (_serverToClientPipe.recv(reinterpret_cast<uint8_t*>(buffer), bufferLen, timeout)) {
        case InprocPipe::Result::Timeout:
        case InprocPipe::Result::Closed:
            return false;
        case InprocPipe::Result::Success: {}
    }

    return true;
}

bool TransportLayerInProc::Connection::clientCanRead() {
    return _serverToClientPipe.canRead();
}

bool TransportLayerInProc::Connection::clientCanWrite() {
    return _clientToServerPipe.canWrite();
}

}  // namespace transport
}  // namespace mongo
