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
#include "mongo/scripting/limit_wasm.h"
#include "mongo/scripting/passthrough_wasm.h"
#include "mongo/scripting/project_wasm.h"
#include "mongo/scripting/stats_wasm.h"
#include "mongo/scripting/wasm_engine.h"

namespace mongo {

TEST(IWasmTest, PassthroughAggPipelineStage) {
    auto scope =
        WASMEngine::get().createScope(ConstDataRange(passthrough_wasm, passthrough_wasm_len));

    auto out = scope->transform("getNext", BSON("doc" << BSON("foo" << 1)));
    std::cout << "passthrough returned: " << out << "\n";

    out = scope->transform("getNext", BSON("doc" << BSON("bar" << 1)));
    std::cout << "passthrough returned: " << out << "\n";

    out = scope->transform("getNext", BSONObj{});
    std::cout << "passthrough returned: " << out << "\n";
}

TEST(IWasmTest, ProjectAggPipelineStage) {
    auto scope = WASMEngine::get().createScope(ConstDataRange(project_wasm, project_wasm_len));

    auto out = scope->transform("getNext", BSON("doc" << BSON("foo" << 1)));
    std::cout << "project returned: " << out << "\n";

    out = scope->transform("getNext", BSON("doc" << BSON("bar" << 1)));
    std::cout << "project returned: " << out << "\n";

    out = scope->transform("getNext", BSONObj{});
    std::cout << "project returned: " << out << "\n";
}

TEST(IWasmTest, LimitAggPipelineStage) {
    auto scope = WASMEngine::get().createScope(ConstDataRange(limit_wasm, limit_wasm_len));

    auto out = scope->transform("getNext", BSON("doc" << BSON("foo" << 1)));
    std::cout << "limit returned: " << out << "\n";

    out = scope->transform("getNext", BSON("doc" << BSON("bar" << 1)));
    std::cout << "limit returned: " << out << "\n";

    out = scope->transform("getNext", BSONObj{});
    std::cout << "limit returned: " << out << "\n";
}

TEST(IWasmTest, StatsAggPipelineStage) {
    auto scope = WASMEngine::get().createScope(ConstDataRange(stats_wasm, stats_wasm_len));

    auto out = scope->transform("getNext",
                                BSON("doc" << BSON("name"
                                                   << "Younger Person"
                                                   << "age" << 25 << "email_address"
                                                   << "young@mongodb.com")));
    std::cout << "stats returned: " << out << "\n";

    out = scope->transform("getNext",
                           BSON("doc" << BSON("name"
                                              << "Older Person"
                                              << "age" << 75 << "email_address"
                                              << "older@mongodb.com")));
    std::cout << "stats returned: " << out << "\n";

    out = scope->transform("getNext",
                           BSON("doc" << BSON("name"
                                              << "Middle Person"
                                              << "age" << 50 << "email_address"
                                              << "middle@mongodb.com")));
    std::cout << "stats returned: " << out << "\n";

    out = scope->transform("getNext", BSONObj{});
    std::cout << "stats returned: " << out << "\n";
}

}  // namespace mongo
