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

bool my_recv(int fd, char* buf, size_t len) {
    while (len) {
        ssize_t r = recv(fd, buf, len, 0);

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

static void work(int fd) {
    char buf[10000];
    uint32_t len;

    for (;;) {
        ConstDataRange cdr(buf, buf + sizeof(buf));

        if (! my_recv(fd, buf, 4)) break;

        len = uassertStatusOK(cdr.read<LittleEndian<uint32_t>>());

        if (! my_recv(fd, buf + 4, len - 4)) break;

        BSONObj out = uassertStatusOK(cdr.read<BSONObj>());
        std::cout << "IN: " << out << std::endl;

        std::unique_ptr<Scope> scope(globalScriptEngine->newScope());

        if (out.hasField("scope")) {
            for (auto elem : out["scope"].Obj()) {
                scope->setElement(elem.fieldName(), elem);
            }
        }

        auto func = scope->createFunction(out["code"].String().c_str());

        BSONObj doc = out["document"].Obj();

        auto err = scope->invoke(func, 0, &doc, 1000 * 60, false);

        BSONObjBuilder bob;

        bob.appendNumber("err", err);
        scope->append(bob, "rval", "__returnValue");

        auto rval = bob.obj();

        std::cout << "OUT: " << rval << std::endl;

        if (! my_send(fd, rval.objdata(), rval.objsize())) break;
    }

    close(fd);
}

static ExitCode runMongoJSServer() {
    setThreadName( "mongojsMain" );

    int optval;
    int lfd;
    struct sockaddr_in saddr;
    int r;

    std::cout << "got here\n";

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
    std::cout << status << std::endl;
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
