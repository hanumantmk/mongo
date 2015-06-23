/**
 * Copyright (C) 2015 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/scripting/mozjs/valuereader.h"

#define MONGO_LOG_DEFAULT_COMPONENT ::mongo::logger::LogComponent::kQuery

#include "mongo/platform/basic.h"

#include "mongo/scripting/mozjs/valuereader.h"

#include "mongo/base/error_codes.h"
#include "mongo/scripting/mozjs/implscope.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/util/base64.h"
#include "mongo/util/log.h"

namespace mongo {
namespace mozjs {

ValueReader::ValueReader(JSContext* cx, JS::MutableHandleValue value, int depth)
    : _context(cx), _value(value), _depth(depth) {}

void ValueReader::fromBSONElement(const BSONElement& elem, bool readOnly) {
    auto scope = getScope(_context);

    switch (elem.type()) {
        case mongo::Code:
            scope->newFunction(elem.valueStringData(), _value);
            return;
        case mongo::CodeWScope:
            if (!elem.codeWScopeObject().isEmpty())
                warning() << "CodeWScope doesn't transfer to db.eval";
            scope->newFunction(StringData(elem.codeWScopeCode(), elem.codeWScopeCodeLen() - 1),
                               _value);
            return;
        case mongo::Symbol:
        case mongo::String:
            fromStringData(elem.valueStringData());
            return;
        case mongo::jstOID: {
            JS::AutoValueArray<1> args(_context);

            ValueReader(_context, args[0]).fromStringData(elem.OID().toString());

            scope->getOidProto().newInstance(args, _value);
            return;
        }
        case mongo::NumberDouble:
            _value.setDouble(elem.Number());
            return;
        case mongo::NumberInt:
            _value.setInt32(elem.Int());
            return;
        case mongo::Array: {
            JS::RootedObject array(_context, JS_NewArrayObject(_context, 0));
            int i = 0;
            BSONForEach(subElem, elem.embeddedObject()) {
                char str[20];
                snprintf(str, sizeof(str), "%i", i++);
                JS::RootedValue member(_context);

                ValueReader(_context, &member, _depth + 1).fromBSONElement(subElem, readOnly);
                ObjectWrapper(_context, array, _depth + 1).setValue(str, member);
            }
            _value.setObjectOrNull(array);
            return;
        }
        case mongo::Object:
            fromBSON(elem.embeddedObject(), readOnly);
            return;
        case mongo::Date:
            _value.setObjectOrNull(
                JS_NewDateObjectMsec(_context, elem.Date().toMillisSinceEpoch()));
            return;
        case mongo::Bool:
            _value.setBoolean(elem.Bool());
            return;
        case mongo::EOO:
        case mongo::jstNULL:
        case mongo::Undefined:  // duplicate sm behavior
            _value.setNull();
            return;
        case mongo::RegEx: {
            // TODO parse into a custom type that can support any patterns and flags SERVER-9803

            JS::AutoValueArray<2> args(_context);

            ValueReader(_context, args[0]).fromStringData(elem.regex());
            ValueReader(_context, args[1]).fromStringData(elem.regexFlags());

            JS::RootedObject obj(_context);
            scope->getRegExpProto().newInstance(args, &obj);

            _value.setObjectOrNull(obj);

            return;
        }
        case mongo::BinData: {
            int len;
            const char* data = elem.binData(len);
            std::stringstream ss;
            base64::encode(ss, data, len);

            JS::AutoValueArray<2> args(_context);

            args[0].setInt32(elem.binDataType());

            ValueReader(_context, args[1]).fromStringData(ss.str());

            scope->getBinDataProto().newInstance(args, _value);
            return;
        }
        case mongo::bsonTimestamp: {
            JS::AutoValueArray<2> args(_context);

            args[0].setDouble(elem.timestampTime().toMillisSinceEpoch() / 1000);
            args[1].setNumber(elem.timestampInc());

            scope->getTimestampProto().newInstance(args, _value);

            return;
        }
        case mongo::NumberLong: {
            unsigned long long nativeUnsignedLong = elem.numberLong();
            // values above 2^53 are not accurately represented in JS
            if (static_cast<long long>(nativeUnsignedLong) ==
                    static_cast<long long>(
                        static_cast<double>(static_cast<long long>(nativeUnsignedLong))) &&
                nativeUnsignedLong < 9007199254740992ULL) {
                JS::AutoValueArray<1> args(_context);
                args[0].setNumber(static_cast<double>(static_cast<long long>(nativeUnsignedLong)));

                scope->getNumberLongProto().newInstance(args, _value);
            } else {
                JS::AutoValueArray<3> args(_context);
                args[0].setNumber(static_cast<double>(static_cast<long long>(nativeUnsignedLong)));
                args[1].setDouble(nativeUnsignedLong >> 32);
                args[2].setDouble(
                    static_cast<unsigned long>(nativeUnsignedLong & 0x00000000ffffffff));
                scope->getNumberLongProto().newInstance(args, _value);
            }

            return;
        }
        case mongo::MinKey:
            scope->getMinKeyProto().newInstance(_value);
            return;
        case mongo::MaxKey:
            scope->getMaxKeyProto().newInstance(_value);
            return;
        case mongo::DBRef: {
            JS::AutoValueArray<1> oidArgs(_context);
            ValueReader(_context, oidArgs[0]).fromStringData(elem.dbrefOID().toString());

            JS::AutoValueArray<2> dbPointerArgs(_context);
            ValueReader(_context, dbPointerArgs[0]).fromStringData(elem.dbrefNS());
            scope->getOidProto().newInstance(oidArgs, dbPointerArgs[1]);

            scope->getDbPointerProto().newInstance(dbPointerArgs, _value);
            return;
        }
        default:
            massert(16661,
                    str::stream() << "can't handle type: " << elem.type() << " " << elem.toString(),
                    false);
            break;
    }

    _value.setUndefined();
}

void ValueReader::fromBSON(const BSONObj& obj, bool readOnly) {
    if (obj.firstElementType() == String && str::equals(obj.firstElementFieldName(), "$ref")) {
        BSONObjIterator it(obj);
        const BSONElement ref = it.next();
        const BSONElement id = it.next();

        if (id.ok() && str::equals(id.fieldName(), "$id")) {
            JS::AutoValueArray<2> args(_context);

            ValueReader(_context, args[0]).fromBSONElement(ref, readOnly);
            ValueReader(_context, args[1]).fromBSONElement(id, readOnly);

            JS::RootedObject obj(_context);

            auto scope = getScope(_context);

            scope->getDbRefProto().newInstance(args, &obj);
            ObjectWrapper o(_context, obj);

            while (it.more()) {
                BSONElement elem = it.next();
                o.setBSONElement(elem.fieldName(), elem, readOnly);
            }

            _value.setObjectOrNull(obj);
            return;
        }
    }

    JS::RootedObject child(_context);
    BSONInfo::make(_context, &child, obj, readOnly);

    _value.setObjectOrNull(child);
}

void ValueReader::fromStringData(StringData sd) {
    auto jsStr = JS_NewStringCopyN(_context, sd.rawData(), sd.size());

    uassert(ErrorCodes::JSInterpreterFailure,
            str::stream() << "Unable to copy \"" << sd << "\" into MozJS",
            jsStr);

    _value.setString(jsStr);
}

}  // namespace mozjs
}  // namespace mongo
