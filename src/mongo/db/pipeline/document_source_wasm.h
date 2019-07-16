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

#pragma once

#include "mongo/platform/basic.h"

#include "mongo/db/pipeline/document_source.h"

namespace mongo {

class DocumentSourceWasm final : public DocumentSource {
public:
    static constexpr StringData kStageName = "$wasm"_sd;

    /**
     * Create a new $wasm stage.
     */
    static boost::intrusive_ptr<DocumentSourceWasm> create(
        const boost::intrusive_ptr<ExpressionContext>& pExpCtx, ConstDataRange wasm);

    /**
     * Parse a $wasm stage from a BSON stage specification. 'elem's field name must be "$wasm".
     */
    static boost::intrusive_ptr<DocumentSource> createFromBson(
        BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx);

    StageConstraints constraints(Pipeline::SplitState pipeState) const final {
        return {StreamType::kBlocking,  // May or may not be blocking, we don't know.
                PositionRequirement::kNone,
                HostTypeRequirement::kNone,
                DiskUseRequirement::kNoDiskUse,
                FacetRequirement::kNotAllowed,
                TransactionRequirement::kNotAllowed,
                LookupRequirement::kNotAllowed};
    }

    GetNextResult getNext() final;
    const char* getSourceName() const final {
        return kStageName.rawData();
    }

    DepsTracker::State getDependencies(DepsTracker* deps) const final {
        return DepsTracker::State::NOT_SUPPORTED;
    }

    Value serialize(boost::optional<ExplainOptions::Verbosity> explain = boost::none) const final;

    boost::optional<DistributedPlanLogic> distributedPlanLogic() final {
        // FIXME: this seem right?
        return DistributedPlanLogic{nullptr, nullptr, boost::none};
    }

private:
    DocumentSourceWasm(const boost::intrusive_ptr<ExpressionContext>& pExpCtx, ConstDataRange wasm);

    ConstDataRange _wasm;
};

}  // namespace mongo
