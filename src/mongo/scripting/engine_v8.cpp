//engine_v8.cpp

/*    Copyright 2009 10gen Inc.
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

#include "mongo/scripting/engine_v8.h"

#include <iostream>

#include "mongo/base/init.h"
#include "mongo/db/service_context.h"
#include "mongo/db/operation_context.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/scripting/v8_db.h"
#include "mongo/scripting/v8_utils.h"
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

    // The  unwrapXXX functions extract internal fields from an object wrapped by wrapBSONObject.
    // These functions are currently only used in places that should always have the correct
    // type of object, however it may be possible for users to come up with a way to make these
    // called with the wrong type so calling code should always check the returns.
    static BSONHolder* unwrapHolder(V8Scope* scope, const v8::Handle<v8::Object>& obj) {
        // Warning: can't throw exceptions in this context.
        if (!scope->LazyBsonFT()->HasInstance(obj))
            return NULL;

        v8::Handle<v8::External> field = v8::Handle<v8::External>::Cast(obj->GetInternalField(0));
        if (field.IsEmpty() || !field->IsExternal())
            return 0;
        void* ptr = field->Value();
        return (BSONHolder*)ptr;
    }

    static BSONObj unwrapBSONObj(V8Scope* scope, const v8::Handle<v8::Object>& obj) {
        // Warning: can't throw exceptions in this context.
        BSONHolder* holder = unwrapHolder(scope, obj);
        return holder ? holder->_obj : BSONObj();
    }

    static v8::Handle<v8::Object> unwrapObject(V8Scope* scope, const v8::Handle<v8::Object>& obj) {
        // Warning: can't throw exceptions in this context.
        if (!scope->LazyBsonFT()->HasInstance(obj))
            return v8::Handle<v8::Object>();

        return obj->GetInternalField(1).As<v8::Object>();
    }

    void V8Scope::wrapBSONObject(v8::Handle<v8::Object> obj, BSONObj data, bool readOnly) {
        verify(LazyBsonFT()->HasInstance(obj));

        // Nothing below throws
        BSONHolder* holder = new BSONHolder(data);
        holder->_readOnly = readOnly;
        holder->_scope = this;
        obj->SetInternalField(0, v8::External::New(holder)); // Holder
        obj->SetInternalField(1, v8::Object::New()); // Object
        v8::Persistent<v8::Object> p = v8::Persistent<v8::Object>::New(obj);
        bsonHolderTracker.track(p, holder);
    }

    static v8::Handle<v8::Value> namedGet(v8::Local<v8::String> name,
                                          const v8::AccessorInfo& info) {
        v8::HandleScope handle_scope;
        v8::Handle<v8::Value> val;
        try {
            V8Scope* scope = getScope(info.GetIsolate());
            v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
            if (realObject.IsEmpty()) return v8::Handle<v8::Value>();
            if (realObject->HasOwnProperty(name)) {
                // value already cached or added
                return handle_scope.Close(realObject->Get(name));
            }

            string key = toSTLString(name);
            BSONHolder* holder = unwrapHolder(scope, info.Holder());
            if (!holder || holder->_removed.count(key))
                return handle_scope.Close(v8::Handle<v8::Value>());

            BSONObj obj = holder->_obj;
            BSONElement elmt = obj.getField(key.c_str());
            if (elmt.eoo())
                return handle_scope.Close(v8::Handle<v8::Value>());

            val = scope->mongoToV8Element(elmt, holder->_readOnly);

            if (obj.objsize() > 128 || val->IsObject()) {
                // Only cache if expected to help (large BSON) or is required due to js semantics
                realObject->Set(name, val);
            }

            if (elmt.type() == mongo::Object || elmt.type() == mongo::Array) {
              // if accessing a subobject, it may get modified and base obj would not know
              // have to set base as modified, which means some optim is lost
              holder->_modified = true;
            }
        }
        catch (const DBException &dbEx) {
            return v8AssertionException(dbEx.toString());
        }
        catch (...) {
            return v8AssertionException(string("error getting property ") + toSTLString(name));
        }
        return handle_scope.Close(val);
    }

    static v8::Handle<v8::Value> namedGetRO(v8::Local<v8::String> name,
                                            const v8::AccessorInfo &info) {
        return namedGet(name, info);
    }

    static v8::Handle<v8::Value> namedSet(v8::Local<v8::String> name,
                                          v8::Local<v8::Value> value_obj,
                                          const v8::AccessorInfo& info) {
        string key = toSTLString(name);
        V8Scope* scope = getScope(info.GetIsolate());
        BSONHolder* holder = unwrapHolder(scope, info.Holder());
        if (!holder) return v8::Handle<v8::Value>();
        holder->_removed.erase(key);
        holder->_modified = true;

        v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
        if (realObject.IsEmpty()) return v8::Handle<v8::Value>();
        realObject->Set(name, value_obj);
        return value_obj;
    }

    static v8::Handle<v8::Array> namedEnumerator(const v8::AccessorInfo &info) {
        v8::HandleScope handle_scope;
        V8Scope* scope = getScope(info.GetIsolate());
        BSONHolder* holder = unwrapHolder(scope, info.Holder());
        if (!holder) return v8::Handle<v8::Array>();
        BSONObj obj = holder->_obj;
        v8::Handle<v8::Array> out = v8::Array::New();
        int outIndex = 0;

        unordered_set<StringData, StringData::Hasher> added;
        // note here that if keys are parseable number, v8 will access them using index
        for (BSONObjIterator it(obj); it.more();) {
            const BSONElement& f = it.next();
            StringData sname (f.fieldName(), f.fieldNameSize()-1);
            if (holder->_removed.count(sname.toString()))
                continue;

            v8::Handle<v8::String> name = scope->v8StringData(sname);
            added.insert(sname);
            out->Set(outIndex++, name);
        }


        v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
        if (realObject.IsEmpty()) return v8::Handle<v8::Array>();
        v8::Handle<v8::Array> fields = realObject->GetOwnPropertyNames();
        const int len = fields->Length();
        for (int field=0; field < len; field++) {
            v8::Handle<v8::String> name = fields->Get(field).As<v8::String>();
            V8String sname (name);
            if (added.count(sname))
                continue;
            out->Set(outIndex++, name);
        }
        return handle_scope.Close(out);
    }

    v8::Handle<v8::Boolean> namedDelete(v8::Local<v8::String> name, const v8::AccessorInfo& info) {
        v8::HandleScope handle_scope;
        string key = toSTLString(name);
        V8Scope* scope = getScope(info.GetIsolate());
        BSONHolder* holder = unwrapHolder(scope, info.Holder());
        if (!holder) return v8::Handle<v8::Boolean>();
        holder->_removed.insert(key);
        holder->_modified = true;

        v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
        if (realObject.IsEmpty()) return v8::Handle<v8::Boolean>();
        realObject->Delete(name);
        return v8::True();
    }

    static v8::Handle<v8::Value> indexedGet(uint32_t index, const v8::AccessorInfo &info) {
        v8::HandleScope handle_scope;
        v8::Handle<v8::Value> val;
        try {
            V8Scope* scope = getScope(info.GetIsolate());
            v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
            if (realObject.IsEmpty()) return v8::Handle<v8::Value>();
            if (realObject->Has(index)) {
                // value already cached or added
                return handle_scope.Close(realObject->Get(index));
            }
            string key = str::stream() << index;

            BSONHolder* holder = unwrapHolder(scope, info.Holder());
            if (!holder) return v8::Handle<v8::Value>();
            if (holder->_removed.count(key))
                return handle_scope.Close(v8::Handle<v8::Value>());

            BSONObj obj = holder->_obj;
            BSONElement elmt = obj.getField(key);
            if (elmt.eoo())
                return handle_scope.Close(v8::Handle<v8::Value>());
            val = scope->mongoToV8Element(elmt, holder->_readOnly);
            realObject->Set(index, val);

            if (elmt.type() == mongo::Object || elmt.type() == mongo::Array) {
                // if accessing a subobject, it may get modified and base obj would not know
                // have to set base as modified, which means some optim is lost
                holder->_modified = true;
            }
        }
        catch (const DBException &dbEx) {
            return v8AssertionException(dbEx.toString());
        }
        catch (...) {
            return v8AssertionException(str::stream() << "error getting indexed property "
                                                      << index);
        }
        return handle_scope.Close(val);
    }

    v8::Handle<v8::Boolean> indexedDelete(uint32_t index, const v8::AccessorInfo& info) {
        string key = str::stream() << index;
        V8Scope* scope = getScope(info.GetIsolate());
        BSONHolder* holder = unwrapHolder(scope, info.Holder());
        if (!holder) return v8::Handle<v8::Boolean>();
        holder->_removed.insert(key);
        holder->_modified = true;

        // also delete in JS obj
        v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
        if (realObject.IsEmpty()) return v8::Handle<v8::Boolean>();
        realObject->Delete(index);
        return v8::True();
    }

    static v8::Handle<v8::Value> indexedGetRO(uint32_t index, const v8::AccessorInfo &info) {
        return indexedGet(index, info);
    }

    static v8::Handle<v8::Value> indexedSet(uint32_t index, v8::Local<v8::Value> value_obj,
                                            const v8::AccessorInfo& info) {
        string key = str::stream() << index;
        V8Scope* scope = getScope(info.GetIsolate());
        BSONHolder* holder = unwrapHolder(scope, info.Holder());
        if (!holder) return v8::Handle<v8::Value>();
        holder->_removed.erase(key);
        holder->_modified = true;

        v8::Handle<v8::Object> realObject = unwrapObject(scope, info.Holder());
        if (realObject.IsEmpty()) return v8::Handle<v8::Value>();
        realObject->Set(index, value_obj);
        return value_obj;
    }

    v8::Handle<v8::Value> NamedReadOnlySet(v8::Local<v8::String> property,
                                           v8::Local<v8::Value> value,
                                           const v8::AccessorInfo& info) {
        cout << "cannot write property " << V8String(property) << " to read-only object" << endl;
        return value;
    }

    v8::Handle<v8::Boolean> NamedReadOnlyDelete(v8::Local<v8::String> property,
                                                const v8::AccessorInfo& info) {
        cout << "cannot delete property " << V8String(property) << " from read-only object" << endl;
        return v8::Boolean::New(false);
    }

    v8::Handle<v8::Value> IndexedReadOnlySet(uint32_t index, v8::Local<v8::Value> value,
                                             const v8::AccessorInfo& info) {
        cout << "cannot write property " << index << " to read-only array" << endl;
        return value;
    }

    v8::Handle<v8::Boolean> IndexedReadOnlyDelete(uint32_t index, const v8::AccessorInfo& info) {
        cout << "cannot delete property " << index << " from read-only array" << endl;
        return v8::Boolean::New(false);
    }

    /**
     * GC Prologue and Epilogue constants (used to display description constants)
     */
    struct GCPrologueState { static const char* name; };
    const char* GCPrologueState::name = "prologue";
    struct GCEpilogueState { static const char* name; };
    const char* GCEpilogueState::name = "epilogue";

    template <typename _GCState>
    void gcCallback(v8::GCType type, v8::GCCallbackFlags flags) {
        if (!shouldLog(logger::LogSeverity::Debug(1)))
             // don't collect stats unless verbose
             return;

         v8::HeapStatistics stats;
         v8::V8::GetHeapStatistics(&stats);
         log() << "V8 GC " << _GCState::name
               << " heap stats - "
               << " total: " << stats.total_heap_size()
               << " exec: " << stats.total_heap_size_executable()
               << " used: " << stats.used_heap_size()<< " limit: "
               << stats.heap_size_limit()
               << endl;
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
         LOG(2) << "V8Scope " << static_cast<const void*>(this) << " unregistered for op " << _opId << endl;
        if (_opId != 0) {
            // scope is currently associated with an operation id
            V8ScriptEngine::OpIdToScopeMap::iterator it = _engine->_opToScopeMap.find(_opId);
            if (it != _engine->_opToScopeMap.end())
                _engine->_opToScopeMap.erase(it);
            _opId = 0;
        }
    }

    bool V8Scope::nativePrologue() {
        v8::Locker l(_isolate);
        boost::lock_guard<boost::mutex> cbEnterLock(_interruptLock);
        if (v8::V8::IsExecutionTerminating(_isolate)) {
            LOG(2) << "v8 execution interrupted.  isolate: " << static_cast<const void*>(_isolate) << endl;
            return false;
        }
        if (isKillPending()) {
            // kill flag was set before entering our callback
            LOG(2) << "marked for death while leaving callback.  isolate: " << static_cast<const void*>(_isolate) << endl;
            v8::V8::TerminateExecution(_isolate);
            return false;
        }
        _inNativeExecution = true;
        return true;
    }

    bool V8Scope::nativeEpilogue() {
        v8::Locker l(_isolate);
        boost::lock_guard<boost::mutex> cbLeaveLock(_interruptLock);
        _inNativeExecution = false;
        if (v8::V8::IsExecutionTerminating(_isolate)) {
            LOG(2) << "v8 execution interrupted.  isolate: " << static_cast<const void*>(_isolate) << endl;
            return false;
        }
        if (isKillPending()) {
            LOG(2) << "marked for death while leaving callback.  isolate: " << static_cast<const void*>(_isolate) << endl;
            v8::V8::TerminateExecution(_isolate);
            return false;
        }
        return true;
    }

    void V8Scope::kill() {
        boost::lock_guard<boost::mutex> interruptLock(_interruptLock);
        if (!_inNativeExecution) {
            // Set the TERMINATE flag on the stack guard for this isolate.
            // This won't happen between calls to nativePrologue and nativeEpilogue().
            v8::V8::TerminateExecution(_isolate);
            LOG(1) << "killing v8 scope.  isolate: " << static_cast<const void*>(_isolate) << endl;
        }
        LOG(1) << "marking v8 scope for death.  isolate: " << static_cast<const void*>(_isolate) << endl;
        _pendingKill = true;
    }

    /** check if there is a pending killOp request */
    bool V8Scope::isKillPending() const {
        return _pendingKill;
    }

    OperationContext* V8Scope::getOpContext() const {
        return _opCtx;
    }

    V8Scope::V8Scope(V8ScriptEngine * engine)
        : _engine(engine),
          _connectState(NOT),
          _cpuProfiler(),
          _inNativeExecution(true),
          _pendingKill(false),
          _opId(0),
          _opCtx(NULL) {

        // create new isolate and enter it via a scope
        _isolate.set(v8::Isolate::New());
        v8::Isolate::Scope iscope(_isolate);

        // lock the isolate and enter the context
        v8::Locker l(_isolate);
        v8::HandleScope handleScope;
        _context = v8::Context::New();
        v8::Context::Scope context_scope(_context);

        _isolate->SetData(this);

        // display heap statistics on MarkAndSweep GC run
        v8::V8::AddGCPrologueCallback(gcCallback<GCPrologueState>, v8::kGCTypeMarkSweepCompact);
        v8::V8::AddGCEpilogueCallback(gcCallback<GCEpilogueState>, v8::kGCTypeMarkSweepCompact);

        // if the isolate runs out of heap space, raise a flag on the StackGuard instead of
        // calling abort()
        v8::V8::IgnoreOutOfMemoryException();

        // create a global (rooted) object
        _global = v8::Persistent<v8::Object>::New(_context->Global());

        // Grab the RegExp constructor before user code gets a chance to change it. This ensures
        // we can always construct proper RegExps from C++.
        v8::Handle<v8::Value> regexp = _global->Get(strLitToV8("RegExp"));
        verify(regexp->IsFunction());
        _jsRegExpConstructor = v8::Persistent<v8::Function>::New(regexp.As<v8::Function>());

        // initialize lazy object template
        _LazyBsonFT = v8::Persistent<v8::FunctionTemplate>::New(v8::FunctionTemplate::New());
        LazyBsonFT()->InstanceTemplate()->SetInternalFieldCount(2);
        LazyBsonFT()->InstanceTemplate()->SetNamedPropertyHandler(
                namedGet, namedSet, NULL, namedDelete, namedEnumerator);
        LazyBsonFT()->InstanceTemplate()->SetIndexedPropertyHandler(
                indexedGet, indexedSet, NULL, indexedDelete, namedEnumerator);
        LazyBsonFT()->PrototypeTemplate()->Set(strLitToV8("_bson"),
                                               v8::Boolean::New(true),
                                               v8::DontEnum);

        _ROBsonFT = v8::Persistent<v8::FunctionTemplate>::New(v8::FunctionTemplate::New());
        ROBsonFT()->Inherit(LazyBsonFT()); // This makes LazyBsonFT()->HasInstance() true
        ROBsonFT()->InstanceTemplate()->SetInternalFieldCount(2);
        ROBsonFT()->InstanceTemplate()->SetNamedPropertyHandler(
                namedGetRO, NamedReadOnlySet, NULL, NamedReadOnlyDelete, namedEnumerator);
        ROBsonFT()->InstanceTemplate()->SetIndexedPropertyHandler(
                indexedGetRO, IndexedReadOnlySet, NULL, IndexedReadOnlyDelete, NULL);
        ROBsonFT()->PrototypeTemplate()->Set(strLitToV8("_bson"),
                                             v8::Boolean::New(true),
                                             v8::DontEnum);

        injectV8Function("print", Print);
        injectV8Function("version", Version);  // TODO: remove
        injectV8Function("gc", GCV8);
        // injectV8Function("startCpuProfiler", startCpuProfiler);
        // injectV8Function("stopCpuProfiler", stopCpuProfiler);
        // injectV8Function("getCpuProfile", getCpuProfile);

        // install BSON functions in the global object
        installBSONTypes();

        // load JS helpers (dependancy: installBSONTypes)
        execSetup(JSFiles::assert);
        execSetup(JSFiles::types);

        // install process-specific utilities in the global scope (dependancy: types.js, assert.js)
        if (_engine->_scopeInitCallback)
            _engine->_scopeInitCallback(*this);

        // install global utility functions
        installGlobalUtils(*this);
    }

    V8Scope::~V8Scope() {
        unregisterOperation();
    }

    bool V8Scope::hasOutOfMemoryException() {
        V8_SIMPLE_HEADER
        if (!_context.IsEmpty())
            return _context->HasOutOfMemoryException();
        return false;
    }

    v8::Handle<v8::Value> V8Scope::load(V8Scope* scope, const v8::Arguments &args) {
        v8::Context::Scope context_scope(scope->_context);
        for (int i = 0; i < args.Length(); ++i) {
            std::string filename(toSTLString(args[i]));
            if (!scope->execFile(filename, false, true)) {
                return v8AssertionException(string("error loading js file: ") + filename);
            }
        }
        return v8::True();
    }

    v8::Handle<v8::Value> V8Scope::nativeCallback(V8Scope* scope, const v8::Arguments &args) {
        BSONObj ret;
        string exceptionText;
        v8::HandleScope handle_scope;
        try {
            v8::Local<v8::External> f = args.Callee()->GetHiddenValue(
                scope->strLitToV8("_native_function")).As<v8::External>();
            NativeFunction function = (NativeFunction)(f->Value());
            v8::Local<v8::External> data = args.Callee()->GetHiddenValue(
                scope->strLitToV8("_native_data")).As<v8::External>();
            BSONObjBuilder b;
            for (int i = 0; i < args.Length(); ++i)
                scope->v8ToMongoElement(b, BSONObjBuilder::numStr(i), args[i]);
            BSONObj nativeArgs = b.obj();
            ret = function(nativeArgs, data->Value());
        }
        catch (const std::exception &e) {
            exceptionText = e.what();
        }
        catch (...) {
            exceptionText = "unknown exception in V8Scope::nativeCallback";
        }
        if (!exceptionText.empty()) {
            return v8AssertionException(exceptionText);
        }
        return handle_scope.Close(scope->mongoToV8Element(ret.firstElement()));
    }

    v8::Handle<v8::Value> V8Scope::v8Callback(const v8::Arguments &args) {
        v8::HandleScope handle_scope;
        V8Scope* scope = getScope(args.GetIsolate());

        if (!scope->nativePrologue())
            // execution terminated
            return v8::Undefined();

        v8::Local<v8::External> f = v8::Local<v8::External>::Cast(args.Data());
        v8Function function = (v8Function)(f->Value());
        v8::Handle<v8::Value> ret;
        string exceptionText;

        try {
            // execute the native function
            ret = function(scope, args);
        }
        catch (const std::exception& e) {
            exceptionText = e.what();
        }
        catch (...) {
            exceptionText = "unknown exception in V8Scope::v8Callback";
        }

        if (!scope->nativeEpilogue())
            // execution terminated
            return v8::Undefined();

        if (!exceptionText.empty()) {
            return v8AssertionException(exceptionText);
        }
        return handle_scope.Close(ret);
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
        V8_SIMPLE_HEADER
        _global->ForceSet(v8StringData(field), v8::Number::New(val));
    }

    void V8Scope::setString(const char * field, StringData val) {
        V8_SIMPLE_HEADER
        _global->ForceSet(v8StringData(field), v8::String::New(val.rawData(), val.size()));
    }

    void V8Scope::setBoolean(const char * field, bool val) {
        V8_SIMPLE_HEADER
        _global->ForceSet(v8StringData(field), v8::Boolean::New(val));
    }

    void V8Scope::setElement(const char *field, const BSONElement& e) {
        V8_SIMPLE_HEADER
        _global->ForceSet(v8StringData(field), mongoToV8Element(e));
    }

    void V8Scope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
        V8_SIMPLE_HEADER
        _global->ForceSet(v8StringData(field),
                          mongoToLZV8(obj, readOnly ? v8::ReadOnly : v8::None));
    }

    int V8Scope::type(const char *field) {
        V8_SIMPLE_HEADER
        v8::Handle<v8::Value> v = get(field);
        if (v->IsNull())
            return jstNULL;
        if (v->IsUndefined())
            return Undefined;
        if (v->IsString())
            return String;
        if (v->IsFunction())
            return Code;
        if (v->IsArray())
            return Array;
        if (v->IsBoolean())
            return Bool;
        // needs to be explicit NumberInt to use integer
//        if (v->IsInt32())
//            return NumberInt;
        if (v->IsNumber())
            return NumberDouble;
        if (v->IsExternal()) {
            uassert(10230,  "can't handle external yet", 0);
            return -1;
        }
        if (v->IsDate())
            return Date;
        if (v->IsObject())
            return Object;

        uasserted(12509, str::stream() << "unable to get type of field " << field);
    }

    v8::Handle<v8::Value> V8Scope::get(const char * field) {
        return _global->Get(v8StringData(field));
    }

    double V8Scope::getNumber(const char *field) {
        V8_SIMPLE_HEADER
        return get(field)->ToNumber()->Value();
    }

    int V8Scope::getNumberInt(const char *field) {
        V8_SIMPLE_HEADER
        return get(field)->ToInt32()->Value();
    }

    long long V8Scope::getNumberLongLong(const char *field) {
        V8_SIMPLE_HEADER
        return get(field)->ToInteger()->Value();
    }

    string V8Scope::getString(const char *field) {
        V8_SIMPLE_HEADER
        return toSTLString(get(field));
    }

    bool V8Scope::getBoolean(const char *field) {
        V8_SIMPLE_HEADER
        return get(field)->ToBoolean()->Value();
    }

    BSONObj V8Scope::getObject(const char * field) {
        V8_SIMPLE_HEADER
        v8::Handle<v8::Value> v = get(field);
        if (v->IsNull() || v->IsUndefined())
            return BSONObj();
        uassert(10231,  "not an object", v->IsObject());
        return v8ToMongo(v->ToObject());
    }

    v8::Handle<v8::FunctionTemplate> getNumberLongFunctionTemplate(V8Scope* scope) {
        v8::Handle<v8::FunctionTemplate> numberLong = scope->createV8Function(numberLongInit);
        v8::Handle<v8::ObjectTemplate> proto = numberLong->PrototypeTemplate();
        scope->injectV8Method("valueOf", numberLongValueOf, proto);
        scope->injectV8Method("toNumber", numberLongToNumber, proto);
        scope->injectV8Method("toString", numberLongToString, proto);
        return numberLong;
    }

    v8::Handle<v8::FunctionTemplate> getNumberIntFunctionTemplate(V8Scope* scope) {
        v8::Handle<v8::FunctionTemplate> numberInt = scope->createV8Function(numberIntInit);
        v8::Handle<v8::ObjectTemplate> proto = numberInt->PrototypeTemplate();
        scope->injectV8Method("valueOf", numberIntValueOf, proto);
        scope->injectV8Method("toNumber", numberIntToNumber, proto);
        scope->injectV8Method("toString", numberIntToString, proto);
        return numberInt;
    }

    v8::Handle<v8::FunctionTemplate> getBinDataFunctionTemplate(V8Scope* scope) {
        v8::Handle<v8::FunctionTemplate> binData = scope->createV8Function(binDataInit);
        binData->InstanceTemplate()->SetInternalFieldCount(1);
        v8::Handle<v8::ObjectTemplate> proto = binData->PrototypeTemplate();
        scope->injectV8Method("toString", binDataToString, proto);
        scope->injectV8Method("base64", binDataToBase64, proto);
        scope->injectV8Method("hex", binDataToHex, proto);
        return binData;
    }

    v8::Handle<v8::FunctionTemplate> getTimestampFunctionTemplate(V8Scope* scope) {
        v8::Handle<v8::FunctionTemplate> ts = scope->createV8Function(dbTimestampInit);
        return ts;
    }

    v8::Handle<v8::Value> minKeyToJson(V8Scope* scope, const v8::Arguments& args) {
        // MinKey can't just be an object like {$minKey:1} since insert() checks for fields that
        // start with $ and raises an error. See DBCollection.prototype._validateForStorage().
        return scope->strLitToV8("{ \"$minKey\" : 1 }");
    }

    v8::Handle<v8::Value> minKeyCall(const v8::Arguments& args) {
        // The idea here is that MinKey and MaxKey are singleton callable objects
        // that return the singleton when called. This enables all instances to
        // compare == and === to MinKey even if created by "new MinKey()" in JS.
        V8Scope* scope = getScope(args.GetIsolate());

        v8::Handle<v8::Function> func = scope->MinKeyFT()->GetFunction();
        v8::Handle<v8::String> name = scope->strLitToV8("singleton");
        v8::Handle<v8::Value> singleton = func->GetHiddenValue(name);
        if (!singleton.IsEmpty())
            return singleton;

        if (!args.IsConstructCall())
            return func->NewInstance();

        verify(scope->MinKeyFT()->HasInstance(args.This()));

        func->SetHiddenValue(name, args.This());
        return v8::Undefined();
    }

    v8::Handle<v8::FunctionTemplate> getMinKeyFunctionTemplate(V8Scope* scope) {
        v8::Handle<v8::FunctionTemplate> myTemplate = v8::FunctionTemplate::New(minKeyCall);
        myTemplate->InstanceTemplate()->SetCallAsFunctionHandler(minKeyCall);
        myTemplate->PrototypeTemplate()->Set(
                "tojson", scope->createV8Function(minKeyToJson)->GetFunction());
        myTemplate->SetClassName(scope->strLitToV8("MinKey"));
        return myTemplate;
    }

    v8::Handle<v8::Value> maxKeyToJson(V8Scope* scope, const v8::Arguments& args) {
        return scope->strLitToV8("{ \"$maxKey\" : 1 }");
    }

    v8::Handle<v8::Value> maxKeyCall(const v8::Arguments& args) {
        // See comment in minKeyCall.
        V8Scope* scope = getScope(args.GetIsolate());

        v8::Handle<v8::Function> func = scope->MaxKeyFT()->GetFunction();
        v8::Handle<v8::String> name = scope->strLitToV8("singleton");
        v8::Handle<v8::Value> singleton = func->GetHiddenValue(name);
        if (!singleton.IsEmpty())
            return singleton;

        if (!args.IsConstructCall())
            return func->NewInstance();

        verify(scope->MaxKeyFT()->HasInstance(args.This()));

        func->SetHiddenValue(name, args.This());
        return v8::Undefined();
    }

    v8::Handle<v8::FunctionTemplate> getMaxKeyFunctionTemplate(V8Scope* scope) {
        v8::Handle<v8::FunctionTemplate> myTemplate = v8::FunctionTemplate::New(maxKeyCall);
        myTemplate->InstanceTemplate()->SetCallAsFunctionHandler(maxKeyCall);
        myTemplate->PrototypeTemplate()->Set(
                "tojson", scope->createV8Function(maxKeyToJson)->GetFunction());
        myTemplate->SetClassName(scope->strLitToV8("MaxKey"));
        return myTemplate;
    }

    std::string V8Scope::v8ExceptionToSTLString(const v8::TryCatch* try_catch) {
        stringstream ss;
        v8::Local<v8::Value> stackTrace = try_catch->StackTrace();
        if (!stackTrace.IsEmpty()) {
            ss << StringData(V8String(stackTrace));
        }
        else {
            ss << StringData(V8String((try_catch->Exception())));
        }

        // get the exception message
        v8::Handle<v8::Message> message = try_catch->Message();
        if (message.IsEmpty())
            return ss.str();

        // get the resource (e.g. file or internal call)
        v8::String::Utf8Value resourceName(message->GetScriptResourceName());
        if (!*resourceName)
            return ss.str();

        string resourceNameString = *resourceName;
        if (resourceNameString.compare("undefined") == 0)
            return ss.str();
        if (resourceNameString.find("_funcs") == 0) {
            // script loaded from __createFunction
            string code;
            // find the source script based on the resource name supplied to v8::Script::Compile().
            // this is accomplished by converting the integer after the '_funcs' prefix.
            unsigned int funcNum = str::toUnsigned(resourceNameString.substr(6));
            for (map<string, ScriptingFunction>::iterator it = getFunctionCache().begin();
                 it != getFunctionCache().end();
                 ++it) {
                if (it->second == funcNum) {
                    code = it->first;
                    break;
                }
            }
            if (!code.empty()) {
                // append surrounding code (padded with up to 20 characters on each side)
                int startPos = message->GetStartPosition();
                const int kPadding = 20;
                if (startPos - kPadding < 0)
                    // lower bound exceeded
                    startPos = 0;
                else
                    startPos -= kPadding;

                int displayRange = message->GetEndPosition();
                if (displayRange + kPadding > static_cast<int>(code.length()))
                    // upper bound exceeded
                    displayRange -= startPos;
                else
                    // compensate for startPos padding
                    displayRange = (displayRange - startPos) + kPadding;

                if (startPos > static_cast<int>(code.length()) ||
                    displayRange > static_cast<int>(code.length()))
                    return ss.str();

                string codeNear = code.substr(startPos, displayRange);
                for (size_t newLine = codeNear.find('\n');
                     newLine != string::npos;
                     newLine = codeNear.find('\n')) {
                    if (static_cast<int>(newLine) > displayRange - kPadding) {
                        // truncate at first newline past the reported end position
                        codeNear = codeNear.substr(0, newLine - 1);
                        break;
                    }
                    // convert newlines to spaces
                    codeNear.replace(newLine, 1, " ");
                }
                // trim leading chars
                codeNear = str::ltrim(codeNear);
                ss << " near '" << codeNear << "' ";
                const int linenum = message->GetLineNumber();
                if (linenum != 1)
                    ss << " (line " << linenum << ")";
            }
        }
        else if (resourceNameString.find("(shell") == 0) {
            // script loaded from shell input -- simply print the error
        }
        else {
            // script loaded from file
            ss << " at " << *resourceName;
            const int linenum = message->GetLineNumber();
            if (linenum != 1) ss << ":" << linenum;
        }
        return ss.str();
    }

    // --- functions -----

    bool hasFunctionIdentifier(StringData code) {
        if (code.size() < 9 || code.find("function") != 0 )
            return false;

        return code[8] == ' ' || code[8] == '(';
    }

    v8::Local<v8::Function> V8Scope::__createFunction(const char* raw,
                                                      ScriptingFunction functionNumber) {
        v8::HandleScope handle_scope;
        v8::TryCatch try_catch;
        raw = jsSkipWhiteSpace(raw);
        string code = raw;
        if (!hasFunctionIdentifier(code)) {
            if (code.find('\n') == string::npos &&
                    ! hasJSReturn(code) &&
                    (code.find(';') == string::npos || code.find(';') == code.size() - 1)) {
                code = "return " + code;
            }
            code = "function(){ " + code + "}";
        }

        string fn = str::stream() << "_funcs" << functionNumber;
        code = str::stream() << fn << " = " << code;

        v8::Handle<v8::Script> script = v8::Script::Compile(
                                            v8::String::New(code.c_str(), code.length()),
                                            v8::String::New(fn.c_str()));

        // throw on error
        checkV8ErrorState(script, try_catch);

        v8::Local<v8::Value> result = script->Run();

        // throw on error
        checkV8ErrorState(result, try_catch);

        return handle_scope.Close(v8::Handle<v8::Function>(
                v8::Function::Cast(*_global->Get(v8::String::New(fn.c_str())))));
    }

    ScriptingFunction V8Scope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        V8_SIMPLE_HEADER
        v8::Local<v8::Value> ret = __createFunction(raw, functionNumber);
        v8::Persistent<v8::Value> f = v8::Persistent<v8::Value>::New(ret);
        uassert(10232, "not a function", f->IsFunction());
        _funcs.push_back(f);
        return functionNumber;
    }

    void V8Scope::setFunction(const char* field, const char* code) {
        V8_SIMPLE_HEADER
        _global->ForceSet(v8StringData(field),
                          __createFunction(code, getFunctionCache().size() + 1));
    }

    void V8Scope::rename(const char * from, const char * to) {
        V8_SIMPLE_HEADER;
        v8::Handle<v8::String> f = v8StringData(from);
        v8::Handle<v8::String> t = v8StringData(to);
        _global->ForceSet(t, _global->Get(f));
        _global->ForceSet(f, v8::Undefined());
    }

    int V8Scope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        V8_SIMPLE_HEADER
        v8::Handle<v8::Value> funcValue = _funcs[func-1];
        v8::TryCatch try_catch;
        v8::Local<v8::Value> result;

        // TODO SERVER-8016: properly allocate handles on the stack
        static const int MAX_ARGS = 24;
        const int nargs = argsObject ? argsObject->nFields() : 0;
        uassert(16862, "Too many arguments. Max is 24",
                nargs <= MAX_ARGS);

        v8::Handle<v8::Value> args[MAX_ARGS];
        if (nargs) {
            BSONObjIterator it(*argsObject);
            for (int i=0; i<nargs; i++) {
                BSONElement next = it.next();
                args[i] = mongoToV8Element(next, readOnlyArgs);
            }
        }

        v8::Handle<v8::Object> v8recv;
        if (recv != 0)
            v8recv = mongoToLZV8(*recv, readOnlyRecv);
        else
            v8recv = _global;

        if (!nativeEpilogue()) {
            _error = "JavaScript execution terminated";
            error() << _error << endl;
            uasserted(16711, _error);
        }

        if (timeoutMs)
            // start the deadline timer for this script
            _engine->getDeadlineMonitor()->startDeadline(this, timeoutMs);

        result = ((v8::Function*)(*funcValue))->Call(v8recv, nargs, nargs ? args : NULL);

        if (timeoutMs)
            // stop the deadline timer for this script
            _engine->getDeadlineMonitor()->stopDeadline(this);

        if (!nativePrologue()) {
            _error = "JavaScript execution terminated";
            error() << _error << endl;
            uasserted(16712, _error);
        }

        // throw on error
        checkV8ErrorState(result, try_catch);

        if (!ignoreReturn) {
            v8::Handle<v8::Object> resultObject = result->ToObject();
            // must validate the handle because TerminateExecution may have
            // been thrown after the above checks
            if (!resultObject.IsEmpty() && resultObject->Has(strLitToV8("_v8_function"))) {
                log() << "storing native function as return value" << endl;
                _lastRetIsNativeCode = true;
            }
            else {
                _lastRetIsNativeCode = false;
            }
            _global->ForceSet(strLitToV8("__returnValue"), result);
        }

        return 0;
    }

    bool V8Scope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        V8_SIMPLE_HEADER
        v8::TryCatch try_catch;

        v8::Handle<v8::Script> script =
                v8::Script::Compile(v8::String::New(code.rawData(), code.size()),
                                    v8::String::New(name.c_str(), name.length()));

        if (checkV8ErrorState(script, try_catch, reportError, assertOnError))
            return false;

        if (!nativeEpilogue()) {
            _error = "JavaScript execution terminated";
            if (reportError)
                error() << _error << endl;
            if (assertOnError)
                uasserted(13475, _error);
            return false;
        }

        if (timeoutMs)
            // start the deadline timer for this script
            _engine->getDeadlineMonitor()->startDeadline(this, timeoutMs);

        v8::Handle<v8::Value> result = script->Run();

        if (timeoutMs)
            // stopt the deadline timer for this script
            _engine->getDeadlineMonitor()->stopDeadline(this);

        if (!nativePrologue()) {
            _error = "JavaScript execution terminated";
            if (reportError)
                error() << _error << endl;
            if (assertOnError)
                uasserted(16721, _error);
            return false;
        }

        if (checkV8ErrorState(result, try_catch, reportError, assertOnError))
            return false;

        _global->ForceSet(strLitToV8("__lastres__"), result);

        if (printResult && !result->IsUndefined()) {
            // appears to only be used by shell
            cout << V8String(result) << endl;
        }

        return true;
    }

    void V8Scope::injectNative(const char *field, NativeFunction func, void* data) {
        V8_SIMPLE_HEADER    // required due to public access
        injectNative(field, func, _global, data);
    }

    void V8Scope::injectNative(const char* field,
                               NativeFunction nativeFunc,
                               v8::Handle<v8::Object>& obj,
                               void* data) {
        v8::Handle<v8::FunctionTemplate> ft = createV8Function(nativeCallback);
        injectV8Function(field, ft, obj);
        v8::Handle<v8::Function> func = ft->GetFunction();
        func->SetHiddenValue(strLitToV8("_native_function"), v8::External::New((void*)nativeFunc));
        func->SetHiddenValue(strLitToV8("_native_data"), v8::External::New(data));
    }

    v8::Handle<v8::FunctionTemplate> V8Scope::injectV8Function(const char *field, v8Function func) {
        return injectV8Function(field, func, _global);
    }

    v8::Handle<v8::FunctionTemplate> V8Scope::injectV8Function(const char *field,
                                                               v8Function func,
                                                               v8::Handle<v8::Object>& obj) {
        return injectV8Function(field, createV8Function(func), obj);
    }

    v8::Handle<v8::FunctionTemplate> V8Scope::injectV8Function(const char *fieldCStr,
                                                               v8::Handle<v8::FunctionTemplate> ft,
                                                               v8::Handle<v8::Object>& obj) {
        v8::Handle<v8::String> field = v8StringData(fieldCStr);
        ft->SetClassName(field);
        v8::Handle<v8::Function> func = ft->GetFunction();
        func->SetName(field);
        obj->ForceSet(field, func);
        return ft;
    }

    v8::Handle<v8::FunctionTemplate> V8Scope::injectV8Method(
            const char *fieldCStr,
            v8Function func,
            v8::Handle<v8::ObjectTemplate>& proto) {
        v8::Handle<v8::String> field = v8StringData(fieldCStr);
        v8::Handle<v8::FunctionTemplate> ft = createV8Function(func);
        v8::Handle<v8::Function> f = ft->GetFunction();
        f->SetName(field);
        proto->Set(field, f);
        return ft;
    }

    v8::Handle<v8::FunctionTemplate> V8Scope::createV8Function(v8Function func) {
        v8::Handle<v8::Value> funcHandle = v8::External::New(reinterpret_cast<void*>(func));
        v8::Handle<v8::FunctionTemplate> ft = v8::FunctionTemplate::New(v8Callback, funcHandle);
        ft->Set(strLitToV8("_v8_function"), v8::Boolean::New(true),
                static_cast<v8::PropertyAttribute>(v8::DontEnum | v8::ReadOnly));
        return ft;
    }

    void V8Scope::gc() {
        V8_SIMPLE_HEADER
        // trigger low memory notification.  for more granular control over garbage
        // collection cycle, @see v8::V8::IdleNotification.
        v8::V8::LowMemoryNotification();
    }

    void V8Scope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        typedef v8::Persistent<v8::FunctionTemplate> FTPtr;
        {
            V8_SIMPLE_HEADER;

            invariant(_opCtx == NULL);
            _opCtx = txn;

            if (_connectState == EXTERNAL)
                uasserted(12510, "externalSetup already called, can't call localConnect");
            if (_connectState ==  LOCAL) {
                if (_localDBName == dbName)
                    return;
                uasserted(12511,
                          str::stream() << "localConnect previously called with name "
                                        << _localDBName);
            }

            // NOTE: order is important here.  the following methods must be called after
            //       the above conditional statements.

            // install db access functions in the global object
            installDBAccess();

            // install the Mongo function object and instantiate the 'db' global
            _MongoFT = FTPtr::New(getMongoFunctionTemplate(this, true));
            injectV8Function("Mongo", MongoFT(), _global);
            execCoreFiles();
            exec("_mongo = new Mongo();", "local connect 2", false, true, true, 0);
            exec((string)"db = _mongo.getDB(\"" + dbName + "\");", "local connect 3",
                 false, true, true, 0);
            _connectState = LOCAL;
            _localDBName = dbName;
        }
        loadStored(txn);
    }

    void V8Scope::externalSetup() {
        typedef v8::Persistent<v8::FunctionTemplate> FTPtr;
        V8_SIMPLE_HEADER
        if (_connectState == EXTERNAL)
            return;
        if (_connectState == LOCAL)
            uasserted(12512, "localConnect already called, can't call externalSetup");

        // install db access functions in the global object
        installDBAccess();

        // install thread-related functions (e.g. _threadInject)
        installFork(this, _global, _context);

        // install 'load' helper function
        injectV8Function("load", load);

        // install the Mongo function object
        _MongoFT = FTPtr::New(getMongoFunctionTemplate(this, false));
        injectV8Function("Mongo", MongoFT(), _global);
        execCoreFiles();
        _connectState = EXTERNAL;
    }

    void V8Scope::installDBAccess() {
        typedef v8::Persistent<v8::FunctionTemplate> FTPtr;
        _DBFT             = FTPtr::New(createV8Function(dbInit));
        _DBQueryFT        = FTPtr::New(createV8Function(dbQueryInit));
        _DBCollectionFT   = FTPtr::New(createV8Function(collectionInit));

        // These must be done before calling injectV8Function
        DBFT()->InstanceTemplate()->SetNamedPropertyHandler(collectionGetter, collectionSetter);
        DBQueryFT()->InstanceTemplate()->SetIndexedPropertyHandler(dbQueryIndexAccess);
        DBCollectionFT()->InstanceTemplate()->SetNamedPropertyHandler(collectionGetter,
                                                                      collectionSetter);

        injectV8Function("DB", DBFT(), _global);
        injectV8Function("DBQuery", DBQueryFT(), _global);
        injectV8Function("DBCollection", DBCollectionFT(), _global);

        // The internal cursor type isn't exposed to the users at all
        _InternalCursorFT = FTPtr::New(getInternalCursorFunctionTemplate(this));
    }

    void V8Scope::installBSONTypes() {
        typedef v8::Persistent<v8::FunctionTemplate> FTPtr;
        _ObjectIdFT  = FTPtr::New(injectV8Function("ObjectId", objectIdInit));
        _DBRefFT     = FTPtr::New(injectV8Function("DBRef", dbRefInit));
        _DBPointerFT = FTPtr::New(injectV8Function("DBPointer", dbPointerInit));

        _BinDataFT    = FTPtr::New(getBinDataFunctionTemplate(this));
        _NumberLongFT = FTPtr::New(getNumberLongFunctionTemplate(this));
        _NumberIntFT  = FTPtr::New(getNumberIntFunctionTemplate(this));
        _TimestampFT  = FTPtr::New(getTimestampFunctionTemplate(this));
        _MinKeyFT     = FTPtr::New(getMinKeyFunctionTemplate(this));
        _MaxKeyFT     = FTPtr::New(getMaxKeyFunctionTemplate(this));

        injectV8Function("BinData", BinDataFT(), _global);
        injectV8Function("NumberLong", NumberLongFT(), _global);
        injectV8Function("NumberInt", NumberIntFT(), _global);
        injectV8Function("Timestamp", TimestampFT(), _global);

        // These are instances created from the functions, not the functions themselves
        _global->ForceSet(strLitToV8("MinKey"), MinKeyFT()->GetFunction()->NewInstance());
        _global->ForceSet(strLitToV8("MaxKey"), MaxKeyFT()->GetFunction()->NewInstance());

        // These all create BinData objects so we don't need to hold on to them.
        injectV8Function("UUID", uuidInit);
        injectV8Function("MD5", md5Init);
        injectV8Function("HexData", hexDataInit);

        injectV8Function("bsonWoCompare", bsonWoCompare);

        _global->Get(strLitToV8("Object"))->ToObject()->ForceSet(
                            strLitToV8("bsonsize"),
                            createV8Function(bsonsize)->GetFunction());
        _global->Get(strLitToV8("Object"))->ToObject()->ForceSet(
                            strLitToV8("invalidForStorage"),
                            createV8Function(v8ObjectInvalidForStorage)->GetFunction());
    }


    // ----- internal -----

    void V8Scope::reset() {
        V8_SIMPLE_HEADER
        unregisterOperation();
        _error = "";
        _pendingKill = false;
        _inNativeExecution = true;
    }

    v8::Local<v8::Value> V8Scope::newFunction(StringData code) {
        v8::HandleScope handle_scope;
        v8::TryCatch try_catch;
        string codeStr = str::stream() << "____MongoToV8_newFunction_temp = " << code;

        v8::Local<v8::Script> compiled = v8::Script::New(v8::String::New(codeStr.c_str(),
                                                                         codeStr.length()));

        // throw on compile error
        checkV8ErrorState(compiled, try_catch);

        v8::Local<v8::Value> ret = compiled->Run();

        // throw on run/assignment error
        checkV8ErrorState(ret, try_catch);

        return handle_scope.Close(ret);
    }

    v8::Local<v8::Value> V8Scope::newId(const OID &id) {
        v8::HandleScope handle_scope;
        v8::Handle<v8::Function> idCons = ObjectIdFT()->GetFunction();
        v8::Handle<v8::Value> argv[1];
        const string& idString = id.toString();
        argv[0] = v8::String::New(idString.c_str(), idString.length());
        return handle_scope.Close(idCons->NewInstance(1, argv));
    }

    // --- random utils ----

    static logger::MessageLogDomain* jsPrintLogDomain;
    v8::Handle<v8::Value> V8Scope::Print(V8Scope* scope, const v8::Arguments& args) {
        LogstreamBuilder builder(jsPrintLogDomain, getThreadName(), logger::LogSeverity::Log());
        std::ostream& ss = builder.stream();
        v8::HandleScope handle_scope;
        bool first = true;
        for (int i = 0; i < args.Length(); i++) {
            if (first)
                first = false;
            else
                ss << " ";

            if (args[i].IsEmpty()) {
                // failed to get object to convert
                ss << "[unknown type]";
                continue;
            }
            if (args[i]->IsExternal()) {
                // object is External
                ss << "[mongo internal]";
                continue;
            }

            v8::String::Utf8Value str(args[i]);
            ss << *str;
        }
        ss << "\n";
        return handle_scope.Close(v8::Undefined());
    }

    v8::Handle<v8::Value> V8Scope::Version(V8Scope* scope, const v8::Arguments& args) {
        v8::HandleScope handle_scope;
        return handle_scope.Close(v8::String::New(v8::V8::GetVersion()));
    }

    v8::Handle<v8::Value> V8Scope::GCV8(V8Scope* scope, const v8::Arguments& args) {
        // trigger low memory notification.  for more granular control over garbage
        // collection cycle, @see v8::V8::IdleNotification.
        v8::V8::LowMemoryNotification();
        return v8::Undefined();
    }

    v8::Handle<v8::Value> V8Scope::startCpuProfiler(V8Scope* scope, const v8::Arguments& args) {
        if (args.Length() != 1 || !args[0]->IsString()) {
            return v8AssertionException("startCpuProfiler takes a string argument");
        }
        scope->_cpuProfiler.start(*v8::String::Utf8Value(args[0]->ToString()));
        return v8::Undefined();
    }

    v8::Handle<v8::Value> V8Scope::stopCpuProfiler(V8Scope* scope, const v8::Arguments& args) {
        if (args.Length() != 1 || !args[0]->IsString()) {
            return v8AssertionException("stopCpuProfiler takes a string argument");
        }
        scope->_cpuProfiler.stop(*v8::String::Utf8Value(args[0]->ToString()));
        return v8::Undefined();
    }

    v8::Handle<v8::Value> V8Scope::getCpuProfile(V8Scope* scope, const v8::Arguments& args) {
        if (args.Length() != 1 || !args[0]->IsString()) {
            return v8AssertionException("getCpuProfile takes a string argument");
        }
        return scope->mongoToLZV8(scope->_cpuProfiler.fetch(
                *v8::String::Utf8Value(args[0]->ToString())));
    }

    /**
     * Check for an error condition (e.g. empty handle, JS exception, OOM) after executing
     * a v8 operation.
     * @resultHandle         handle storing the result of the preceding v8 operation
     * @try_catch            the active v8::TryCatch exception handler
     * @param reportError    if true, log an error message
     * @param assertOnError  if true, throw an exception if an error is detected
     *                       if false, return value indicates error state
     * @return true if an error was detected and assertOnError is set to false
     *         false if no error was detected
     */
    template <typename _HandleType>
    bool V8Scope::checkV8ErrorState(const _HandleType& resultHandle,
                                    const v8::TryCatch& try_catch,
                                    bool reportError,
                                    bool assertOnError) {
        bool haveError = false;

        if (try_catch.HasCaught() && try_catch.CanContinue()) {
            // normal JS exception
            _error = v8ExceptionToSTLString(&try_catch);
            haveError = true;
        }
        else if (hasOutOfMemoryException()) {
            // out of memory exception (treated as terminal)
            _error = "JavaScript execution failed -- v8 is out of memory";
            haveError = true;
        }
        else if (resultHandle.IsEmpty() || try_catch.HasCaught()) {
            // terminal exception (due to empty handle, termination, etc.)
            _error = "JavaScript execution failed";
            haveError = true;
        }

        if (haveError) {
            if (reportError)
                error() << _error << std::endl;
            if (assertOnError)
                uasserted(16722, _error);
            return true;
        }

        return false;
    }

    MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
        jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
        return Status::OK();
    }

} // namespace mongo
