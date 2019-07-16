/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/unittest/unittest.h"

#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobjbuilder.h"
#include "mongo/scripting/iwasm_wasm.h"
#include "mongo/scripting/wasm_engine.h"

namespace mongo {

TEST(IWasmTest, Func) {
    auto scope = WASMEngine::get().createScope(ConstDataRange(test_wasm, test_wasm_len));

    std::vector<uint32_t> args{8};

    scope->callStr("_mysq", "(i32)i32", args);

    std::cout << "mysq function returned: " << args[0] << "\n";
}

TEST(IWasmTest, FuncWithTransform) {
    auto scope = WASMEngine::get().createScope(ConstDataRange(test_wasm, test_wasm_len));

    auto out = scope->transform("_mytransform", BSON("x" << 1));

    std::cout << "mytransform function returned: " << out << "\n";
}

TEST(IWasmTest, FuncWithFilter) {
    auto scope = WASMEngine::get().createScope(ConstDataRange(test_wasm, test_wasm_len));

    auto out = scope->filter("_myfilter", BSON("x" << 1));

    std::cout << "myfilter function returned: " << out << "\n";

    out = scope->filter("_myfilter", BSON("y" << 1));

    std::cout << "myfilter function returned: " << out << "\n";
}

}  // namespace mongo
