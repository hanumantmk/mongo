/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "bson_iter.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string>

#define CONSTEXPR
#define CONSTEXPR_OFF
#include "cexpr/bson.hpp"

extern "C" {

void* getNext(void* buf) {
    uint32_t len;
    memcpy(&len, buf, 4);

    auto outBuf = (uint8_t*)malloc(1000);
    cexpr::bson out(outBuf);
    cexpr::bson sub;

    const char* str = "hello from C++!";

    out.append_bool("is_eof", 6, len==5);

    if (len != 5) {
        out.append_document_begin("next_doc", 8, sub);
        sub.append_utf8("msg", 3, "hello from C++", strlen(str)-1);
        out.append_document_end(sub);
    }

    free(buf);

    return outBuf;
}

}
