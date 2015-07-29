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

#include "mongo/scripting/mozjs/ast/ast_program.h"
#include "mongo/scripting/mozjs/objectwrapper.h"

namespace mongo {
namespace mozjs {

ASTProgram::ASTProgram(JSContext* cx, JS::HandleValue program) {
    JS::RootedValue body(cx);
    ObjectWrapper(cx, program).getValue("body", &body);
    ObjectWrapper o(cx, body);
    JS::RootedValue val(cx);

    o.enumerate([&](JS::HandleId id){
        o.getValue(id, &val);

        _body.push_back(AST::parse(cx, val));
    });
}

ASTType ASTProgram::compute(ComputeScope* scope) const {
    std::vector<ASTType> exprArg;

    ASTType result;
    for (auto&& expr : _body) {
        result = expr->compute(scope);
    }

    return std::move(result);
}

void ASTProgram::prettyPrint(std::ostream& os) const {
    os << "Program(";

    for (auto&& expr : _body) {
        expr->prettyPrint(os);
    }

    os << ")";
}

}  // namespace mozjs
}  // namespace mongo
