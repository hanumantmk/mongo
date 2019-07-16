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

#include <vector>

#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/document_source_wasm_gen.h"
#include "mongo/db/pipeline/lite_parsed_document_source.h"
#include "mongo/scripting/wasm_engine.h"

namespace mongo {

/**
 * Produces one document per session in the local cache if 'allUsers' is specified
 * as true, and returns just sessions for the currently logged in user if
 * 'allUsers' is specified as false, or not specified at all.
 */
class DocumentSourceWasmt final : public DocumentSource {
public:
    static const char* kStageName;

    class LiteParsed final : public LiteParsedDocumentSource {
    public:
        static std::unique_ptr<LiteParsed> parse(const AggregationRequest& request,
                                                 const BSONElement& spec) {

            return std::make_unique<LiteParsed>(WasmtSpec::parse("$wasmt"_sd, spec.Obj()));
        }

        explicit LiteParsed(const WasmtSpec& spec) : _spec(spec) {}

        stdx::unordered_set<NamespaceString> getInvolvedNamespaces() const final {
            return stdx::unordered_set<NamespaceString>();
        }

        PrivilegeVector requiredPrivileges(bool isMongos) const final {
            return {};
        }

    private:
        const WasmtSpec _spec;
    };

    GetNextResult getNext() final;

    const char* getSourceName() const final {
        return kStageName;
    }

    Value serialize(boost::optional<ExplainOptions::Verbosity> explain = boost::none) const final {
        return Value(Document{{getSourceName(), _spec.toBSON()}});
    }

    StageConstraints constraints(Pipeline::SplitState pipeState) const final {
        StageConstraints constraints(StreamType::kStreaming,
                                     PositionRequirement::kNone,
                                     HostTypeRequirement::kNone,
                                     DiskUseRequirement::kNoDiskUse,
                                     FacetRequirement::kNotAllowed,
                                     TransactionRequirement::kNotAllowed,
                                     LookupRequirement::kAllowed);
        return constraints;
    }

    boost::optional<DistributedPlanLogic> distributedPlanLogic() final {
        return boost::none;
    }

    static boost::intrusive_ptr<DocumentSource> createFromBson(
        BSONElement elem, const boost::intrusive_ptr<ExpressionContext>& pExpCtx);

private:
    DocumentSourceWasmt(const boost::intrusive_ptr<ExpressionContext>& pExpCtx,
                        const WasmtSpec& spec);

    const WasmtSpec _spec;
    std::unique_ptr<WASMEngine::Scope> _scope;
};

}  // namespace mongo
