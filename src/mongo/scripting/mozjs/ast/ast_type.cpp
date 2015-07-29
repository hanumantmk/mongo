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

#include "mongo/scripting/mozjs/ast/ast_type.h"

#include <iostream>

#include "mongo/db/jsobj.h"

namespace mongo {
namespace mozjs {

ASTType::ASTType() :
    _type(Type::None)
    {}

ASTType::ASTType(ASTType&& rhs) :
    _type(Type::None)
{
    *this = std::move(rhs);
}

ASTType::ASTType(const ASTType& rhs) :
    _type(Type::None)
{
    *this = rhs;
}

ASTType& ASTType::operator=(const ASTType& rhs) {
    destroyMembers();

    _type = rhs._type;

    switch (rhs._type) {
        case Type::None:
        case Type::Undefined:
        case Type::Null:
            break;
        case Type::Number:
            _number = rhs._number;
            break;
        case Type::Boolean:
            _bool = rhs._bool;
            break;
        case Type::String:
            _string = new std::string(*rhs._string);
            break;
        case Type::Function:
            _function = new stdx::function<void()>(*rhs._function);
            break;
        case Type::Object:
            _object = new BSONObj(*rhs._object);
            break;
    }

    return *this;
}

ASTType& ASTType::operator=(ASTType&& rhs) {
    destroyMembers();

    _type = rhs._type;

    switch (rhs._type) {
        case Type::None:
        case Type::Undefined:
        case Type::Null:
            break;
        case Type::Number:
            _number = rhs._number;
            break;
        case Type::Boolean:
            _bool = rhs._bool;
            break;
        case Type::String:
            _string = rhs._string;
            break;
        case Type::Function:
            _function = rhs._function;
            break;
        case Type::Object:
            _object = rhs._object;
            break;
    }

    rhs._type = Type::None;

    return *this;
}

ASTType::ASTType(bool b) :
    _bool(b),
    _type(Type::Boolean)
    {}

ASTType::ASTType(Function function) :
    _function(new Function(std::move(function))),
    _type(Type::Function)
    {}

ASTType::ASTType(NullTag) :
    _type(Type::Null)
    {}

ASTType::ASTType(BSONObj obj) :
    _object(new BSONObj(std::move(obj))),
    _type(Type::Object)
    {}

ASTType::ASTType(const BSONElement& ele) {
    if (ele.isNumber()) {
        _number = ele.numberDouble();
        _type = Type::Number;
    } else {
        std::cerr << "ele: " << ele << "\n";
        _type = Type::None;
    }
}

ASTType::ASTType(std::string str) :
    _string(new std::string(std::move(str))),
    _type(Type::String)
    {}

ASTType::ASTType(double number) :
    _number(number),
    _type(Type::Number)
    {}

ASTType::ASTType(UndefinedTag) :
    _type(Type::Undefined)
    {}

ASTType::~ASTType() {
    destroyMembers();
}

ASTType::Type ASTType::type() const {
    return _type;
}

bool ASTType::getBool() const {
    return _bool;
}

const ASTType::Function& ASTType::getFunction() const {
    return *_function;
}

double ASTType::getNumber() const {
    return _number;
}

const BSONObj& ASTType::getObject() const {
    return *_object;
}

const std::string& ASTType::getString() const {
    return *_string;
}

bool operator==(const ASTType& lhs, const ASTType& rhs) {
    if (lhs.type() != rhs.type()) {
        return false;
    } else {
        switch (lhs.type()) {
            case ASTType::Type::None:
            case ASTType::Type::Undefined:
            case ASTType::Type::Null:
                return true;
            case ASTType::Type::Number:
                return lhs._number == rhs._number;
            case ASTType::Type::Boolean:
                return lhs._bool == rhs._bool;
            case ASTType::Type::String:
                return lhs._string->compare(*rhs._string) == 0;
            case ASTType::Type::Function:
                return lhs._function == rhs._function;
            case ASTType::Type::Object:
                return *lhs._object == *rhs._object;
        }
    }
}

void ASTType::destroyMembers() {
    switch (_type) {
        case Type::None:
        case Type::Undefined:
        case Type::Number:
        case Type::Boolean:
        case Type::Null:
            break;
        case Type::String:
            delete _string;
            break;
        case Type::Function:
            delete _function;
            break;
        case Type::Object:
            delete _object;
            break;
    }

    _type = Type::None;
}


}  // namespace mozjs
}  // namespace mongo
