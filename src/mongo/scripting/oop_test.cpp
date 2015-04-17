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

namespace mongo {

    using std::string;

    string mongojsCommand;
    bool dbexitCalled = false;

    bool inShutdown() {
        return dbexitCalled;
    }

    bool haveLocalShardingInfo( const string& ns ) {
        verify( 0 );
        return false;
    }

    static BSONObj buildErrReply( const DBException& ex ) {
        BSONObjBuilder errB;
        errB.append( "$err", ex.what() );
        errB.append( "code", ex.getCode() );
        if ( !ex._shard.empty() ) {
            errB.append( "shard", ex._shard );
        }
        return errB.obj();
    }

    DBClientBase* createDirectClient(OperationContext* txn) {
        return 0;
    }

} // namespace mongo

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

static void work(int fd) {
    close(fd);

    std::cout << "this is working at all" << std::endl;
}

static ExitCode runMongoJSServer() {
    setThreadName( "mongojsMain" );

    int cfd;
    struct sockaddr_in saddr;
    int r;

    cfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&saddr, 0, sizeof(saddr));

    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(40001);
    saddr.sin_addr.s_addr = htonl(INADDR_ANY);

    r = connect(cfd, reinterpret_cast<struct sockaddr*>(&saddr), sizeof(saddr));

    {
        BSONObjBuilder bob;

        bob.append("code", "function(){ return this.a == 1;}");
        bob.append("document", BSON("a" << 1));

        BSONObj b = bob.obj();

        my_send(cfd, b.objdata(), b.objsize());

        char buf[1000000];
        my_recv(cfd, buf, 4);

        ConstDataRange cdr(buf, buf + sizeof(buf));

        uint32_t len = uassertStatusOK(cdr.read<LittleEndian<uint32_t>>());
        my_recv(cfd, buf + 4, len - 4);

        b = uassertStatusOK(cdr.read<BSONObj>());

        std::cout << b << std::endl;
    }

    {
        BSONObjBuilder bob;

        bob.append("code", "function(){ return this.a == 1;}");
        bob.append("document", BSON("b" << 1));

        BSONObj b = bob.obj();

        my_send(cfd, b.objdata(), b.objsize());

        char buf[1000000];
        my_recv(cfd, buf, 4);

        ConstDataRange cdr(buf, buf + sizeof(buf));

        uint32_t len = uassertStatusOK(cdr.read<LittleEndian<uint32_t>>());
        my_recv(cfd, buf + 4, len - 4);

        b = uassertStatusOK(cdr.read<BSONObj>());

        std::cout << b << std::endl;
    }

    close (cfd);

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

    mongojsCommand = argv[0];

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

void mongo::signalShutdown() {
    // Notify all threads shutdown has started
    dbexitCalled = true;
}

void mongo::exitCleanly(ExitCode code) {
    // TODO: do we need to add anything?
    mongo::dbexit( code );
}

void mongo::dbexit( ExitCode rc, const char *why ) {
    dbexitCalled = true;

#if defined(_WIN32)
    // Windows Service Controller wants to be told when we are done shutting down
    // and call quickExit itself.
    //
    if ( rc == EXIT_WINDOWS_SERVICE_STOP ) {
        log() << "dbexit: exiting because Windows service was stopped" << endl;
        return;
    }
#endif
    quickExit(rc);
}
