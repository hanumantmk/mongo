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

#define CONSTEXPR
#define CONSTEXPR_OFF
#include "cexpr/bson.hpp"

extern "C" {

struct Magic {
    const char* str() {
        return my_str;
    };
    std::size_t str_len() {
        return strlen(my_str);
    };

    const char* my_str;
};

void* json2bson(const char* json) {
    auto outBuf = (uint8_t*)malloc(1000);

    printf("json: %s\n", json);

    Magic m{json};

    size_t len = cexpr::parse<cexpr::bson, Magic>(cexpr::bson(outBuf), m);
    printf("wrote: %ld\n", len);

    return outBuf;
}

}
