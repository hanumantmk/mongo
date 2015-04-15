/*    Copyright 2014 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_type_endian.h"
#include "mongo/base/data_type_terminated.h"
#include "mongo/base/data_type_string_data.h"

#include "mongo/scripting/engine_v8-oop-4.1.h"

#include <iostream>

#include "mongo/base/data_range.h"
#include "mongo/base/init.h"
#include "mongo/stdx/memory.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/scripting/v8-oop-4.1_db.h"
#include "mongo/scripting/v8-oop-4.1_utils.h"
#include "mongo/util/base64.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

# include <arpa/inet.h>
# include <poll.h>
# include <netdb.h>
# include <netinet/in.h>
# include <netinet/tcp.h>
# include <sys/types.h>
# include <sys/socket.h>
# include <sys/uio.h>
# include <sys/un.h>
# include <thread>
# include <mutex>

using namespace mongoutils;

namespace mongo {

    using std::cout;
    using std::endl;
    using std::map;
    using std::string;
    using std::stringstream;

    namespace {
        std::mutex g_oop_mutex;
    }

void my_send(int fd, const char* buf, size_t len) {
    while (len) {
        ssize_t r = send(fd, buf, len, 0);

        if (r < 0) {
            abort();
        }

        if (r > 0) {
            len -= r;
            buf += r;
        }
    }
}

void my_recv(int fd, char* buf, size_t len) {
    while (len) {
        ssize_t r = recv(fd, buf, len, 0);

        if (r <= 0) {
            abort();
        }

        if (r > 0) {
            len -= r;
            buf += r;
        }
    }
}

    // Generated symbols for JS files
    namespace JSFiles {
        extern const JSFile types;
        extern const JSFile assert;
    }

     V8ScriptEngine::V8ScriptEngine() {
     }

     V8ScriptEngine::~V8ScriptEngine() {
     }

     void ScriptEngine::setup() {
         if (!globalScriptEngine) {
             globalScriptEngine = new V8ScriptEngine();

             if (hasGlobalServiceContext()) {
                 getGlobalServiceContext()->registerKillOpListener(globalScriptEngine);
             }
         }
     }

     std::string ScriptEngine::getInterpreterVersionString() {
         return "V8 4.1.27";
     }

     std::string V8Scope::getError() {
         return ""; // TODO ...
     }

     void V8ScriptEngine::interrupt(unsigned opId) {
         // TODO ...
     }

     void V8ScriptEngine::interruptAll() {
         // TODO ...
     }

     void V8Scope::registerOperation(OperationContext* txn) {
         // TODO ...
     }

     void V8Scope::unregisterOperation() {
         // TODO ...
    }

    void V8Scope::kill() {
        // TODO ...
    }

    /** check if there is a pending killOp request */
    bool V8Scope::isKillPending() const {
        // TODO ...
        return false;
    }
    
    OperationContext* V8Scope::getOpContext() const {
        // TODO ...
        return nullptr;
    }

    V8Scope::V8Scope(V8ScriptEngine * engine) :
        _global_engine(engine)
    {
        struct sockaddr_un saddr;
        int r;

        _cfd = socket(AF_UNIX, SOCK_STREAM, 0);

        memset(&saddr, 0, sizeof(saddr));

        saddr.sun_family = AF_UNIX;
        char path[] = "\0/mongodb/js.sock";
        std::memcpy(saddr.sun_path, path, sizeof(path));

        r = connect(_cfd, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr));
    }

    V8Scope::~V8Scope() {
        unregisterOperation();
        close (_cfd);
    }

    bool V8Scope::hasOutOfMemoryException() {
        return false;
    }

    void V8Scope::init(const BSONObj * data) {
        if (! data)
            return;

        BSONObjIterator i(*data);
        while (i.more()) {
            BSONElement e = i.next();
            setElement(e.fieldName(), e);
        }
    }

    void V8Scope::setNumber(const char * field, double val) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::setNumber)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<double>(val)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::setString(const char * field, StringData val) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::setString)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(val)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::setBoolean(const char * field, bool val) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::setBoolean)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(val)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::setElement(const char *field, const BSONElement& e) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::setElement)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        uassertStatusOK(drc.writeAndAdvance(BSON("v" << e)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::setObject)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        uassertStatusOK(drc.writeAndAdvance(obj));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    int V8Scope::type(const char *field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::type)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        DataRange dr(_recv(buf, sizeof(buf)));
        return uassertStatusOK(dr.read<LittleEndian<int32_t>>());
    }

    double V8Scope::getNumber(const char *field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::getNumber)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<LittleEndian<double>>());
    }

    int V8Scope::getNumberInt(const char *field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::getNumberInt)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<LittleEndian<int32_t>>());
    }

    long long V8Scope::getNumberLongLong(const char *field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::getNumberLongLong)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<LittleEndian<int64_t>>());
    }

    string V8Scope::getString(const char *field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::getString)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<Terminated<0, StringData>>()).value.toString();
    }

    bool V8Scope::getBoolean(const char *field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::getBoolean)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<uint8_t>());
    }

    BSONObj V8Scope::getObject(const char * field) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::getObject)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<BSONObj>()).getOwned();
    }

    ScriptingFunction V8Scope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::_createFunction)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(raw)));
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<int64_t>(functionNumber)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        drc = DataRange(_recv(buf, sizeof(buf)));
        return uassertStatusOK(drc.read<LittleEndian<int64_t>>());
    }

    void V8Scope::setFunction(const char* field, const char* code) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::setFunction)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(code)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::rename(const char * from, const char * to) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::rename)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(from)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(to)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::_enqueue_packet(ConstDataRange cdr) {
        _send_buf.appendBuf(cdr.data(), cdr.length());
    }

    void V8Scope::_send() {
        my_send(_cfd, _send_buf.buf(), _send_buf.len());
        _send_buf.reset();
    }

    DataRange V8Scope::_recv(char *buffer, size_t len) {
        for (;;) {
            char header[5];
            ScopeMethods smr;

            my_recv(_cfd, header, sizeof(header));

            uint32_t to_read = uassertStatusOK(ConstDataRange(header, header + 4).read<LittleEndian<uint32_t>>()) - 5;

            invariant(to_read <= len);

            my_recv(_cfd, buffer, to_read);
            ConstDataRangeCursor cursor(buffer, buffer + to_read);

            uassertStatusOK(ConstDataRange(header + 4, header + 5).read(&smr));

            switch (smr) {
                case ScopeMethods::native: {
                    StringData name = uassertStatusOK(cursor.readAndAdvance<Terminated<0, StringData>>());
                    BSONObj args = uassertStatusOK(cursor.readAndAdvance<BSONObj>());

                    auto& x = _native_functions[name.toString()];
                    auto y = std::get<0>(x)(args, std::get<1>(x));

                    // char buf[4096];

                    DataRangeCursor drc(buf, buf + sizeof(buf));

                    uassertStatusOK(drc.advance(4));
                    uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::_return)));
                    uassertStatusOK(drc.writeAndAdvance(y));
                    DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

                    _enqueue_packet(ConstDataRange(buf, drc.data()));
                    _send();

                    break;
                }
                case ScopeMethods::_return: {
                    return DataRange(buffer, buffer + to_read);
                }
                case ScopeMethods::exception: {
                    ErrorCodes::Error code = static_cast<ErrorCodes::Error>(uassertStatusOK(cursor.readAndAdvance<LittleEndian<uint32_t>>()).value);
                    StringData reason = uassertStatusOK(cursor.readAndAdvance<Terminated<0, StringData>>());

                    uassertStatusOK(Status(code, reason.toString()));
                    break;
                }
                default:
                    invariant(false);
            }
        }
    }

    int V8Scope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::invoke)));
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<int64_t>(func)));
        if (argsObject) {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(1)));
            uassertStatusOK(drc.writeAndAdvance(*argsObject));
        } else {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(0)));
        }
        if (recv) {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(1)));
            uassertStatusOK(drc.writeAndAdvance(*recv));
        } else {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(0)));
        }
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<int32_t>(timeoutMs)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ignoreReturn)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(readOnlyArgs)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(readOnlyRecv)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        DataRange dr = _recv(buf, sizeof(buf));

        return uassertStatusOK(dr.read<LittleEndian<int32_t>>());

    }

    int V8Scope::invokeWhere(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                             int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::invokeWhere)));
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<int64_t>(func)));
        if (argsObject) {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(1)));
            uassertStatusOK(drc.writeAndAdvance(*argsObject));
        } else {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(0)));
        }
        if (recv) {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(1)));
            uassertStatusOK(drc.writeAndAdvance(*recv));
        } else {
            uassertStatusOK(drc.writeAndAdvance(uint8_t(0)));
        }
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<int32_t>(timeoutMs)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ignoreReturn)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(readOnlyArgs)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(readOnlyRecv)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        DataRange dr = _recv(buf, sizeof(buf));

        return uassertStatusOK(dr.read<LittleEndian<int32_t>>());
    }

    bool V8Scope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::exec)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(code)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(name)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(printResult)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(reportError)));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(assertOnError)));
        uassertStatusOK(drc.writeAndAdvance(LittleEndian<int32_t>(timeoutMs)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
        _send();

        DataRange dr = _recv(buf, sizeof(buf));

        return uassertStatusOK(dr.read<uint8_t>());
    }

    void V8Scope::injectNative(const char *field, NativeFunction func, void* data) {
        _native_functions[field] = std::make_tuple(func, data);

        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::injectNative)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(field)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    void V8Scope::gc() {
        // TODO ...
    }

    void V8Scope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::localConnectForDbEval)));
        uassertStatusOK(drc.writeAndAdvance(Terminated<0, StringData>(dbName)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));

        _localDBName = dbName;
        loadStored(txn);
    }

    void V8Scope::externalSetup() {
        // char buf[4096];

        DataRangeCursor drc(buf, buf + sizeof(buf));

        uassertStatusOK(drc.advance(4));
        uassertStatusOK(drc.writeAndAdvance(uint8_t(ScopeMethods::externalSetup)));
        DataView(buf).write(LittleEndian<uint32_t>(drc.data() - buf));

        _enqueue_packet(ConstDataRange(buf, drc.data()));
    }

    // ----- internal -----

    void V8Scope::reset() {
    }

    // --- random utils ----

    static logger::MessageLogDomain* jsPrintLogDomain;

    MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
        jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
        return Status::OK();
    }

} // namespace mongo
