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

#pragma once

#include "mongo/stdx/functional.h"

namespace mongo {

class BSONObj;
class BSONElement;

namespace mozjs {

class ASTType {
public:
    using Function = stdx::function<void()>;
    struct NullTag {};
    struct UndefinedTag {};

    enum class Type : uint8_t {
        None = 0,
        Boolean,
        Function,
        Null,
        Number,
        Object,
        String,
        Undefined,
    };

    ASTType();
    ASTType(const ASTType& rhs);
    ASTType& operator=(const ASTType& rhs);
    ASTType(ASTType&& rhs);
    ASTType& operator=(ASTType&& rhs);

    ~ASTType();

    explicit ASTType(bool b);
    explicit ASTType(Function function);
    explicit ASTType(NullTag);
    explicit ASTType(double number);
    explicit ASTType(BSONObj obj);
    explicit ASTType(std::string str);
    explicit ASTType(UndefinedTag);
    explicit ASTType(const BSONElement& ele);

    Type type() const;

    bool getBool() const;
    const Function& getFunction() const;
    double getNumber() const;
    const BSONObj& getObject() const;
    const std::string& getString() const;

    friend bool operator==(const ASTType& lhs, const ASTType& rhs);

private:
    void destroyMembers();

    union {
        bool _bool;
        double _number;
        std::string* _string;
        BSONObj* _object;
        Function* _function;
    };

    Type _type;
};

}  // namespace mozjs
}  // namespace mongo
