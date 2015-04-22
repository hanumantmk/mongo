#include "mongo/platform/basic.h"

#include <string>

#include <cstdint>
#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/base/data_range.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_type_endian.h"
#include "mongo/base/initializer.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/client/dbclientinterface.h"
#include "mongo/logger/log_component.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/concurrency/mutex.h"
#include "mongo/util/signal_handlers.h"
#include "mongo/util/exit.h"
#include "mongo/util/quick_exit.h"
#include "mongo/scripting/engine.h"
#include "mongo/stdx/memory.h"

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

#define DEBUG 0

using namespace mongo;

MONGO_INITIALIZER_GENERAL(ForkServer,
                          ("EndStartupOptionHandling"),
                          ("default"))(InitializerContext* context) {
    return Status::OK();
}

bool my_send(int fd, const char* buf, size_t len) {
    while (len) {
        ssize_t r = send(fd, buf, len, 0);

        if (r < 0) {
            return false;
        }

        if (r > 0) {
            len -= r;
            buf += r;
        }
    }

    return true;
}

bool send_obj(int fd, const BSONObj& obj) {
    return my_send(fd, obj.objdata(), obj.objsize());
}

bool my_recv(int fd, char* buf, size_t len) {
    while (len) {
        ssize_t r = recv(fd, buf, len, 0);

        if (r <= 0) {
            return false;
        }

        if (r > 0) {
            len -= r;
            buf += r;
        }
    }

    return true;
}

bool recv_obj(int fd, BSONObj* obj) {
    char len_buf[4];

    if (! my_recv(fd, len_buf, sizeof(len_buf))) {
        return false;
    }

    uint32_t to_read = uassertStatusOK(ConstDataRange(len_buf, len_buf + 4).read<LittleEndian<uint32_t>>());

    auto buffer = stdx::make_unique<char[]>(to_read);
    std::memcpy(buffer.get(), len_buf, sizeof(len_buf));

    if (! my_recv(fd, buffer.get() + 4, to_read - 4)) {
        return false;
    }

    BSONObj reply;

    uassertStatusOK(ConstDataRange(buffer.get(), buffer.get() + to_read).read(&reply));

    *obj = reply.getOwned();

    return true;
}

static bool work_recurse(int fd, Scope* scope, const BSONObj& in);

class NativeWrapper {
public:
    NativeWrapper(std::string name, int fd, Scope* scope) :
        _name(std::move(name)),
        _fd(fd),
        _scope(scope)
    {
    }

    static BSONObj wrap(const BSONObj& args, void* ctx)
    {
        auto x = static_cast<NativeWrapper*>(ctx);

        BSONObjBuilder root;
        BSONArrayBuilder bab(root.subarrayStart("native"));

        bab.append(x->_name.c_str());
        bab.append(args);
        bab.done();

        BSONObj out(root.obj());

        if (DEBUG) std::cout << "in native wrapper" << std::endl;

        if (! send_obj(x->_fd, out)) {
            if (DEBUG) std::cout << "failed send" << std::endl;
            return BSONObj();
        }

        for (;;) {
            BSONObj in;

            if (! recv_obj(x->_fd, &in)) {
                if (DEBUG) std::cout << "failed recv" << std::endl;
                return BSONObj();
            }
            if (DEBUG) std::cout << "GOT: " << in << std::endl;

            if (in.hasField("return")) {
                return in["return"].Obj().getOwned();
            } else {
                if (DEBUG) std::cout << "recursive invoke..." << std::endl;
                if (! work_recurse(x->_fd, x->_scope, in)) {
                    return BSONObj();
                }
            }
        }
    }

    std::string _name;
    int _fd;
    Scope* _scope;
};

