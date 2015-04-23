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

        if (r < 0) {
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
        struct sockaddr_in saddr;
        int r;

        _cfd = socket(AF_INET, SOCK_STREAM, 0);

        memset(&saddr, 0, sizeof(saddr));

        saddr.sin_family = AF_INET;
        saddr.sin_port = htons(40001);
        saddr.sin_addr.s_addr = htonl(INADDR_ANY);

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
        _enqueue_packet(BSON("setValue" << BSON_ARRAY(field << val)));
    }

    void V8Scope::setString(const char * field, StringData val) {
        _enqueue_packet(BSON("setValue" << BSON_ARRAY(field << val)));
    }

    void V8Scope::setBoolean(const char * field, bool val) {
        _enqueue_packet(BSON("setValue" << BSON_ARRAY(field << val)));
    }

    void V8Scope::setElement(const char *field, const BSONElement& e) {
        _enqueue_packet(BSON("setValue" << BSON_ARRAY(field << e)));
    }

    void V8Scope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
        _enqueue_packet(BSON("setValue" << BSON_ARRAY(field << obj)));
    }

    int V8Scope::type(const char *field) {
        _enqueue_packet(BSON("getType" << field));
        _send();

        auto x = _recv();
        return x["return"].Int();
    }

    double V8Scope::getNumber(const char *field) {
        _enqueue_packet(BSON("getNumber" << field));
        _send();

        auto x = _recv();
        return x["return"].Double();
    }

    int V8Scope::getNumberInt(const char *field) {
        _enqueue_packet(BSON("getNumberInt" << field));
        _send();

        auto x = _recv();
        return x["return"].Int();
    }

    long long V8Scope::getNumberLongLong(const char *field) {
        _enqueue_packet(BSON("getNumberLongLong" << field));
        _send();

        auto x = _recv();
        return x["return"].Long();
    }

    string V8Scope::getString(const char *field) {
        _enqueue_packet(BSON("getString" << field));
        _send();

        auto x = _recv();
        return x["return"].String();
    }

    bool V8Scope::getBoolean(const char *field) {
        _enqueue_packet(BSON("getBoolean" << field));
        _send();

        auto x = _recv();
        return x["return"].Bool();
    }

    BSONObj V8Scope::getObject(const char * field) {
        _enqueue_packet(BSON("getObject" << field));
        _send();

        auto x = _recv();
        return x["return"].Obj().getOwned();
    }

    ScriptingFunction V8Scope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        _enqueue_packet(BSON("_createFunction" << BSON_ARRAY(raw << static_cast<long long>(functionNumber))));
        _send();

        auto x = _recv();

        return x["return"].Long();
    }

    void V8Scope::setFunction(const char* field, const char* code) {
        _enqueue_packet(BSON("setFunction" << BSON_ARRAY(field << code)));
    }

    void V8Scope::rename(const char * from, const char * to) {
        _enqueue_packet(BSON("rename" << BSON_ARRAY(from << to)));
    }

    void V8Scope::_enqueue_packet(const BSONObj& obj) {
        _send_buf.appendBuf(obj.objdata(), obj.objsize());
    }

    void V8Scope::_send() {
        my_send(_cfd, _send_buf.buf(), _send_buf.len());
        _send_buf.reset();
    }

    BSONObj V8Scope::_recv() {
        char len_buf[4];

        my_recv(_cfd, len_buf, sizeof(len_buf));

        uint32_t to_read = uassertStatusOK(ConstDataRange(len_buf, len_buf + 4).read<LittleEndian<uint32_t>>());

        auto buffer = stdx::make_unique<char[]>(to_read);
        std::memcpy(buffer.get(), len_buf, sizeof(len_buf));

        my_recv(_cfd, buffer.get() + 4, to_read - 4);

        BSONObj reply;

        uassertStatusOK(ConstDataRange(buffer.get(), buffer.get() + to_read).read(&reply));

        return reply.getOwned();
    }

    int V8Scope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        BSONObjBuilder root;

        BSONObjBuilder bob(root.subobjStart("invoke"));

        bob.appendIntOrLL("func", func);
        if (argsObject) {
            bob.appendObject("argsObject", argsObject->objdata(), argsObject->objsize());
        }
        if (recv) {
            bob.appendObject("recv", recv->objdata(), recv->objsize());
        }
        bob.appendIntOrLL("timeoutMs", timeoutMs);
        bob.appendBool("ignoreReturn", ignoreReturn);
        bob.appendBool("readOnlyArgs", readOnlyArgs);
        bob.appendBool("readOnlyRecv", readOnlyRecv);

        bob.done();

        BSONObj req(root.obj());

        _enqueue_packet(req);
        _send();

        BSONObj reply;

        for (;;) {
            reply = _recv();

            if (reply.hasField("native")) {
                auto args = reply["native"].Array();

                auto& x = _native_functions[args[0].String()];

                auto y = std::get<0>(x)(args[1].Obj(), std::get<1>(x));

                _enqueue_packet(BSON("return" << y));
                _send();
            } else {
                break;
            }
        }

        if (reply.hasField("exception")) {
            auto e = reply["exception"].Obj();

            uassertStatusOK(Status(static_cast<ErrorCodes::Error>(e["code"].Int()), e["reason"].String()));
        }

        return reply["return"].Int();
    }

    bool V8Scope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        BSONObjBuilder root;
        BSONObjBuilder bob(root.subobjStart("exec"));

        bob.append("code", code);
        bob.append("name", name);
        bob.append("printResult", printResult);
        bob.append("reportError", reportError);
        bob.append("assertOnError", assertOnError);
        bob.append("timeoutMs", timeoutMs);
        
        bob.done();

        BSONObj obj = root.obj();

        _enqueue_packet(obj);
        _send();

        obj = _recv();

        return obj["return"].Bool();
    }

    void V8Scope::injectNative(const char *field, NativeFunction func, void* data) {
        _native_functions[field] = std::make_tuple(func, data);

        _enqueue_packet(BSON("injectNative" << field));
    }

    void V8Scope::gc() {
        // TODO ...
    }

    void V8Scope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        _enqueue_packet(BSON("localConnectForDbEval" << dbName));
        _localDBName = dbName;
        loadStored(txn);
    }

    void V8Scope::externalSetup() {
        _enqueue_packet(BSON("externalSetup" << true));
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
