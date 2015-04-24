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

#include "mongo/scripting/engine_v8-sm.h"

#include <iostream>

#include "mongo/base/init.h"
#include "mongo/db/operation_context.h"
#include "mongo/db/service_context.h"
#include "mongo/platform/unordered_set.h"
#include "mongo/scripting/v8-sm_db.h"
#include "mongo/scripting/v8-sm_utils.h"
#include "mongo/util/base64.h"
#include "mongo/util/log.h"
#include "mongo/util/mongoutils/str.h"

using namespace mongoutils;

namespace mongo {

    namespace {
        using namespace JS;

        static constexpr JSClass globalClass = {
            "global",
            JSCLASS_GLOBAL_FLAGS
        };
    }

    using std::cout;
    using std::endl;
    using std::map;
    using std::string;
    using std::stringstream;

    // Generated symbols for JS files
    namespace JSFiles {
        extern const JSFile types;
        extern const JSFile assert;
    }

     SMScriptEngine::SMScriptEngine()
     {
         JS_Init();
     }

     SMScriptEngine::~SMScriptEngine() {
         JS_ShutDown();
     }

     void ScriptEngine::setup() {
         if (!globalScriptEngine) {
             globalScriptEngine = new SMScriptEngine();

             if (hasGlobalServiceContext()) {
                 getGlobalServiceContext()->registerKillOpListener(globalScriptEngine);
             }
         }
     }

     std::string ScriptEngine::getInterpreterVersionString() {
         return "SM 40a1";
     }

     std::string SMScope::getError() {
         return ""; // TODO ...
     }

     void SMScriptEngine::interrupt(unsigned opId) {
         // TODO ...
     }

     void SMScriptEngine::interruptAll() {
         // TODO ...
     }

     void SMScope::registerOperation(OperationContext* txn) {
         // TODO ...
     }

     void SMScope::unregisterOperation() {
         // TODO ...
    }

    void SMScope::kill() {
        // TODO ...
    }

    /** check if there is a pending killOp request */
    bool SMScope::isKillPending() const {
        // TODO ...
        return false;
    }
    
    OperationContext* SMScope::getOpContext() const {
        // TODO ...
        return nullptr;
    }

    SMScope::SMScope(SMScriptEngine * engine) :
        _runtime(JS_NewRuntime(8 * 1024 * 1024)),
        _context(JS_NewContext(_runtime, 8192)),
        _global(_context)
    {
        _global = JS_NewGlobalObject(_context, &globalClass, nullptr, JS::DontFireOnNewGlobalHook);

        JSAutoCompartment ac(_context, _global);

        JS_InitStandardClasses(_context, _global);
    }

    SMScope::~SMScope() {
        unregisterOperation();
        JS_DestroyContext(_context);
        JS_DestroyRuntime(_runtime);
    }

    bool SMScope::hasOutOfMemoryException() {
        return false;
    }

    void SMScope::init(const BSONObj * data) {
        if (! data)
            return;

        BSONObjIterator i(*data);
        while (i.more()) {
            BSONElement e = i.next();
            setElement(e.fieldName(), e);
        }
    }

    void SMScope::setNumber(const char * field, double val) {
    }

    void SMScope::setString(const char * field, StringData val) {
    }

    void SMScope::setBoolean(const char * field, bool val) {
    }

    void SMScope::setElement(const char *field, const BSONElement& e) {
    }

    void SMScope::setObject(const char *field, const BSONObj& obj, bool readOnly) {
    }

    int SMScope::type(const char *field) {
        // uasserted(12509, str::stream() << "unable to get type of field " << field);
        return 0; // TODO ...
    }

    double SMScope::getNumber(const char *field) {
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toDouble();
    }

    int SMScope::getNumberInt(const char *field) {
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toInt32();
    }

    long long SMScope::getNumberLongLong(const char *field) {
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toDouble();
    }

    string SMScope::getString(const char *field) {
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        char* cstr = JS_EncodeString(_context, x.toString());

        std::string out(cstr);

        JS_free(_context, cstr);

        return out;
    }

    bool SMScope::getBoolean(const char *field) {
        JS::RootedValue x(_context);

        JS_GetProperty(_context, _global, field, &x);

        return x.toBoolean();
    }

    BSONObj SMScope::getObject(const char * field) {
        // uassert(10231,  "not an object", v->IsObject());
        return BSONObj(); // TODO ...
    }

