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
