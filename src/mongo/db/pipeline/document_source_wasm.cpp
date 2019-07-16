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

#include "mongo/platform/basic.h"

#include "mongo/db/pipeline/document_source_wasm.h"

#include "mongo/db/jsobj.h"
#include "mongo/db/pipeline/document.h"
#include "mongo/db/pipeline/expression.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/lite_parsed_document_source.h"
#include "mongo/db/pipeline/value.h"

namespace mongo {

using boost::intrusive_ptr;

DocumentSourceWasm::DocumentSourceWasm(const intrusive_ptr<ExpressionContext>& pExpCtx,
                                       ConstDataRange wasm)
    : DocumentSource(pExpCtx), _wasm(wasm) {}

REGISTER_DOCUMENT_SOURCE(wasm,
                         LiteParsedDocumentSourceDefault::parse,
                         DocumentSourceWasm::createFromBson);

constexpr StringData DocumentSourceWasm::kStageName;

DocumentSource::GetNextResult DocumentSourceWasm::getNext() {
    pExpCtx->checkForInterrupt();

    auto nextInput = pSource->getNext();
    return nextInput;
}

Value DocumentSourceWasm::serialize(boost::optional<ExplainOptions::Verbosity> explain) const {
    // FIXME: does _wasm.data() need to be copied?
    return Value(DOC(getSourceName()
                     << BSONBinData(_wasm.data(), _wasm.length(), BinDataType::BinDataGeneral)));
}

intrusive_ptr<DocumentSourceWasm> DocumentSourceWasm::create(
    const intrusive_ptr<ExpressionContext>& pExpCtx, ConstDataRange wasm) {
    intrusive_ptr<DocumentSourceWasm> source(new DocumentSourceWasm(pExpCtx, wasm));
    return source;
}

intrusive_ptr<DocumentSource> DocumentSourceWasm::createFromBson(
    BSONElement elem, const intrusive_ptr<ExpressionContext>& pExpCtx) {
    uassert(
        51242, "wasm be specified as binData type 0", elem.isBinData(BinDataType::BinDataGeneral));

    int binDataLen = 0;
    const char* binData = elem.binData(binDataLen);
    ConstDataRange wasm(binData, binDataLen);
    return DocumentSourceWasm::create(pExpCtx, std::move(wasm));
}
}  // namespace mongo