    BSONObj SMScope::smToMongo(JS::HandleObject o, int depth) {
        BSONObj originalBSON;

        BSONObjBuilder b;

        // We special case the _id field in top-level objects and move it to the front.
        // This matches other drivers behavior and makes finding the _id field quicker in BSON.
        if (depth == 0) {
            bool found;
            JS_HasProperty(_context, o, "_id", &found);

            if (found) {
                JS::RootedValue x(_context);
                JS_GetProperty(_context, o, "_id", &x);

                smToMongoElement(b, "_id", x, 0, &originalBSON);
            }
        }

        JS::AutoIdArray names(_context, JS_Enumerate(_context, o));

        for (size_t i = 0; i < names.length(); ++i) {
            JS::RootedValue x(_context);

            JS_IdToValue(_context, names[i], &x);

            bool is_id;
            JS_StringEqualsAscii(_context, x.toString(), "_id", &is_id);

            if (depth == 0 && is_id)
                continue;

            JS::RootedValue value(_context);

            JS::RootedId id(_context, names[i]);

            JS_GetPropertyById(_context, o, id, &value);

            char* sname = JS_EncodeString(_context, x.toString());

            smToMongoElement(b, sname, value, depth + 1, &originalBSON);

            JS_free(_context, sname);
        }

        const int sizeWithEOO = b.len() + 1/*EOO*/ - 4/*BSONObj::Holder ref count*/;
        uassert(17260, str::stream() << "Converting from JavaScript to BSON failed: "
                                     << "Object size " << sizeWithEOO << " exceeds limit of "
                                     << BSONObjMaxInternalSize << " bytes.",
                sizeWithEOO <= BSONObjMaxInternalSize);

        return b.obj(); // Would give an uglier error than above for oversized objects.
    }

    void SMScope::smToMongoElement(BSONObjBuilder & b, StringData sname,
                                   JS::HandleValue value, int depth,
                                   BSONObj* originalParent) {
        uassert(17279,
                str::stream() << "Exceeded depth limit of " << objectDepthLimit
                              << " when converting js object to BSON. Do you have a cycle?",
                depth < objectDepthLimit);

        // Null char should be at the end, not in the string
        uassert(16985,
                str::stream() << "JavaScript property (name) contains a null char "
                              << "which is not allowed in BSON. "
                              << originalParent->jsonString(),
                (string::npos == sname.find('\0')) );

        if (value.isString()) {
            char* str = JS_EncodeString(_context, value.toString());
            b.append(sname, StringData(str));
            return;
        }
        /*
        if (value->IsFunction()) {
            uassert(16716, "cannot convert native function to BSON",
                    !value->ToObject()->Has(strLitToV8("_v8_function")));
            b.appendCode(sname, V8String(value));
            return;
        }
        */
        if (value.isNumber()) {
            smToMongoNumber(b, sname, value, originalParent);
            return;
        }
        if (JS_IsArrayObject(_context, value)) {
            // Note: can't use BSONArrayBuilder because need to call recursively
            BSONObjBuilder arrBuilder(b.subarrayStart(sname));

            RootedObject array(_context, value.toObjectOrNull());

            uint32_t len;
            JS_GetArrayLength(_context, array, &len);

            for (uint32_t i = 0; i < len; i++) {
                const string name = BSONObjBuilder::numStr(i);

                RootedValue value(_context);

                JS_GetElement(_context, array, i, &value);
                smToMongoElement(arrBuilder, name, value, depth+1, originalParent);
            }
            return;
        }
        /*
        if (value->IsDate()) {
            long long dateval = (long long)(v8::Date::Cast(*value)->NumberValue());
            b.appendDate(sname, Date_t((unsigned long long) dateval));
            return;
        }
        if (value->IsExternal())
            return;
        */
        if (value.isObject()) {
            smToMongoObject(b, sname, value, depth, originalParent);
            return;
        }

        if (value.isBoolean()) {
            b.appendBool(sname, value.toBoolean());
            return;
        }
        else if (value.isUndefined()) {
            b.appendUndefined(sname);
            return;
        }
        else if (value.isNull()) {
            b.appendNull(sname);
            return;
        }
        uasserted(16662, str::stream() << "unable to convert JavaScript property to mongo element "
                                   << sname);
    }
    ScriptingFunction SMScope::_createFunction(const char* raw, ScriptingFunction functionNumber) {
        // uassert(10232, "not a function", ret->IsFunction());
        return functionNumber;
    }

    void SMScope::setFunction(const char* field, const char* code) {
        // TODO ...
    }

    void SMScope::rename(const char * from, const char * to) {
        // TODO ...
    }

    int SMScope::invoke(ScriptingFunction func, const BSONObj* argsObject, const BSONObj* recv,
                        int timeoutMs, bool ignoreReturn, bool readOnlyArgs, bool readOnlyRecv) {
        // TODO ...

        return 0;
    }

    bool SMScope::exec(StringData code, const string& name, bool printResult,
                       bool reportError, bool assertOnError, int timeoutMs) {
        // TODO ...

        return true;
    }

    void SMScope::injectNative(const char *field, NativeFunction func, void* data) {
        // TODO ...
    }

    void SMScope::gc() {
        // TODO ...
    }

    void SMScope::localConnectForDbEval(OperationContext* txn, const char * dbName) {
        // TODO ...
    }

    void SMScope::externalSetup() {
        // TODO ...
    }

    // ----- internal -----

    void SMScope::reset() {
        // TODO ...
    }

    // --- random utils ----

    static logger::MessageLogDomain* jsPrintLogDomain;

    MONGO_INITIALIZER(JavascriptPrintDomain)(InitializerContext*) {
        jsPrintLogDomain = logger::globalLogManager()->getNamedDomain("javascriptOutput");
        return Status::OK();
    }

} // namespace mongo
