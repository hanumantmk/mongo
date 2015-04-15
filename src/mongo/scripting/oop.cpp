#include "mongo/platform/basic.h"

#include <string>

#include <cstdint>
#include "mongo/base/init.h"
#include "mongo/base/initializer.h"
#include "mongo/base/data_range.h"
#include "mongo/base/data_range_cursor.h"
#include "mongo/base/data_type_endian.h"
#include "mongo/base/data_type_terminated.h"
#include "mongo/base/data_type_string_data.h"
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
#define INVOKE 1

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

bool send_obj(int fd, ConstDataRange cdr) {
    return my_send(fd, cdr.data(), cdr.length());
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

class Reader {
public:
    Reader (int fd) :
        fd(fd),
        total_bytes(0),
        read_bytes(0)
    {}

    DataRange recv() {
        if (total_bytes == read_bytes) {
            total_bytes = 0;
            read_bytes = 0;
        }

        while (total_bytes - read_bytes < 4) {
            ssize_t r = ::recv(fd, buf + total_bytes, sizeof(buf) - total_bytes, 0);

            if (r <= 0) {
                throw "done";
            }

            total_bytes += r;
        }

        uint32_t packet_length = ConstDataView(buf + read_bytes).read<LittleEndian<uint32_t>>();

        while (total_bytes - read_bytes < packet_length) {
            ssize_t r = ::recv(fd, buf + total_bytes, sizeof(buf) - total_bytes, 0);

            if (r <= 0) {
                throw "done";
            }

            total_bytes += r;
        }

        size_t old_head = read_bytes;

        read_bytes += packet_length;

        return DataRange(buf + old_head + 4, buf + read_bytes);
    }

private:
    int fd;
    size_t total_bytes;
    size_t read_bytes;
    char buf[1 << 23];
};

static constexpr size_t buf_len = 1 << 14;

bool recv_obj(int fd, char* buffer, size_t buf_len, DataRange* dr) {
    char len_buf[4];

    if (! my_recv(fd, len_buf, sizeof(len_buf))) {
        return false;
    }

    uint32_t to_read = uassertStatusOK(ConstDataRange(len_buf, len_buf + 4).read<LittleEndian<uint32_t>>()) - 4;

    if (DEBUG) std::cout << fd << " going to read " << to_read << std::endl;

    invariant(to_read <= buf_len);

    if (! my_recv(fd, buffer, to_read)) {
        return false;
    }

    if (DEBUG) std::cout << fd << " finished reading" << std::endl;

    *dr = DataRange(buffer, buffer + to_read);

    return true;
}

static bool work_recurse(int fd, Scope* scope, DataRange dr, char* buffer, size_t buffer_len);

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

        char buffer[4096];

        DataRangeCursor out(buffer, buffer + sizeof(buffer));

        uassertStatusOK(out.advance(4));

        uassertStatusOK(out.writeAndAdvance(uint8_t(ScopeMethods::native)));
        uassertStatusOK(out.writeAndAdvance(Terminated<0, StringData>(x->_name)));
        uassertStatusOK(out.writeAndAdvance(args));
        DataView(buffer).write(LittleEndian<uint32_t>(out.data() - buffer));

        if (DEBUG) std::cout << "in native wrapper" << std::endl;
        if (DEBUG) std::cout << "in native wrapper : " << args << std::endl;

        if (! send_obj(x->_fd, ConstDataRange(buffer, out.data()))) {
            if (DEBUG) std::cout << "failed send" << std::endl;
            return BSONObj();
        }

        if (DEBUG) std::cout << "sent" << std::endl;

        for (;;) {
            DataRange in_(nullptr, nullptr);

            if (! recv_obj(x->_fd, buffer, sizeof(buffer), &in_)) {
                if (DEBUG) std::cout << "failed recv" << std::endl;
                return BSONObj();
            }

            if (DEBUG) std::cout << "received " << in_.length() << std::endl;

            DataRangeCursor in(in_);

            ScopeMethods smr = static_cast<ScopeMethods>(uassertStatusOK(in.readAndAdvance<uint8_t>()));

            if (smr == ScopeMethods::_return) {
                BSONObj out = uassertStatusOK(in.readAndAdvance<BSONObj>());

                return out.getOwned();
            } else {
                if (DEBUG) std::cout << "recursive invoke..." << std::endl;
                if (! work_recurse(x->_fd, x->_scope, in_, nullptr, 0)) {
                    return BSONObj();
                }
            }
        }
    }

    std::string _name;
    int _fd;
    Scope* _scope;
};

