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

#include "mongo/db/jsobj.h"
#include "mongo/scripting/mozjs/ast/ast_member_expression.h"
#include "mongo/scripting/mozjs/objectwrapper.h"

namespace mongo {
namespace mozjs {

ASTMemberExpression::ASTMemberExpression(JSContext* cx, JS::HandleValue expr) {
    JS::RootedValue object(cx);
    JS::RootedValue property(cx);
    ObjectWrapper o(cx, expr);
    
    o.getValue("object", &object);
    o.getValue("property", &property);

    _object = AST::parse(cx, object);
    _property = AST::parse(cx, property);
}

ASTType ASTMemberExpression::compute(ComputeScope* scope) const {
    ASTType property = _property->compute(scope);
    ASTType object = _object->compute(scope);

    return ASTType(object.getObject()[property.getString()]);
}

void ASTMemberExpression::prettyPrint(std::ostream& os) const {
    os << "MemberExpression(";
    _object->prettyPrint(os);
    os << ", ";
    _property->prettyPrint(os);
    os << ")";
}

}  // namespace mozjs
}  // namespace mongo
