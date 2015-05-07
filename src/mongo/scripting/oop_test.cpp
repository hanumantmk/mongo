#include "mongo/platform/basic.h"

#include <string>

#include <cstdint>
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
#include "mongo/scripting/engine_v8-sm.h"

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

static ExitCode runMongoJSServer() {
    setThreadName( "mongojsMain" );

    ScriptEngine::setup();

    std::unique_ptr<Scope> scope(static_cast<Scope*>(globalScriptEngine->newScope()));

    scope->setNumber("foo", 10);

    std::cout << scope->getNumber("foo") << std::endl;

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

#undef exit
