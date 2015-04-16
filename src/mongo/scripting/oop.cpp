#include "mongo/platform/basic.h"

#include <string>

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

static ExitCode runMongoJSServer() {
    setThreadName( "mongojsMain" );


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

    Status status = mongo::runGlobalInitializers(argc, argv, envp);
    if (!status.isOK()) {
        quickExit(EXIT_FAILURE);
    }

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
