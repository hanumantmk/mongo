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

void returnValue(void*, size_t len);
size_t getInputSize(void);
int writeInputToLocation(void*);

int mysq(int n) {
    return n * n;
}

void mybson() {
    size_t inputSize = getInputSize();
    char* buf = malloc(inputSize);
    writeInputToLocation(buf);
    buf[4+1]++;
    returnValue(buf, inputSize);
    free(buf);
}
