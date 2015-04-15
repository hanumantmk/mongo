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

#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <v8.h>
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
#include "mongo/scripting/v8-4.1_profiler.h"

namespace mongo {

    class V8ScriptEngine;
    class V8Scope;
    class BSONHolder;
    class JSThreadConfig;

    /**
     * A V8Scope represents a unit of javascript execution environment; specifically a single
     * isolate and a single context executing in a single mongo thread.  A V8Scope can be reused
     * in another thread only after reset() has been called.
     *
     * NB:
     *   - v8 objects/handles/etc. cannot be shared between V8Scopes
     *   - in mongod, each scope is associated with an opId (for KillOp support)
     *   - any public functions that call the v8 API should use a V8_SIMPLE_HEADER
     *   - the caller of any public function that returns a v8 type or has a v8 handle argument
     *         must enter the isolate, context, and set up the appropriate handle scope
     */
    class V8Scope : public Scope {
    public:

        V8Scope(V8ScriptEngine* engine);
        ~V8Scope();

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
         * DBDirectClient used by the V8 engine's connection. Only needed for dbEval.
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

        virtual void installDBAccess();

        virtual void installBSONTypes();

        virtual std::string getError() { return _error; }

        virtual bool hasOutOfMemoryException();

        /**
         * Run the garbage collector on this scope (native function).  @see GCV8 for the
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

    private:
        /**
         * Recursion limit when converting from JS objects to BSON.
         */
        static const int objectDepthLimit = 150;

        V8ScriptEngine* _engine;

        std::string _error;

        enum ConnectState { NOT, LOCAL, EXTERNAL };
        ConnectState _connectState;

        mongo::mutex _interruptLock; // protects interruption-related flags
        bool _inNativeExecution;     // protected by _interruptLock
        bool _pendingKill;           // protected by _interruptLock
        unsigned int _opId;          // op id for this scope
        OperationContext* _opCtx;    // Op context for DbEval
    };

    class V8ScriptEngine : public ScriptEngine {
    public:
        V8ScriptEngine();
        virtual ~V8ScriptEngine();
        virtual Scope* createScope() { return new V8Scope(this); }
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

    private:
        friend class V8Scope;

        std::string printKnownOps_inlock();

        /**
         * Get the deadline monitor instance for the v8 ScriptEngine
         */
        DeadlineMonitor<V8Scope>* getDeadlineMonitor() { return &_deadlineMonitor; }

        typedef std::map<unsigned, V8Scope*> OpIdToScopeMap;
        mongo::mutex _globalInterruptLock;  // protects map of all operation ids -> scope
        OpIdToScopeMap _opToScopeMap;       // map of mongo op ids to scopes (protected by
                                            // _globalInterruptLock).
        DeadlineMonitor<V8Scope> _deadlineMonitor;
    };

    extern ScriptEngine* globalScriptEngine;

}
