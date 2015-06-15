/*    Copyright 2015 MongoDB Inc.
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
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

#include "mongo/client/dbclientcursor.h"
#include "mongo/scripting/mozjs/bindata.h"
#include "mongo/scripting/mozjs/bson.h"
#include "mongo/scripting/mozjs/cursor.h"
#include "mongo/scripting/mozjs/dbcollection.h"
#include "mongo/scripting/mozjs/db.h"
#include "mongo/scripting/mozjs/dbpointer.h"
#include "mongo/scripting/mozjs/dbquery.h"
#include "mongo/scripting/mozjs/dbref.h"
#include "mongo/scripting/mozjs/engine.h"
#include "mongo/scripting/mozjs/global.h"
#include "mongo/scripting/mozjs/maxkey.h"
#include "mongo/scripting/mozjs/minkey.h"
#include "mongo/scripting/mozjs/mongo.h"
#include "mongo/scripting/mozjs/nativefunction.h"
#include "mongo/scripting/mozjs/numberint.h"
#include "mongo/scripting/mozjs/numberlong.h"
#include "mongo/scripting/mozjs/object.h"
#include "mongo/scripting/mozjs/oid.h"
#include "mongo/scripting/mozjs/regexp.h"
#include "mongo/scripting/mozjs/timestamp.h"

#include "jsapi.h"

namespace mongo {
namespace mozjs {

class ImplScope : public Scope {
public:
    ImplScope(ScriptEngine* engine);
    ~ImplScope();

    virtual void init(const BSONObj* data);

    virtual void reset();

    virtual void kill();

    bool isKillPending() const;

    OperationContext* getOpContext() const;

    virtual void registerOperation(OperationContext* txn);

    virtual void unregisterOperation();

    virtual void localConnectForDbEval(OperationContext* txn, const char* dbName);

    virtual void externalSetup();

    virtual std::string getError();

    virtual bool hasOutOfMemoryException();

    void gc();

    virtual double getNumber(const char* field);
    virtual int getNumberInt(const char* field);
    virtual long long getNumberLongLong(const char* field);
    virtual std::string getString(const char* field);
    virtual bool getBoolean(const char* field);
    virtual BSONObj getObject(const char* field);

    virtual void setNumber(const char* field, double val);
    virtual void setString(const char* field, StringData val);
    virtual void setBoolean(const char* field, bool val);
    virtual void setElement(const char* field, const BSONElement& e);
    virtual void setObject(const char* field, const BSONObj& obj, bool readOnly);
    virtual void setFunction(const char* field, const char* code);

    virtual int type(const char* field);

    virtual void rename(const char* from, const char* to);

    virtual int invoke(ScriptingFunction func,
                       const BSONObj* args,
                       const BSONObj* recv,
                       int timeoutMs = 0,
                       bool ignoreReturn = false,
                       bool readOnlyArgs = false,
                       bool readOnlyRecv = false);

    virtual bool exec(StringData code,
                      const std::string& name,
                      bool printResult,
                      bool reportError,
                      bool assertOnError,
                      int timeoutMs);

    virtual void injectNative(const char* field, NativeFunction func, void* data = 0);

    virtual ScriptingFunction _createFunction(const char* code,
                                              ScriptingFunction functionNumber = 0);
    void newFunction(StringData code, JS::MutableHandleValue out);

    WrapType<BinDataInfo>& binDataProto() {
        return _binDataProto;
    }
    WrapType<BSONInfo>& bsonProto() {
        return _bsonProto;
    }
    WrapType<CursorInfo>& cursorProto() {
        return _cursorProto;
    }
    WrapType<DBCollectionInfo>& dbCollectionProto() {
        return _dbCollectionProto;
    }
    WrapType<DBPointerInfo>& dbPointerProto() {
        return _dbPointerProto;
    }
    WrapType<DBQueryInfo>& dbQueryProto() {
        return _dbQueryProto;
    }
    WrapType<DBInfo>& dbProto() {
        return _dbProto;
    }
    WrapType<DBRefInfo>& dbRefProto() {
        return _dbRefProto;
    }
    WrapType<MaxKeyInfo>& maxKeyProto() {
        return _maxKeyProto;
    }
    WrapType<MinKeyInfo>& minKeyProto() {
        return _minKeyProto;
    }
    WrapType<MongoExternalInfo>& mongoExternalProto() {
        return _mongoExternalProto;
    }
    WrapType<MongoLocalInfo>& mongoLocalProto() {
        return _mongoLocalProto;
    }
    WrapType<NativeFunctionInfo>& nativeFunctionProto() {
        return _nativeFunctionProto;
    }
    WrapType<NumberIntInfo>& numberIntProto() {
        return _numberIntProto;
    }
    WrapType<NumberLongInfo>& numberLongProto() {
        return _numberLongProto;
    }
    WrapType<ObjectInfo>& objectProto() {
        return _objectProto;
    }
    WrapType<OIDInfo>& oidProto() {
        return _oidProto;
    }
    WrapType<RegExpInfo>& regExpProto() {
        return _regExpProto;
    }
    WrapType<TimestampInfo>& timestampProto() {
        return _timestampProto;
    }

private:
    void __createFunction(const char* raw,
                          ScriptingFunction functionNumber,
                          JS::MutableHandleValue fun);

    class MozRuntime {
    public:
        MozRuntime();
        ~MozRuntime();

        JSRuntime* _runtime;
        JSContext* _context;
    };
    static void _reportError(JSContext* cx, const char* message, JSErrorReport* report);
    static bool _interruptCallback(JSContext* cx);
    static void _gcCallback(JSRuntime* rt, JSGCStatus status, void* data);
    bool _checkErrorState(bool success, bool reportError = true, bool assertOnError = true);

    void installDBAccess();
    void installBSONTypes();

    ScriptEngine* _engine;
    MozRuntime _mr;
    JSRuntime* _runtime;
    JSContext* _context;
    WrapType<GlobalInfo> _globalProto;
    JS::HandleObject _global;
    std::vector<JS::PersistentRootedValue> _funcs;
    std::atomic_bool _pendingKill;
    std::string _error;
    unsigned int _opId;        // op id for this scope
    OperationContext* _opCtx;  // Op context for DbEval
    std::atomic_bool _pendingGC;
    enum ConnectState { NOT, LOCAL, EXTERNAL };
    ConnectState _connectState;
    Status _status;

    WrapType<BinDataInfo> _binDataProto;
    WrapType<BSONInfo> _bsonProto;
    WrapType<CursorInfo> _cursorProto;
    WrapType<DBCollectionInfo> _dbCollectionProto;
    WrapType<DBPointerInfo> _dbPointerProto;
    WrapType<DBQueryInfo> _dbQueryProto;
    WrapType<DBInfo> _dbProto;
    WrapType<DBRefInfo> _dbRefProto;
    WrapType<MaxKeyInfo> _maxKeyProto;
    WrapType<MinKeyInfo> _minKeyProto;
    WrapType<MongoExternalInfo> _mongoExternalProto;
    WrapType<MongoLocalInfo> _mongoLocalProto;
    WrapType<NativeFunctionInfo> _nativeFunctionProto;
    WrapType<NumberIntInfo> _numberIntProto;
    WrapType<NumberLongInfo> _numberLongProto;
    WrapType<ObjectInfo> _objectProto;
    WrapType<OIDInfo> _oidProto;
    WrapType<RegExpInfo> _regExpProto;
    WrapType<TimestampInfo> _timestampProto;
};

inline ImplScope* getScope(JSContext* cx) {
    return static_cast<ImplScope*>(JS_GetContextPrivate(cx));
}

}  // namespace mozjs
}  // namespace mongo
