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
#include "mongo/db/pipeline/document_source_wasm_gen.h"

namespace mongo {

using boost::intrusive_ptr;

REGISTER_DOCUMENT_SOURCE(wasm,
                         DocumentSourceWasm::LiteParsed::parse,
                         DocumentSourceWasm::createFromBson);

const char* DocumentSourceWasm::kStageName = "$wasm";

intrusive_ptr<DocumentSource> DocumentSourceWasm::createFromBson(
    BSONElement spec, const intrusive_ptr<ExpressionContext>& pExpCtx) {
    return new DocumentSourceWasm(pExpCtx, WasmSpec::parse("$wasm"_sd, spec.Obj()));
}

intrusive_ptr<DocumentSourceWasm> DocumentSourceWasm::create(
    const intrusive_ptr<ExpressionContext>& pExpCtx, const WasmSpec& spec) {
    intrusive_ptr<DocumentSourceWasm> source(new DocumentSourceWasm(pExpCtx, spec));
    return source;
}

DocumentSource::GetNextResult DocumentSourceWasm::getNext() {
    pExpCtx->checkForInterrupt();

    // If the wasm module or our DocumentSource had already indicated that it had reached EOF,
    // simply return EOF.
    if (_eof) {
        return GetNextResult::makeEOF();
    }

    while (!_eof) {
        auto nextInput = pSource->getNext();
        if (nextInput.isPaused()) {
            return GetNextResult::makePauseExecution();
        }

        // If we receive EOF from the previous stage, give this information to the wasm module and
        // allow it to return a final document.
        _eof = nextInput.isEOF();

        // Construct the expected input document.
        boost::optional<BSONObj> inputDoc =
            _eof ? boost::optional<BSONObj>{} : nextInput.releaseDocument().toBson();
        InputSpec input{};
        input.setDoc(inputDoc);

        // Call the wasm module and parse the response.
        BSONObj result = _scope->transform("getNext", input.toBSON());
        ReturnSpec returned = ReturnSpec::parse("$wasm"_sd, result);

        auto nextDoc = returned.getNext_doc();

        // The wasm module can signify that it's done prior to the DocumentSource, e.g. if it's
        // imitating $limit.
        if (returned.getIs_eof()) {
            _eof = true;

            // If the module signified EOF without a final document to return, then return EOF.
            // Otherwise, fall through and return the final document. If this is the case the next
            // getNext() call will return EOF thanks to the initial guard on _eof.
            if (!nextDoc.is_initialized()) {
                return GetNextResult::makeEOF();
            }
        }

        // If the wasm module returned a document, pass it along.
        if (nextDoc.is_initialized()) {
            return Document(nextDoc.get());
        }
    }

    // We reach here if the wasm module was passed EOF and it did not signify EOF in its return
    // document.
    uasserted(51242, "$wasm module didn't terminate after EOF");
}

DocumentSourceWasm::DocumentSourceWasm(const intrusive_ptr<ExpressionContext>& pExpCtx,
                                       const WasmSpec& spec)
    : DocumentSource(pExpCtx),
      _spec(spec),
      _scope(WASMEngine::get().createScope(spec.getWasm())),
      _eof(false) {}

}  // namespace mongo
