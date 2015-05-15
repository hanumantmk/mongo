//engine_v8.h

/*    Copyright 2014 MongoDB Inc.
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

#define JS_USE_CUSTOM_ALLOCATOR

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <set>
#include <string>
#include <vector>

#include "mongo/base/disallow_copying.h"
#include "mongo/base/string_data.h"
#include "mongo/client/dbclientinterface.h"
#include "mongo/client/dbclientcursor.h"
#include "mongo/platform/unordered_map.h"
#include "mongo/scripting/engine.h"
#include "mongo/scripting/v8_deadline_monitor.h"
#include "mongo/scripting/v8-sm_profiler.h"
#include "mongo/scripting/v8-sm_db.h"
#include "mongo/scripting/sm_class.hpp"

#include "jsapi.h"
#include "jscustomallocator.h"

namespace mongo {

    class SMScriptEngine;
    class SMScope;
    class BSONHolder;
    class JSThreadConfig;

    /**
     * A SMScope represents a unit of javascript execution environment; specifically a single
     * isolate and a single context executing in a single mongo thread.  A SMScope can be reused
     * in another thread only after reset() has been called.
     *
     * NB:
     *   - v8 objects/handles/etc. cannot be shared between SMScopes
     *   - in mongod, each scope is associated with an opId (for KillOp support)
     *   - any public functions that call the v8 API should use a SM_SIMPLE_HEADER
     *   - the caller of any public function that returns a v8 type or has a v8 handle argument
     *         must enter the isolate, context, and set up the appropriate handle scope
     */
    class SMScope : public Scope {
    public:

        SMScope(SMScriptEngine* engine);
        ~SMScope();

        virtual void init(const BSONObj* data);

        /**
         * Reset the state of this scope for use by another thread or operation
         */
        virtual void reset();

        /**
         * Terminate this scope
         */
        virtual void kill();

        /** check if there is a pending killOp request */
        bool isKillPending() const;
        
        /**
         * Obtains the operation context associated with this Scope, so it can be given to the
         * DBDirectClient used by the SM engine's connection. Only needed for dbEval.
         */
        OperationContext* getOpContext() const;

        /**
         * Register this scope with the mongo op id.  If executing outside the
         * context of a mongo operation (e.g. from the shell), killOp will not
         * be supported.
         */
        virtual void registerOperation(OperationContext* txn);

        /**
         * Unregister this scope with the mongo op id.
         */
        virtual void unregisterOperation();

        /**
         * Connect to a local database, create a Mongo object instance, and load any
         * server-side js into the global object
         */
        virtual void localConnectForDbEval(OperationContext* txn, const char* dbName);

        virtual void externalSetup();

        virtual std::string getError();

        virtual bool hasOutOfMemoryException();

        /**
         * Run the garbage collector on this scope (native function).  @see GCSM for the
         * javascript binding version.
         */
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

        virtual int invoke(ScriptingFunction func, const BSONObj* args, const BSONObj* recv,
                           int timeoutMs = 0, bool ignoreReturn = false,
                           bool readOnlyArgs = false, bool readOnlyRecv = false);

        virtual bool exec(StringData code, const std::string& name, bool printResult,
                          bool reportError, bool assertOnError, int timeoutMs);

        // functions to create v8 object and function templates
        virtual void injectNative(const char* field, NativeFunction func, void* data = 0);

        virtual ScriptingFunction _createFunction(const char* code,
                                                  ScriptingFunction functionNumber = 0);
        void mongoToSMElement(const BSONElement &elem, bool readOnly, JS::MutableHandleValue);
        mongo::BSONObj smToMongo(JS::HandleObject o, int depth = 0);
        void mongoToLZSM(const BSONObj& m, bool readOnly, JS::MutableHandleValue out);

        void smToMongoObject(BSONObjBuilder& b,
                             StringData elementName,
                             JS::HandleValue value,
                             int depth,
                             BSONObj* originalParent);
        void smToMongoElement(BSONObjBuilder & b, StringData sname,
                                       JS::HandleValue value, int depth,
                                       BSONObj* originalParent);
        void checkBool(bool x);

        std::string toSTLString(JSString* str);
        void fromStringData(StringData sd, JS::MutableHandleValue out);

    private:

        void __createFunction(const char* raw, ScriptingFunction functionNumber, JS::MutableHandleValue fun);
        void _setValue(const char * field, JS::HandleValue val);
        void newFunction(StringData code, JS::MutableHandleValue out);
        void injectSMFunction(StringData sd, JSNative);

        void smToMongoNumber(BSONObjBuilder& b,
                             StringData elementName,
                             JS::HandleValue value,
                             BSONObj* originalParent);
        void smToMongoRegex(BSONObjBuilder& b,
                            StringData elementName,
                            JS::HandleValue value);
        void smToMongoDBRef(BSONObjBuilder& b,
                            StringData elementName,
                            JS::HandleValue value);
        void smToMongoBinData(BSONObjBuilder& b,
                              StringData elementName,
                              JS::HandleValue value);
        OID smToMongoObjectID(JS::HandleValue value);
        void installOIDProto();
        void installNLProto();

        class CleanUpMagic {
        public:
            CleanUpMagic();
            ~CleanUpMagic();

            JSRuntime* _runtime;
            JSContext* _context;
        };

        void installDBAccess();

        void makeCursor(DBClientCursor* cursor, JS::MutableHandleValue out);
        void installBSONTypes();

    public:
        SMScriptEngine* _engine;
        CleanUpMagic _magic;
        JSRuntime* _runtime;
        JSContext* _context;
        JS::PersistentRootedObject _global;
        std::vector<JS::PersistentRootedValue> _funcs;
        std::atomic_bool _pendingKill;
        unsigned int _opId;                   // op id for this scope
        OperationContext* _opCtx;    // Op context for DbEval
        std::atomic_bool _pendingGC;
        enum ConnectState { NOT, LOCAL, EXTERNAL };
        ConnectState _connectState;
        SMClass<OIDClass> _oid;
        SMClass<NumberLongClass> _numberLong;

    };

    class SMScriptEngine : public ScriptEngine {
        friend SMScope;
    public:
        SMScriptEngine();
        virtual ~SMScriptEngine();
        virtual Scope* createScope() { return new SMScope(this); }
        virtual void runTest() {}
        bool utf8Ok() const { return true; }

        /**
         * Interrupt a single active v8 execution context
         * NB: To interrupt a context, we must acquire the following locks (in order):
         *       - mutex to protect the the map of all scopes (_globalInterruptLock)
         *       - mutex to protect the scope that's being interrupted (_interruptLock)
         * The scope will be removed from the map upon destruction, and the op id
         * will be updated if the scope is ever reused from a pool.
         */
        virtual void interrupt(unsigned opId);

        /**
         * Interrupt all v8 contexts (and isolates).  @see interrupt().
         */
        virtual void interruptAll();

        DeadlineMonitor<SMScope>* getDeadlineMonitor() { return &_deadlineMonitor; }
    private:

        typedef std::map<unsigned, SMScope*> OpIdToScopeMap;

        mongo::mutex _globalInterruptLock;  // protects map of all operation ids -> scope

        OpIdToScopeMap _opToScopeMap;       // map of mongo op ids to scopes (protected by
                                            // _globalInterruptLock).
        DeadlineMonitor<SMScope> _deadlineMonitor;

    };

    extern ScriptEngine* globalScriptEngine;

}
