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

    // ----- internal -----

    v8::Local<v8::Value> V8Scope::newId(const OID &id) {
        v8::HandleScope handle_scope;
        v8::Handle<v8::Function> idCons = ObjectIdFT()->GetFunction();
        v8::Handle<v8::Value> argv[1];
        const string& idString = id.toString();
        argv[0] = v8::String::New(idString.c_str(), idString.length());
        return handle_scope.Close(idCons->NewInstance(1, argv));
    }

    // --- random utils ----

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

} // namespace mongo