static bool work_recurse(int fd, Scope* scope, DataRange in_, char* buffer, size_t buffer_len) {
    DataRangeCursor in = in_;

    DataRangeCursor out(buffer, buffer + buffer_len);

    uassertStatusOK(out.advance(5));

    if (DEBUG) std::cout << fd << " IN_size: " << in.length() << std::endl;

    bool should_write = false;

    ScopeMethods sm = static_cast<ScopeMethods>(uassertStatusOK(in.readAndAdvance<uint8_t>()));

    if (DEBUG) std::cout << fd << " sm: " << int(sm) << std::endl;

    switch (sm) {
        case ScopeMethods::setElement:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            BSONObj obj = uassertStatusOK(in.readAndAdvance<BSONObj>());
            scope->setElement(field.rawData(), obj["v"]);
            break;
        }
        case ScopeMethods::setNumber:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            double val = uassertStatusOK(in.readAndAdvance<LittleEndian<double>>());
            scope->setNumber(field.rawData(), val);
            break;
        }
        case ScopeMethods::setString:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            StringData val = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            scope->setString(field.rawData(), val);
            break;
        }
        case ScopeMethods::setObject:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            BSONObj obj = uassertStatusOK(in.readAndAdvance<BSONObj>());
            scope->setObject(field.rawData(), obj);
            break;
        }
        case ScopeMethods::setBoolean:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            bool val = uassertStatusOK(in.readAndAdvance<uint8_t>());
            scope->setBoolean(field.rawData(), val);
            break;
        }
        case ScopeMethods::getNumber:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            double val = scope->getNumber(field.rawData());
            uassertStatusOK(out.writeAndAdvance(LittleEndian<double>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::getNumberInt:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            int val = scope->getNumberInt(field.rawData());
            uassertStatusOK(out.writeAndAdvance(LittleEndian<int32_t>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::getNumberLongLong:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            long long val = scope->getNumberLongLong(field.rawData());
            uassertStatusOK(out.writeAndAdvance(LittleEndian<int64_t>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::getString:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            std::string val = scope->getString(field.rawData());
            uassertStatusOK(out.writeAndAdvance(Terminated<0, StringData>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::getBoolean:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            bool val = scope->getBoolean(field.rawData());
            uassertStatusOK(out.writeAndAdvance(uint8_t(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::getObject:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            BSONObj val = scope->getObject(field.rawData());
            uassertStatusOK(out.writeAndAdvance(val));

            should_write = true;
            break;
        }
        case ScopeMethods::_createFunction:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            int64_t number = uassertStatusOK(in.readAndAdvance<LittleEndian<int64_t>>());

            auto val = scope->_createFunction(field.rawData(), number);

            uassertStatusOK(out.writeAndAdvance(LittleEndian<int64_t>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::setFunction:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            StringData code = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());

            scope->setFunction(field.rawData(), code.rawData());

            break;
        }
        case ScopeMethods::type:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            auto val = scope->type(field.rawData());
            uassertStatusOK(out.writeAndAdvance(LittleEndian<int32_t>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::rename:
        {
            StringData from = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            StringData to = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());

            scope->rename(from.rawData(), to.rawData());

            break;
        }
        case ScopeMethods::injectNative:
        {
            StringData field = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());

            scope->injectNative(
                field.rawData(),
                NativeWrapper::wrap,
                new NativeWrapper(field.rawData(), fd, scope)
            );

            break;
        }
        case ScopeMethods::invoke:
        {
#if INVOKE
            BSONObj argsObject;
            BSONObj* argsObjectPtr = nullptr;
            BSONObj recv;
            BSONObj* recvPtr = nullptr;

            ScriptingFunction func = uassertStatusOK(in.readAndAdvance<LittleEndian<int64_t>>());

            if(uassertStatusOK(in.readAndAdvance<uint8_t>())) {
                uassertStatusOK(in.readAndAdvance(&argsObject));
                argsObjectPtr = &argsObject;
            }

            if(uassertStatusOK(in.readAndAdvance<uint8_t>())) {
                uassertStatusOK(in.readAndAdvance(&recv));
                recvPtr = &recv;
            }

            int timeoutMs = uassertStatusOK(in.readAndAdvance<LittleEndian<int32_t>>());
            bool ignoreReturn = uassertStatusOK(in.readAndAdvance<uint8_t>());
            bool readOnlyArgs = uassertStatusOK(in.readAndAdvance<uint8_t>());
            bool readOnlyRecv = uassertStatusOK(in.readAndAdvance<uint8_t>());

            auto val = scope->invoke(func, argsObjectPtr, recvPtr, timeoutMs, ignoreReturn, readOnlyArgs, readOnlyRecv);
#else
            uassertStatusOK(in.skip<LittleEndian<int64_t>>());

            if(uassertStatusOK(in.readAndAdvance<uint8_t>())) {
                uassertStatusOK(in.skip<BSONObj>());
            }

            if(uassertStatusOK(in.readAndAdvance<uint8_t>())) {
                uassertStatusOK(in.skip<BSONObj>());
            }

            uassertStatusOK(in.skip<LittleEndian<int32_t>>());
            uassertStatusOK(in.skip<uint8_t>());
            uassertStatusOK(in.skip<uint8_t>());
            uassertStatusOK(in.skip<uint8_t>());

            scope->setBoolean("__returnValue", 0);
            int32_t val = 0;
#endif

            uassertStatusOK(out.writeAndAdvance(LittleEndian<int32_t>(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::invokeWhere:
        {
            BSONObj argsObject;
            BSONObj* argsObjectPtr = nullptr;
            BSONObj recv;
            BSONObj* recvPtr = nullptr;

            ScriptingFunction func = uassertStatusOK(in.readAndAdvance<LittleEndian<int64_t>>());

            if(uassertStatusOK(in.readAndAdvance<uint8_t>())) {
                uassertStatusOK(in.readAndAdvance(&argsObject));
                argsObjectPtr = &argsObject;
            }

            if(uassertStatusOK(in.readAndAdvance<uint8_t>())) {
                uassertStatusOK(in.readAndAdvance(&recv));
                recvPtr = &recv;
            }

            int timeoutMs = uassertStatusOK(in.readAndAdvance<LittleEndian<int32_t>>());
            bool ignoreReturn = uassertStatusOK(in.readAndAdvance<uint8_t>());
            bool readOnlyArgs = uassertStatusOK(in.readAndAdvance<uint8_t>());
            bool readOnlyRecv = uassertStatusOK(in.readAndAdvance<uint8_t>());

#if INVOKE
            scope->setObject("obj", *recvPtr);
            scope->setBoolean("fullObject", true);

            auto val = scope->invoke(func, argsObjectPtr, recvPtr, timeoutMs, ignoreReturn, readOnlyArgs, readOnlyRecv);

            if (val < 0) {
                uassertStatusOK(out.writeAndAdvance(LittleEndian<int32_t>(val)));
            } else {
                uassertStatusOK(out.writeAndAdvance(LittleEndian<int32_t>(scope->getBoolean("__returnValue"))));
            }
#else
            uassertStatusOK(out.writeAndAdvance(LittleEndian<int32_t>(1)));
#endif

            should_write = true;
            break;
        }
        case ScopeMethods::exec:
        {
            StringData code = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            StringData name = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());
            bool printResult = uassertStatusOK(in.readAndAdvance<uint8_t>());
            bool reportError = uassertStatusOK(in.readAndAdvance<uint8_t>());
            bool assertOnError = uassertStatusOK(in.readAndAdvance<uint8_t>());
            int timeoutMs = uassertStatusOK(in.readAndAdvance<LittleEndian<int32_t>>());

            auto val = scope->exec(code, name.toString(), printResult, reportError, assertOnError, timeoutMs);

            uassertStatusOK(out.writeAndAdvance(uint8_t(val)));

            should_write = true;
            break;
        }
        case ScopeMethods::externalSetup:
        {
            scope->externalSetup();

            break;
        }
        case ScopeMethods::localConnectForDbEval:
        {
            StringData dbName = uassertStatusOK(in.readAndAdvance<Terminated<0, StringData>>());

            scope->localConnectForDbEval(nullptr, dbName.rawData());

            break;
        }
        default:
            invariant(false);
    }

    if (should_write) {
        DataRangeCursor header(buffer, buffer + 5);

        uassertStatusOK(header.writeAndAdvance(LittleEndian<uint32_t>(out.data() - buffer)));
        uassertStatusOK(header.writeAndAdvance(uint8_t(ScopeMethods::_return)));

        if (! send_obj(fd, ConstDataRange(buffer, out.data()))) return false;
    }

    if (DEBUG) std::cout << fd << " PROCESSED..." << std::endl;

    return true;
}

static void work(int fd) {
    std::unique_ptr<Reader> reader(new Reader(fd));

    std::unique_ptr<char[]> buffer(new char[1 << 21]);
    std::unique_ptr<char[]> buffer2(new char[1 << 21]);

    std::unique_ptr<Scope> scope(static_cast<Scope*>(globalScriptEngine->newScope()));

    for (;;) {
        if (DEBUG) std::cout << fd << " waiting for a len..." << std::endl;

        DataRange out(nullptr, nullptr);

        bool done = false;

        try {
            out = reader->recv();
        } catch (...) {
            done = true;
            break;
        }

        if (done) {
            break;
        }

        if (! work_recurse(fd, scope.get(), out, buffer2.get(), 1 << 21)) {
            break;
        }
    }

    if (DEBUG) std::cout << fd << " leaving" << std::endl;

    close(fd);
}

static ExitCode runMongoJSServer() {
    setThreadName( "mongojsMain" );

    int lfd;
    struct sockaddr_un saddr;
    int r;

    if (DEBUG) std::cout << "got here\n";

    lfd = socket(AF_UNIX, SOCK_STREAM, 0);

    memset(&saddr, 0, sizeof(saddr));

    saddr.sun_family = AF_UNIX;
    char path[] = "\0/mongodb/js.sock";
    std::memcpy(saddr.sun_path, path, sizeof(path));

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
