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

#include "mongo/scripting/engine_oop_v8-4.1.h"

#include <iostream>

#include "mongo/base/init.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/scripting/oop_v8-4.1_db.h"
#include "mongo/scripting/oop_v8-4.1_utils.h"
#include "mongo/util/base64.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

using namespace mongoutils;

namespace mongo {

    using std::cout;
    using std::endl;
    using std::map;
    using std::string;
    using std::stringstream;

#ifndef _MSC_EXTENSIONS
    const int V8Scope::objectDepthLimit;
#endif

    // Generated symbols for JS files
    namespace JSFiles {
        extern const JSFile types;
        extern const JSFile assert;
    }

     V8ScriptEngine::V8ScriptEngine() :
         _globalInterruptLock(),
         _opToScopeMap(),
         _deadlineMonitor() {
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

     void V8ScriptEngine::interrupt(unsigned opId) {
         boost::lock_guard<boost::mutex> intLock(_globalInterruptLock);
         OpIdToScopeMap::iterator iScope = _opToScopeMap.find(opId);
         if (iScope == _opToScopeMap.end()) {
             // got interrupt request for a scope that no longer exists
             LOG(1) << "received interrupt request for unknown op: " << opId
                    << printKnownOps_inlock() << endl;
             return;
         }
         LOG(1) << "interrupting op: " << opId << printKnownOps_inlock() << endl;
         iScope->second->kill();
     }

     void V8ScriptEngine::interruptAll() {
         boost::lock_guard<boost::mutex> interruptLock(_globalInterruptLock);
         for (OpIdToScopeMap::iterator iScope = _opToScopeMap.begin();
              iScope != _opToScopeMap.end(); ++iScope) {
             iScope->second->kill();
         }
     }

     void V8Scope::registerOperation(OperationContext* txn) {
         boost::lock_guard<boost::mutex> giLock(_engine->_globalInterruptLock);
         invariant(_opId == 0);
         _opId = txn->getOpID();
         _engine->_opToScopeMap[_opId] = this;
         LOG(2) << "V8Scope " << static_cast<const void*>(this) << " registered for op " << _opId;
         Status status = txn->checkForInterruptNoAssert();
         if (!status.isOK()) {
             kill();
         }
     }

     void V8Scope::unregisterOperation() {
         boost::lock_guard<boost::mutex> giLock(_engine->_globalInterruptLock);
         LOG(2) << "V8Scope " << static_cast<const void*>(this) << " unregistered for op "
                << _opId << endl;
        if (_opId != 0) {
            // scope is currently associated with an operation id
            V8ScriptEngine::OpIdToScopeMap::iterator it = _engine->_opToScopeMap.find(_opId);
            if (it != _engine->_opToScopeMap.end())
                _engine->_opToScopeMap.erase(it);
            _opId = 0;
        }
    }

    void V8Scope::kill() {
        // TODO ...
    }

    /** check if there is a pending killOp request */
    bool V8Scope::isKillPending() const {
        return _pendingKill;
    }
    
    OperationContext* V8Scope::getOpContext() const {
        return _opCtx;
    }

    /**
     * Display a list of all known ops (for verbose output)
     */
    std::string V8ScriptEngine::printKnownOps_inlock() {
        stringstream out;
        if (shouldLog(logger::LogSeverity::Debug(2))) {
            out << "  known ops: " << endl;
            for(OpIdToScopeMap::iterator iSc = _opToScopeMap.begin();
                iSc != _opToScopeMap.end(); ++iSc) {
                out << "  " << iSc->first << endl;
            }
        }
        return out.str();
    }


    V8Scope::V8Scope(V8ScriptEngine * engine)
        : _engine(engine),
          _connectState(NOT),
          _interruptLock(),
          _inNativeExecution(true),
          _pendingKill(false),
          _opId(0),
          _opCtx(NULL) 
    {
    }

    V8Scope::~V8Scope() {
        unregisterOperation();
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
    }

    void V8Scope::setString(const char * field, StringData val) {
    }

    void V8Scope::setBoolean(const char * field, bool val) {
    }

    void V8Scope::setElement(const char *field, const BSONElement& e) {
    }

    void V8Scope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
    }

    int V8Scope::type(const char *field) {
        // uasserted(12509, str::stream() << "unable to get type of field " << field);
        return 0; // TODO ...
    }

    double V8Scope::getNumber(const char *field) {
        return 0; // TODO ...
    }

    int V8Scope::getNumberInt(const char *field) {
        return 0; // TODO ...
    }

    long long V8Scope::getNumberLongLong(const char *field) {
        return 0; // TODO ...
    }

    string V8Scope::getString(const char *field) {
        return ""; // TODO ...
    }

    bool V8Scope::getBoolean(const char *field) {
        return false; // TODO ...
    }

    BSONObj V8Scope::getObject(const char * field) {
        // uassert(10231,  "not an object", v->IsObject());
        return BSONObj(); // TODO ...
    }

    ScriptingFunction V8Scope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        // uassert(10232, "not a function", ret->IsFunction());
        return functionNumber;
    }

    void V8Scope::setFunction(const char* field, const char* code) {
        // TODO ...
    }

    void V8Scope::rename(const char * from, const char * to) {
        // TODO ...
    }

    int V8Scope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        // TODO ...

        return 0;
    }

    bool V8Scope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        // TODO ...

        return true;
    }

    void V8Scope::injectNative(const char *field, NativeFunction func, void* data) {
        // TODO ...
    }

    void V8Scope::gc() {
        // TODO ...
    }

    void V8Scope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        // TODO ...
    }

    void V8Scope::externalSetup() {
        // TODO ...
    }

    // ----- internal -----

    void V8Scope::reset() {
        // TODO ...
    }

    // --- random utils ----

    static logger::MessageLogDomain* jsPrintLogDomain;

    MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
        jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
        return Status::OK();
    }

} // namespace mongo
