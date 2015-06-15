// v8_db.cpp

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

#include "mongo/scripting/v8_db.h"

#include <iostream>
#include <iomanip>

#include "mongo/base/init.h"
#include "mongo/base/status_with.h"
#include "mongo/client/native_sasl_client_session.h"
#include "mongo/client/sasl_client_authenticate.h"
#include "mongo/client/sasl_scramsha1_client_conversation.h"
#include "mongo/db/namespace_string.h"
#include "mongo/db/operation_context.h"
#include "mongo/s/d_state.h"
#include "mongo/scripting/engine_v8.h"
#include "mongo/scripting/v8_utils.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/base64.h"
#include "mongo/util/hex.h"
#include "mongo/util/text.h"

using namespace std;
using std::unique_ptr;
using std::shared_ptr;

namespace mongo {

    namespace {
        std::vector<V8FunctionPrototypeManipulatorFn> _mongoPrototypeManipulators;
        bool _mongoPrototypeManipulatorsFrozen = false;

        MONGO_INITIALIZER(V8MongoPrototypeManipulatorRegistry)(InitializerContext* context) {
            return Status::OK();
        }

        MONGO_INITIALIZER_WITH_PREREQUISITES(V8MongoPrototypeManipulatorRegistrationDone,
                                             ("V8MongoPrototypeManipulatorRegistry"))
            (InitializerContext* context) {

            _mongoPrototypeManipulatorsFrozen = true;
            return Status::OK();
        }

    }  // namespace

    void v8RegisterMongoPrototypeManipulator(const V8FunctionPrototypeManipulatorFn& manipulator) {
        fassert(16467, !_mongoPrototypeManipulatorsFrozen);
        _mongoPrototypeManipulators.push_back(manipulator);
    }

    static v8::Handle<v8::Value> newInstance(v8::Handle<v8::Function> f, const v8::Arguments& args) {
        // need to translate arguments into an array
        v8::HandleScope handle_scope;
        const int argc = args.Length();
        static const int MAX_ARGC = 24;
        uassert(16858, "Too many arguments. Max is 24",
                argc <= MAX_ARGC);

        // TODO SERVER-8016: properly allocate handles on the stack
        v8::Handle<v8::Value> argv[MAX_ARGC];
        for (int i = 0; i < argc; ++i) {
            argv[i] = args[i];
        }
        return handle_scope.Close(f->NewInstance(argc, argv));
    }

    std::shared_ptr<DBClientBase> getConnection(V8Scope* scope, const v8::Arguments& args) {
        verify(scope->MongoFT()->HasInstance(args.This()));
        verify(args.This()->InternalFieldCount() == 1);
        v8::Local<v8::External> c = v8::External::Cast(*(args.This()->GetInternalField(0)));
        std::shared_ptr<DBClientBase>* conn =
            static_cast<std::shared_ptr<DBClientBase>*>(c->Value());
        massert(16667, "Unable to get db client connection", conn && conn->get());
        return *conn;
    }

    /**
     * get cursor from v8 argument
     */
    mongo::DBClientCursor* getCursor(V8Scope* scope, const v8::Arguments& args) {
        verify(scope->InternalCursorFT()->HasInstance(args.This()));
        verify(args.This()->InternalFieldCount() == 1);
        v8::Local<v8::External> c = v8::External::Cast(*(args.This()->GetInternalField(0)));
        mongo::DBClientCursor* cursor = static_cast<mongo::DBClientCursor*>(c->Value());
        return cursor;
    }

    v8::Handle<v8::Value> objectIdInit(V8Scope* scope, const v8::Arguments& args) {
        if (!args.IsConstructCall()) {
            v8::Handle<v8::Function> f = scope->ObjectIdFT()->GetFunction();
            return newInstance(f, args);
        }

        v8::Handle<v8::Object> it = args.This();
        verify(scope->ObjectIdFT()->HasInstance(it));

        OID oid;
        if (args.Length() == 0) {
            oid.init();
        }
        else {
            string s = toSTLString(args[0]);
            try {
                Scope::validateObjectIdString(s);
            }
            catch (const MsgAssertionException& m) {
                return v8AssertionException(m.toString());
            }
            oid.init(s);
        }

        it->ForceSet(scope->v8StringData("str"), v8::String::New(oid.toString().c_str()));
        return it;
    }

    v8::Handle<v8::Value> dbTimestampInit(V8Scope* scope, const v8::Arguments& args) {
        if (!args.IsConstructCall()) {
            v8::Handle<v8::Function> f = scope->TimestampFT()->GetFunction();
            return newInstance(f, args);
        }

        v8::Handle<v8::Object> it = args.This();
        verify(scope->TimestampFT()->HasInstance(it));

        if (args.Length() == 0) {
            it->ForceSet(scope->v8StringData("t"), v8::Number::New(0));
            it->ForceSet(scope->v8StringData("i"), v8::Number::New(0));
        }
        else if (args.Length() == 2) {
            if (!args[0]->IsNumber()) {
                return v8AssertionException("Timestamp time must be a number");
            }
            if (!args[1]->IsNumber()) {
                return v8AssertionException("Timestamp increment must be a number");
            }
            int64_t t = args[0]->IntegerValue();
            int64_t largestVal = int64_t(Timestamp::max().getSecs());
            if( t > largestVal )
                return v8AssertionException( str::stream()
                        << "The first argument must be in seconds; "
                        << t << " is too large (max " << largestVal << ")");
            it->ForceSet(scope->v8StringData("t"), args[0]);
            it->ForceSet(scope->v8StringData("i"), args[1]);
        }
        else {
            return v8AssertionException("Timestamp needs 0 or 2 arguments");
        }

        return it;
    }

