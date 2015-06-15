/*    Copyright 2015 MongoDB Inc.
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

#include "mongo/scripting/mozjs/implscope.h"

#include "jscustomallocator.h"
#include "jsfriendapi.h"

#include "mongo/base/error_codes.h"
#include "mongo/db/operation_context.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/scripting/mozjs/valuereader.h"
#include "mongo/scripting/mozjs/valuewriter.h"
#include "mongo/util/log.h"

using namespace mongoutils;

namespace mongo {

// Generated symbols for JS files
namespace JSFiles {
extern const JSFile types;
extern const JSFile assert;
}  // namespace

namespace mozjs {

void ImplScope::_reportError(JSContext* cx, const char* message, JSErrorReport* report) {
    auto scope = getScope(cx);

    if (!JSREPORT_IS_WARNING(report->flags)) {
        // TODO not cerr
        std::cerr << JS::FormatStackDump(cx, nullptr, true, true, false) << std::endl;

        scope->_status = Status(ErrorCodes::InternalError, message);
    }
}

std::string ImplScope::getError() {
    return "";
}

void ImplScope::registerOperation(OperationContext* txn) {
    boost::lock_guard<boost::mutex> giLock(_engine->_globalInterruptLock);

    invariant(_opId == 0);
    _opId = txn->getOpID();
    _engine->_opToScopeMap[_opId] = this;
    LOG(2) << "MSScope " << static_cast<const void*>(this) << " registered for op " << _opId;
    Status status = txn->checkForInterruptNoAssert();
    if (!status.isOK()) {
        kill();
    }
}

void ImplScope::unregisterOperation() {
    boost::lock_guard<boost::mutex> giLock(_engine->_globalInterruptLock);
    LOG(2) << "ImplScope " << static_cast<const void*>(this) << " unregistered for op " << _opId
           << std::endl;
    if (_opId != 0) {
        // scope is currently associated with an operation id
        ScriptEngine::OpIdToScopeMap::iterator it = _engine->_opToScopeMap.find(_opId);
        if (it != _engine->_opToScopeMap.end())
            _engine->_opToScopeMap.erase(it);
        _opId = 0;
    }
}

void ImplScope::kill() {
    _pendingKill.store(true);
    JS_RequestInterruptCallback(_runtime);
}

/** check if there is a pending killOp request */
bool ImplScope::isKillPending() const {
    return _pendingKill.load();
}

OperationContext* ImplScope::getOpContext() const {
    return _opCtx;
}

bool ImplScope::_interruptCallback(JSContext* cx) {
    auto scope = getScope(cx);

    if (scope->_pendingGC.load()) {
        JS_GC(scope->_runtime);
    }

    bool kill = scope->isKillPending();

    std::cerr << "in interrupt callback"
              << " js_total_bytes(" << mongo::sm::get_total_bytes() << ")"
                                                                       " js_max_bytes("
              << mongo::sm::get_max_bytes() << ")"
                                               " interrupt(" << scope->isKillPending() << ")"
                                                                                          " kill?("
              << kill << ")" << std::endl;

    if (kill) {
        scope->_engine->getDeadlineMonitor()->stopDeadline(scope);
        scope->unregisterOperation();
    }

    return !kill;
}

void ImplScope::_gcCallback(JSRuntime* rt, JSGCStatus status, void* data) {
    if (!shouldLog(logger::LogSeverity::Debug(1))) {
         // don't collect stats unless verbose
         return;
    }

    log() << "MozJS GC " << (status == JSGC_BEGIN ? "prologue" : "epilogue")
          << " heap stats - "
          << " total: " << mongo::sm::get_total_bytes()
          << " limit: " << mongo::sm::get_max_bytes()
          << std::endl;
}

ImplScope::MozRuntime::MozRuntime() {
    // Set a 1gb limit per runtime
    mongo::sm::reset(1024 * 1024 * 1024);

    _runtime = JS_NewRuntime(8L * 1024 * 1024);
    _context = JS_NewContext(_runtime, 8192);
}

ImplScope::MozRuntime::~MozRuntime() {
    JS_DestroyContext(_context);
    JS_DestroyRuntime(_runtime);
}

