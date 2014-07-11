#pragma once

#include "mongo/db/pipeline/accumulator.h"

namespace mongo {
    struct GroupOpDesc {
        const char* name;
        intrusive_ptr<Accumulator> (*factory)(const Value&);
    };
}