static bool work_recurse(int fd, Scope* scope, const BSONObj& in) {
    if (DEBUG) std::cout << fd << " IN_size: " << in.objsize() << std::endl;
    if (DEBUG) std::cout << fd << " IN: " << in << std::endl;

    bool should_write = false;
    BSONObj out;

    if (in.hasField("setValue")) {
        auto args = in["setValue"].Array();
        scope->setElement(args[0].String().c_str(), args[1]);
    } else if (in.hasField("getString")) {
        out = BSON("return" << scope->getString(in["getString"].String().c_str()));
        should_write = true;
    } else if (in.hasField("getBoolean")) {
        out = BSON("return" << scope->getBoolean(in["getBoolean"].String().c_str()));
        should_write = true;
    } else if (in.hasField("getNumber")) {
        out = BSON("return" << scope->getNumber(in["getNumber"].String().c_str()));
        should_write = true;
    } else if (in.hasField("getNumberInt")) {
        out = BSON("return" << scope->getNumberInt(in["getNumberInt"].String().c_str()));
        should_write = true;
    } else if (in.hasField("getNumberLongLong")) {
        out = BSON("return" << scope->getNumberLongLong(in["getNumberLongLong"].String().c_str()));
        should_write = true;
    } else if (in.hasField("getObject")) {
        out = BSON("return" << scope->getObject(in["getObject"].String().c_str()));
        should_write = true;
    } else if (in.hasField("getType")) {
        auto field = in["getType"].String();

        out = BSON("return" << scope->type(field.c_str()));
        should_write = true;
    } else if (in.hasField("_createFunction")) {
        auto field = in["_createFunction"].Array();

        auto rval = scope->_createFunction(field[0].String().c_str(), field[1].Long());

        out = BSON("return" << static_cast<long long>(rval));
        should_write = true;
    } else if (in.hasField("setFunction")) {
        auto field = in["setFunction"].Array();

        scope->setFunction(field[0].String().c_str(), field[1].String().c_str());
    } else if (in.hasField("rename")) {
        auto field = in["rename"].Array();

        scope->setFunction(field[0].String().c_str(), field[1].String().c_str());
    } else if (in.hasField("injectNative")) {
        auto field = in["injectNative"].String();

        scope->injectNative(
            field.c_str(),
            NativeWrapper::wrap,
            new NativeWrapper(field.c_str(), fd, scope)
        );
    } else if (in.hasField("invoke")) {
        auto cmd = in["invoke"].Obj();

        BSONObj recv_obj;
        BSONObj args_obj;
        BSONObj* args_obj_ptr = nullptr;
        BSONObj* recv_obj_ptr = nullptr;

        auto func = cmd["func"].Int();

        if (cmd.hasField("recv")) {
            recv_obj = cmd["recv"].Obj();
            recv_obj_ptr = &recv_obj;
        }

        if (cmd.hasField("argsObject")) {
            args_obj = cmd["argsObject"].Obj();
            args_obj_ptr = &args_obj;
        }

        int timeoutMs = cmd["timeoutMs"].Int();
        bool ignoreReturn = cmd["ignoreReturn"].Bool();

        int err;

        try {
            err = scope->invoke(func, args_obj_ptr, recv_obj_ptr, timeoutMs, ignoreReturn);

            out = BSON("return" << err);
        } catch (...) {
            auto status = exceptionToStatus();

            out = BSON("exception" << BSON("code" << status.code() << "reason" << status.reason()));
        }

        should_write = true;
    } else if (in.hasField("exec")) {
        auto cmd = in["exec"].Obj();

        auto rval = scope->exec(
            cmd["code"].String(),
            cmd["name"].String(),
            cmd["printResult"].Bool(),
            cmd["reportError"].Bool(),
            cmd["assertOnError"].Bool(),
            cmd["timeoutMs"].Int()
        );

        out = BSON("return" << rval);

        should_write = true;
    } else if (in.hasField("exec")) {
        auto cmd = in["exec"].Obj();

        auto rval = scope->exec(
            cmd["code"].String(),
            cmd["name"].String(),
            cmd["printResult"].Bool(),
            cmd["reportError"].Bool(),
            cmd["assertOnError"].Bool(),
            cmd["timeoutMs"].Int()
        );

        out = BSON("return" << rval);

        should_write = true;
    } else if (in.hasField("externalSetup")) {
        scope->externalSetup();
    } else if (in.hasField("localConnectForDbEval")) {
        scope->localConnectForDbEval(nullptr, in["localConnectForDbEval"].String().c_str());
    } else {
        if (DEBUG) std::cout << fd << " Unknown command" << std::endl;
    }

    if (should_write) {
        if (DEBUG) std::cout << fd << " OUT: " << out << std::endl;

        if (! send_obj(fd, out)) return false;
    }

    if (DEBUG) std::cout << fd << " PROCESSED: " << in << std::endl;

    return true;
}

static void work(int fd) {
    std::unique_ptr<Scope> scope(static_cast<Scope*>(globalScriptEngine->newScope()));

    for (;;) {
        if (DEBUG) std::cout << fd << " waiting for a len..." << std::endl;

        BSONObj in;

        if (! recv_obj(fd, &in)) {
            break;
        }

        if (! work_recurse(fd, scope.get(), in)) {
            break;
        }
    }

    if (DEBUG) std::cout << fd << " leaving" << std::endl;

    close(fd);
}

static ExitCode runMongoJSServer() {
    setThreadName( "mongojsMain" );

    int optval;
    int lfd;
    struct sockaddr_in saddr;
    int r;

    if (DEBUG) std::cout << "got here\n";

    lfd = socket(AF_INET, SOCK_STREAM, 0);

    optval = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&saddr, 0, sizeof(saddr));

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(40001);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    r = bind(lfd, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr));

    r = listen(lfd, 10);

    for (;;) {
        int fd = accept(lfd, nullptr, nullptr);

        std::thread worker(work, fd);
        worker.detach();
    }

    // listen() will return when exit code closes its socket.
    return EXIT_NET_ERROR;
}

static int _main() {
    ExitCode exitCode = runMongoJSServer();

    // To maintain backwards compatibility, we exit with EXIT_NET_ERROR if the listener loop returns.
    if (exitCode == EXIT_NET_ERROR) {
        dbexit( EXIT_NET_ERROR );
    }

    return (exitCode == EXIT_CLEAN) ? 0 : 1;
}

int mongoJSMain(int argc, char* argv[], char** envp) {
    using std::cout;
    using std::endl;

    static StaticObserver staticObserver;
    if (argc < 1)
        return EXIT_FAILURE;

    setupSignalHandlers(false);

    Status status = mongo::runGlobalInitializers(argc, argv, envp);
    if (DEBUG) std::cout << status << std::endl;
    if (!status.isOK()) {
        quickExit(EXIT_FAILURE);
    }

    ScriptEngine::setup();

    try {
        int exitCode = _main();
        return exitCode;
    }
    catch(SocketException& e) {
        cout << "uncaught SocketException in mongojs main:" << endl;
        cout << e.toString() << endl;
    }
    catch(DBException& e) {
        cout << "uncaught DBException in mongojs main:" << endl;
        cout << e.toString() << endl;
    }
    catch(std::exception& e) {
        cout << "uncaught std::exception in mongojs main:" << endl;
        cout << e.what() << endl;
    }
    catch(...) {
        cout << "uncaught unknown exception in mongojs main" << endl;
    }
    return 20;
}

int main(int argc, char* argv[], char** envp) {
    int exitCode = mongoJSMain(argc, argv, envp);
    quickExit(exitCode);
}
