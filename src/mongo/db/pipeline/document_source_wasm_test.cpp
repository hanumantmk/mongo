/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include <cstdint>

#include "mongo/platform/basic.h"

#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/pipeline/aggregation_context_fixture.h"
#include "mongo/db/pipeline/dependencies.h"
#include "mongo/db/pipeline/document_source_mock.h"
#include "mongo/db/pipeline/document_source_wasm.h"
#include "mongo/db/pipeline/document_source_wasm_gen.h"
#include "mongo/db/pipeline/document_value_test_util.h"
#include "mongo/db/pipeline/pipeline.h"
#include "mongo/scripting/limit_wasm.h"
#include "mongo/scripting/passthrough_wasm.h"
#include "mongo/scripting/project_wasm.h"
#include "mongo/scripting/stats_wasm.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace {

// This provides access to getExpCtx(), but we'll use a different name for this test suite.
using DocumentSourceWasmTest = AggregationContextFixture;

TEST_F(DocumentSourceWasmTest, PassThrough) {
    auto source = DocumentSourceMock::createForTest({"{a: 1}", "{a: 2}"});
    std::vector<uint8_t> limit_bytes(passthrough_wasm, passthrough_wasm + passthrough_wasm_len);
    WasmSpec spec(std::move(limit_bytes));

    auto wasm = DocumentSourceWasm::create(getExpCtx(), spec);
    wasm->setSource(source.get());

    // The wasm's result is as expected.
    auto next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT_VALUE_EQ(Value(1), next.getDocument().getField("a"));

    next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT_VALUE_EQ(Value(2), next.getDocument().getField("a"));

    // The wasm is exhausted.
    ASSERT(wasm->getNext().isEOF());
}

TEST_F(DocumentSourceWasmTest, Project) {
    auto source = DocumentSourceMock::createForTest({"{foo: 1, bar: 2}", "{bar: 2}"});
    std::vector<uint8_t> limit_bytes(project_wasm, project_wasm + project_wasm_len);
    WasmSpec spec(std::move(limit_bytes));

    auto wasm = DocumentSourceWasm::create(getExpCtx(), spec);
    wasm->setSource(source.get());

    // The wasm's result is as expected.
    auto next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT(next.getDocument().getField("foo").missing());
    ASSERT_VALUE_EQ(Value(2), next.getDocument().getField("bar"));

    next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT_VALUE_EQ(Value(2), next.getDocument().getField("bar"));

    // The wasm is exhausted.
    ASSERT(wasm->getNext().isEOF());
}

TEST_F(DocumentSourceWasmTest, Limit) {
    auto source = DocumentSourceMock::createForTest({"{a: 1}", "{a: 2}", "{a: 3}"});
    std::vector<uint8_t> limit_bytes(limit_wasm, limit_wasm + limit_wasm_len);
    WasmSpec spec(std::move(limit_bytes));

    auto wasm = DocumentSourceWasm::create(getExpCtx(), spec);
    wasm->setSource(source.get());

    // The wasm's result is as expected.
    auto next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT_VALUE_EQ(Value(1), next.getDocument().getField("a"));

    next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT_VALUE_EQ(Value(2), next.getDocument().getField("a"));

    // The wasm is exhausted.
    ASSERT(wasm->getNext().isEOF());
}

TEST_F(DocumentSourceWasmTest, Stats) {
    auto source = DocumentSourceMock::createForTest(
        {"{name: \"Younger Person\", age: 25, email_address: \"young@mongodb.com\"}",
         "{name: \"Older Person\", age: 50, email_address: \"old@mongodb.com\"}",
         "{name: \"Middle Person\", age: 25, email_address: \"middle@mongodb.com\"}"});
    std::vector<uint8_t> limit_bytes(stats_wasm, stats_wasm + stats_wasm_len);
    WasmSpec spec(std::move(limit_bytes));

    auto wasm = DocumentSourceWasm::create(getExpCtx(), spec);
    wasm->setSource(source.get());

    // The wasm's result is as expected.
    auto next = wasm->getNext();
    ASSERT(next.isAdvanced());
    ASSERT_VALUE_EQ(Value(3), next.getDocument().getField("num_users"));
    ASSERT_VALUE_EQ(Value("old@mongodb.com"_sd),
                    next.getDocument().getField("oldest_user_email_address"));

    // The wasm is exhausted.
    ASSERT(wasm->getNext().isEOF());
}

}  // namespace
}  // namespace mongo