ImplScope::ImplScope(ScriptEngine* engine)
    : _engine(engine),
      _mr(),
      _runtime(_mr._runtime),
      _context(_mr._context),
      _globalProto(_context),
      _global(_globalProto.proto()),
      _funcs(),
      _pendingKill(false),
      _opId(0),
      _opCtx(nullptr),
      _pendingGC(false),
      _connectState(NOT),
      _status(ErrorCodes::InternalError, "Unknown MozJS error"),
      _binDataProto(_context),
      _bsonProto(_context),
      _cursorProto(_context),
      _dbCollectionProto(_context),
      _dbPointerProto(_context),
      _dbQueryProto(_context),
      _dbProto(_context),
      _dbRefProto(_context),
      _maxKeyProto(_context),
      _minKeyProto(_context),
      _mongoExternalProto(_context),
      _mongoLocalProto(_context),
      _nativeFunctionProto(_context),
      _numberIntProto(_context),
      _numberLongProto(_context),
      _objectProto(_context),
      _oidProto(_context),
      _regExpProto(_context),
      _timestampProto(_context) {
    // The default is quite low and doesn't seem to directly correlate with
    // malloc'd bytes.  Set it to MAX_INT here and catching things in the
    // jscustomallocator.cpp
    JS_SetGCParameter(_runtime, JSGC_MAX_BYTES, 0xffffffff);

    JS_SetInterruptCallback(_runtime, _interruptCallback);
    JS_SetGCCallback(_runtime, _gcCallback, this);
    JS_SetContextPrivate(_context, this);
    JSAutoRequest ar(_context);

    JS_SetErrorReporter(_runtime, _reportError);

    JSAutoCompartment ac(_context, _global);

    _checkErrorState(JS_InitStandardClasses(_context, _global));

    installBSONTypes();
    execSetup(JSFiles::assert);
    execSetup(JSFiles::types);

    // install process-specific utilities in the global scope (dependancy: types.js, assert.js)
    if (_engine->_scopeInitCallback)
        _engine->_scopeInitCallback(*this);

    // install global utility functions
    installGlobalUtils(*this);
}

ImplScope::~ImplScope() {
    for (auto&& x : _funcs) {
        x.reset();
    }

    unregisterOperation();
}

bool ImplScope::hasOutOfMemoryException() {
    return false;
}

void ImplScope::init(const BSONObj* data) {
    if (!data)
        return;

    BSONObjIterator i(*data);
    while (i.more()) {
        BSONElement e = i.next();
        setElement(e.fieldName(), e);
    }
}

#define SMMAGIC_HEADER          \
    JSAutoRequest ar(_context); \
    JSAutoCompartment ac(_context, _global)

void ImplScope::setNumber(const char* field, double val) {
    SMMAGIC_HEADER;

    ObjectWrapper(_context, _global).setNumber(field, val);
}

void ImplScope::setString(const char* field, StringData val) {
    SMMAGIC_HEADER;

    ObjectWrapper(_context, _global).setString(field, val);
}

void ImplScope::setBoolean(const char* field, bool val) {
    SMMAGIC_HEADER;

    ObjectWrapper(_context, _global).setBoolean(field, val);
}

void ImplScope::setElement(const char* field, const BSONElement& e) {
    SMMAGIC_HEADER;

    ObjectWrapper(_context, _global).setBSONElement(field, e, false);
}

void ImplScope::setObject(const char* field, const BSONObj& obj, bool readOnly) {
    SMMAGIC_HEADER;

    ObjectWrapper(_context, _global).setBSON(field, obj, false);
}

int ImplScope::type(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).type(field);
}

double ImplScope::getNumber(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).getNumber(field);
}

int ImplScope::getNumberInt(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).getNumberInt(field);
}

long long ImplScope::getNumberLongLong(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).getNumberLongLong(field);
}

std::string ImplScope::getString(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).getString(field);
}

bool ImplScope::getBoolean(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).getBoolean(field);
}

BSONObj ImplScope::getObject(const char* field) {
    SMMAGIC_HEADER;

    return ObjectWrapper(_context, _global).getObject(field);
}

void ImplScope::newFunction(StringData raw, JS::MutableHandleValue out) {
    SMMAGIC_HEADER;

    std::string code = str::stream() << "____MongoToSM_newFunction_temp = " << raw;

    JS::CompileOptions co(_context);

    _checkErrorState(JS::Evaluate(_context, _global, co, code.c_str(), code.length(), out));
}


