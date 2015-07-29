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

#include "mongo/scripting/mozjs/ast/ast.h"

#include "mongo/scripting/mozjs/ast/ast_binary_expression.h"
#include "mongo/scripting/mozjs/ast/ast_expression_statement.h"
#include "mongo/scripting/mozjs/ast/ast_identifier.h"
#include "mongo/scripting/mozjs/ast/ast_member_expression.h"
#include "mongo/scripting/mozjs/ast/ast_program.h"
#include "mongo/scripting/mozjs/ast/ast_this_expression.h"
#include "mongo/scripting/mozjs/objectwrapper.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/assert_util.h"

namespace mongo {
namespace mozjs {

std::unique_ptr<AST> AST::parse(JSContext* cx, JS::HandleValue parse) {
    ObjectWrapper o(cx, parse);

    std::string type = o.getString("type");

    if (type.compare("Program") == 0) {
        return stdx::make_unique<ASTProgram>(cx, parse);
    } else if (type.compare("BinaryExpression") == 0) {
        return stdx::make_unique<ASTBinaryExpression>(cx, parse);
    } else if (type.compare("ExpressionStatement") == 0) {
        return stdx::make_unique<ASTExpressionStatement>(cx, parse);
    } else if (type.compare("Identifier") == 0) {
        return stdx::make_unique<ASTIdentifier>(cx, parse);
    } else if (type.compare("MemberExpression") == 0) {
        return stdx::make_unique<ASTMemberExpression>(cx, parse);
    } else if (type.compare("ThisExpression") == 0) {
        return stdx::make_unique<ASTThisExpression>(cx, parse);
    }

    uasserted(ErrorCodes::BadValue, "couldn't parse");
}


}  // namespace mozjs
}  // namespace mongo
