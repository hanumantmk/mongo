#pragma once

#include "mongo/db/jsobj.h"
#include "mongo/db/pipeline/accumulator.h"
#include "mongo/db/pipeline/document.h"
#include "mongo/db/pipeline/document_source.h"
#include "mongo/db/pipeline/expression.h"
#include "mongo/db/pipeline/expression_context.h"
#include "mongo/db/pipeline/value.h"
#include "mongo/base/init.h"

namespace mongo {
    struct GroupOpDesc {
        const char* name;
        intrusive_ptr<Accumulator> (*factory)();
    };
}
