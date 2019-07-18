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

#define DUK_SINGLE_FILE
#include "duk_config.h"
//#define DUK_USE_DEBUG 1
//#define DUK_USE_DEBUG_WRITE(...) \
//    printf("DUK\t%ld\t%s\t%ld\t%s\t%s\n", __VA_ARGS__);

#define DUK_USE_DATE_GET_NOW(ctx) 0
#define DUK_USE_DATE_GET_LOCAL_TZOFFSET(d) 0
#include "duktape.c"

int mysq(int n) {
    return n * n;
}

void fatal_handler(void*udata, const char* msg) {
    printf("fatal handler - %s\n", msg);
}

void* mytransform(void* buf) {
    printf("mytransform 1\n");
    duk_context* ctx = duk_create_heap(NULL, NULL, NULL, NULL, fatal_handler);
    printf("mytransform 2\n");

    int r = duk_peval_string(ctx, "\"hello from js\";");
    printf("r = %d\n", r);
//    const char* result = duk_get_string(ctx, -1);
//
//    printf("%s\n", result);
//
//    duk_pop(ctx);
//
//    duk_destroy_heap(ctx);

    return buf;
}

int myfilter(void* buf) {
    return 1;
}