bool hasFunctionIdentifier(StringData code) {
    if (code.size() < 9 || code.find("function") != 0)
        return false;

    return code[8] == ' ' || code[8] == '(';
}

void ImplScope::__createFunction(const char* raw,
                                 ScriptingFunction functionNumber,
                                 JS::MutableHandleValue fun) {
    raw = jsSkipWhiteSpace(raw);
    std::string code = raw;
    if (!hasFunctionIdentifier(code)) {
        if (code.find('\n') == std::string::npos && !hasJSReturn(code) &&
            (code.find(';') == std::string::npos || code.find(';') == code.size() - 1)) {
            code = "return " + code;
        }
        code = "function(){ " + code + "}";
    }

    std::string fn = str::stream() << "_funcs" << functionNumber;
    code = str::stream() << "_funcs" << functionNumber << " = " << code;

    JS::CompileOptions co(_context);

    _checkErrorState(JS::Evaluate(_context, _global, co, code.c_str(), code.length(), fun));
    uassert(10232,
            "not a function",
            fun.isObject() && JS_ObjectIsFunction(_context, fun.toObjectOrNull()));
}

ScriptingFunction ImplScope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
    SMMAGIC_HEADER;

    JS::RootedValue fun(_context);

    __createFunction(raw, functionNumber, &fun);

    _funcs.emplace_back(_context, fun.get());

    return functionNumber;
}

void ImplScope::setFunction(const char* field, const char* code) {
    SMMAGIC_HEADER;

    JS::RootedValue fun(_context);

    __createFunction(code, getFunctionCache().size() + 1, &fun);

    ObjectWrapper(_context, _global).setValue(field, fun);
}

void ImplScope::rename(const char* from, const char* to) {
    SMMAGIC_HEADER;

    ObjectWrapper(_context, _global).rename(from, to);
}

int ImplScope::invoke(ScriptingFunction func,
                      const BSONObj* argsObject,
                      const BSONObj* recv,
                      int timeoutMs,
                      bool ignoreReturn,
                      bool readOnlyArgs,
                      bool readOnlyRecv) {
    SMMAGIC_HEADER;

    auto funcValue = _funcs[func - 1];
    JS::RootedValue result(_context);

    const int nargs = argsObject ? argsObject->nFields() : 0;

    JS::AutoValueVector args(_context);

    if (nargs) {
        BSONObjIterator it(*argsObject);
        for (int i = 0; i < nargs; i++) {
            BSONElement next = it.next();

            JS::RootedValue value(_context);

            ValueReader(_context, &value).fromBSONElement(next, readOnlyArgs);

            args.append(value);
        }
    }

    JS::RootedValue smrecv(_context);
    if (recv)
        ValueReader(_context, &smrecv).fromBSON(*recv, readOnlyRecv);
    else
        smrecv.setObjectOrNull(_global);

    if (timeoutMs)
        _engine->getDeadlineMonitor()->startDeadline(this, timeoutMs);

    JS::RootedValue out(_context);
    JS::RootedObject obj(_context, smrecv.toObjectOrNull());

    JS_MaybeGC(_context);

    bool success = JS::Call(_context, obj, funcValue, args, &out);

    if (timeoutMs)
        _engine->getDeadlineMonitor()->stopDeadline(this);

    _checkErrorState(success);

    if (!ignoreReturn) {
        // must validate the handle because TerminateExecution may have
        // been thrown after the above checks
        if (out.isObject() && nativeFunctionProto().instanceOf(out)) {
            log() << "storing native function as return value" << std::endl;
            _lastRetIsNativeCode = true;
        } else {
            _lastRetIsNativeCode = false;
        }

        ObjectWrapper(_context, _global).setValue("__returnValue", out);
    }

    return 0;
}

