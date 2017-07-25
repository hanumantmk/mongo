var _session_api_module = (function() {

    var ServerSession =
        function(client) {
        var res = client.adminCommand({startSession: 1});

        if (!res.ok) {
            throw _getErrorWithCode(res, "failed to startSession: " + tojson(res));
        }

        this._id = res.id;
        this._timeoutMinutes = res.timeoutMinutes;
        this._lastUsed = new Date();
    }

        ServerSession.prototype.updateLastUsed = function() {
        this._lastUsed = new Date();
    };

    var DriverSession = function(client, opts) {
        this._serverSession = new ServerSession(client);
        this._client = client;
        this._options = opts;
        this._hasEnded = false;
    };

    DriverSession.prototype.getClient = function() {
        return this._client;
    };

    DriverSession.prototype.getOptions = function() {
        return this._options;
    };

    DriverSession.prototype.hasEnded = function() {
        return this._hasEnded;
    };

    DriverSession.prototype.endSession = function() {
        this._client.adminCommand({endSessions: 1, ids: [this._id]});
        this._hasEnded = true;
    };

    DriverSession.prototype.getDatabase = function(db) {
        var db = new DB(this._client, db);
        db._session = this;
        return db;
    };

    var SessionOptions = function() {};

    SessionOptions.prototype.getReadPreference = function() {
        return this._readPreference;
    };

    SessionOptions.prototype.setReadPreference = function(readPreference) {
        return this._readPreference = readPreference;
    };

    SessionOptions.prototype.getReadConcern = function() {
        return this._readConcern;
    };

    SessionOptions.prototype.setReadConcern = function(readConcern) {
        return this._readConcern = readConcern;
    };

    SessionOptions.prototype.getWriteConcern = function() {
        return this._writeConcern;
    };

    SessionOptions.prototype.setWriteConcern = function(writeConcern) {
        return this._writeConcern = writeConcern;
    };

    module = {};

    module.DriverSession = DriverSession;
    module.SessionOptions = SessionOptions;

    return module;
})();

// Globals
DriverSession = _session_api_module.DriverSession;
SessionOptions = _session_api_module.SessionOptions;
