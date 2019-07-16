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

#include <stdio.h>
#include <stdlib.h>
#include "bson_iter.hpp"

#define CONSTEXPR
#define CONSTEXPR_OFF
#include "cexpr/bson.hpp"

extern "C" {

void returnValue(void*, size_t len);
size_t getInputSize(void);
int writeInputToLocation(void*);

int mysq(int n) {
    return n * n;
}

void mytransform() {
    size_t inputSize = getInputSize();
    char* buf = (char*)malloc(inputSize);
    writeInputToLocation(buf);

    bson_iter in((uint8_t*)buf);
    in.next();
    auto outBuf = (uint8_t*)malloc(1000);
    cexpr::bson out(outBuf);

    while (1) {
        printf("%s: %d\n", in.key(), in.type());
        if (strcmp(in.key(), "x") == 0) {
            out.append_int32("x", 1, in.int32()+1);
        } else {
            size_t size;
            auto ptr = in.keyAndValue(&size);
            out.append_slug(ptr, size);
        }

        if (!in.next()) {
            break;
        }
    }

    free(buf);

    returnValue((void*)out.data(), out.length());
    free(outBuf);
}

int myfilter() {
    size_t inputSize = getInputSize();
    char* buf = (char*)malloc(inputSize);
    writeInputToLocation(buf);

    if (buf[4+1] == 'x') {
        free(buf);
        return 1;
    } else {
        free(buf);
        return 0;
    }
}

}