    v8::Handle<v8::Value> numberLongInit(V8Scope* scope, const v8::Arguments& args) {
        if (!args.IsConstructCall()) {
            v8::Handle<v8::Function> f = scope->NumberLongFT()->GetFunction();
            return newInstance(f, args);
        }

        argumentCheck(args.Length() == 0 || args.Length() == 1 || args.Length() == 3,
                      "NumberLong needs 0, 1 or 3 arguments")

        v8::Handle<v8::Object> it = args.This();
        verify(scope->NumberLongFT()->HasInstance(it));

        if (args.Length() == 0) {
            it->ForceSet(scope->v8StringData("floatApprox"), v8::Number::New(0));
        }
        else if (args.Length() == 1) {
            if (args[0]->IsNumber()) {
                it->ForceSet(scope->v8StringData("floatApprox"), args[0]);
            }
            else {
                v8::String::Utf8Value data(args[0]);
                string num = *data;
                const char *numStr = num.c_str();
                long long n;
                try {
                    n = parseLL(numStr);
                }
                catch (const AssertionException&) {
                    return v8AssertionException(string("could not convert \"") +
                                                num +
                                                "\" to NumberLong");
                }
                unsigned long long val = n;
                // values above 2^53 are not accurately represented in JS
                if ((long long)val ==
                    (long long)(double)(long long)(val) && val < 9007199254740992ULL) {
                    it->ForceSet(scope->v8StringData("floatApprox"),
                            v8::Number::New((double)(long long)(val)));
                }
                else {
                    it->ForceSet(scope->v8StringData("floatApprox"),
                            v8::Number::New((double)(long long)(val)));
                    it->ForceSet(scope->v8StringData("top"), v8::Integer::New(val >> 32));
                    it->ForceSet(scope->v8StringData("bottom"),
                            v8::Integer::New((unsigned long)(val & 0x00000000ffffffff)));
                }
            }
        }
        else {
            it->ForceSet(scope->v8StringData("floatApprox"), args[0]->ToNumber());
            it->ForceSet(scope->v8StringData("top"), args[1]->ToUint32());
            it->ForceSet(scope->v8StringData("bottom"), args[2]->ToUint32());
        }
        return it;
    }

    long long numberLongVal(V8Scope* scope, const v8::Handle<v8::Object>& it) {
        verify(scope->NumberLongFT()->HasInstance(it));
        if (!it->Has(v8::String::New("top")))
            return (long long)(it->Get(v8::String::New("floatApprox"))->NumberValue());
        return
            (long long)
            ((unsigned long long)(it->Get(v8::String::New("top"))->ToInt32()->Value()) << 32) +
            (unsigned)(it->Get(v8::String::New("bottom"))->ToInt32()->Value());
    }

    v8::Handle<v8::Value> numberLongValueOf(V8Scope* scope, const v8::Arguments& args) {
        v8::Handle<v8::Object> it = args.This();
        long long val = numberLongVal(scope, it);
        return v8::Number::New(double(val));
    }

    v8::Handle<v8::Value> numberLongToNumber(V8Scope* scope, const v8::Arguments& args) {
        return numberLongValueOf(scope, args);
    }

    v8::Handle<v8::Value> numberLongToString(V8Scope* scope, const v8::Arguments& args) {
        v8::Handle<v8::Object> it = args.This();

        stringstream ss;
        long long val = numberLongVal(scope, it);
        const long long limit = 2LL << 30;

        if (val <= -limit || limit <= val)
            ss << "NumberLong(\"" << val << "\")";
        else
            ss << "NumberLong(" << val << ")";

        string ret = ss.str();
        return v8::String::New(ret.c_str());
    }

    v8::Handle<v8::Value> v8ObjectInvalidForStorage(V8Scope* scope, const v8::Arguments& args) {
        argumentCheck(args.Length() == 1, "invalidForStorage needs 1 argument")
        if (args[0]->IsNull()) {
            return v8::Null();
        }
        argumentCheck(args[0]->IsObject(), "argument to invalidForStorage has to be an object")
        Status validForStorage = scope->v8ToMongo(args[0]->ToObject()).storageValid(true);
        if (validForStorage.isOK()) {
            return v8::Null();
        }

        std::string errmsg = str::stream() << validForStorage.codeString()
                                           << ": "<< validForStorage.reason();
        return v8::String::New(errmsg.c_str());
    }

    v8::Handle<v8::Value> bsonsize(V8Scope* scope, const v8::Arguments& args) {
        argumentCheck(args.Length() == 1, "bsonsize needs 1 argument")
        if (args[0]->IsNull()) {
            return v8::Number::New(0);
        }
        argumentCheck(args[0]->IsObject(), "argument to bsonsize has to be an object")
        return v8::Number::New(scope->v8ToMongo(args[0]->ToObject()).objsize());
    }

    v8::Handle<v8::Value> bsonWoCompare(V8Scope* scope, const v8::Arguments& args) {
        argumentCheck(args.Length() == 2, "bsonWoCompare needs 2 argument");

        argumentCheck(args[0]->IsObject(), "first argument to bsonWoCompare has to be an object");
        argumentCheck(args[1]->IsObject(), "second argument to bsonWoCompare has to be an object");

        BSONObj firstObject(scope->v8ToMongo(args[0]->ToObject()));
        BSONObj secondObject(scope->v8ToMongo(args[1]->ToObject()));

        return v8::Number::New(firstObject.woCompare(secondObject));
    }

}