bool ImplScope::exec(StringData code,
                     const std::string& name,
                     bool printResult,
                     bool reportError,
                     bool assertOnError,
                     int timeoutMs) {
    SMMAGIC_HEADER;

    JS::CompileOptions co(_context);
    JS::RootedScript script(_context);

    bool success;

    success = JS::Compile(_context, _global, co, code.rawData(), code.size(), &script);

    if (_checkErrorState(success, reportError, assertOnError))
        return false;

    if (timeoutMs)
        _engine->getDeadlineMonitor()->startDeadline(this, timeoutMs);

    JS::RootedValue out(_context);

    success = JS_ExecuteScript(_context, _global, script, &out);

    if (timeoutMs)
        _engine->getDeadlineMonitor()->stopDeadline(this);

    if (_checkErrorState(success, reportError, assertOnError))
        return false;

    ObjectWrapper(_context, _global).setValue("__lastres__", out);

    if (printResult && !out.isUndefined()) {
        // appears to only be used by shell
        // std::cout << ValueWriter(_context, out).toString() << std::endl;
    }

    return true;
}

void ImplScope::injectNative(const char* field, NativeFunction func, void* data) {
    SMMAGIC_HEADER;

    JS::RootedObject obj(_context);

    NativeFunctionInfo::make(_context, &obj, func, data);

    JS::RootedValue value(_context);
    value.setObjectOrNull(obj);
    ObjectWrapper(_context, _global).setValue(field, value);
}

void ImplScope::gc() {
    _pendingGC.store(true);
    JS_RequestInterruptCallback(_runtime);
}

void ImplScope::localConnectForDbEval(OperationContext* txn, const char* dbName) {
    SMMAGIC_HEADER;

    invariant(_opCtx == NULL);
    _opCtx = txn;

    if (_connectState == EXTERNAL)
        uasserted(12510, "externalSetup already called, can't call localConnect");
    if (_connectState == LOCAL) {
        if (_localDBName == dbName)
            return;
        uasserted(12511,
                  str::stream() << "localConnect previously called with name " << _localDBName);
    }

    // NOTE: order is important here.  the following methods must be called after
    //       the above conditional statements.

    // install db access functions in the global object
    installDBAccess();

    // install the Mongo function object and instantiate the 'db' global
    _mongoLocalProto.install(_global);
    execCoreFiles();
    exec("_mongo = new Mongo();", "local connect 2", false, true, true, 0);
    exec((std::string) "db = _mongo.getDB(\"" + dbName + "\");",
         "local connect 3",
         false,
         true,
         true,
         0);
    _connectState = LOCAL;
    _localDBName = dbName;

    loadStored(txn);
}

void ImplScope::externalSetup() {
    SMMAGIC_HEADER;

    if (_connectState == EXTERNAL)
        return;
    if (_connectState == LOCAL)
        uasserted(12512, "localConnect already called, can't call externalSetup");

    // install db access functions in the global object
    installDBAccess();

    // install thread-related functions (e.g. _threadInject)
    // installFork(this, _global, _context);

    // install the Mongo function object
    _mongoExternalProto.install(_global);
    execCoreFiles();
    _connectState = EXTERNAL;
}

// ----- internal -----

void ImplScope::reset() {
    unregisterOperation();
    _pendingKill.store(false);
    _pendingGC.store(false);
}

void ImplScope::installBSONTypes() {
    _binDataProto.install(_global);
    _bsonProto.install(_global);
    _dbPointerProto.install(_global);
    _dbRefProto.install(_global);
    _maxKeyProto.install(_global);
    _minKeyProto.install(_global);
    _nativeFunctionProto.install(_global);
    _numberIntProto.install(_global);
    _numberLongProto.install(_global);
    _objectProto.install(_global);
    _oidProto.install(_global);
    _regExpProto.install(_global);
    _timestampProto.install(_global);

    // This builtin map is a javascript 6 thing.  We want our version.  so
    // take there's out
    ObjectWrapper(_context, _global).deleteProperty("Map");
}

void ImplScope::installDBAccess() {
    _cursorProto.install(_global);
    _dbProto.install(_global);
    _dbQueryProto.install(_global);
    _dbCollectionProto.install(_global);
}

bool ImplScope::_checkErrorState(bool success, bool reportError, bool assertOnError) {
    bool haveError = false;

    if (!success) {
        _error = _status.reason();
        haveError = true;
    }

    if (haveError) {
        if (reportError)
            error() << _error << std::endl;
        if (assertOnError) {
            auto status = std::move(_status);
            uassertStatusOK(status);
        }

        return true;
    }

    return false;
}

}  // namespace mozjs
}  // namespace mongo
