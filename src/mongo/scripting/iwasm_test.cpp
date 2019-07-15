/**
 *    Copyright (C) 2019-present MongoDB, Inc.
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

#include "wasm_export.h"
#include "bh_memory.h"

#include "mongo/unittest/unittest.h"

namespace mongo {

unsigned char test_wasm[] = {
  0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x0d, 0x06, 0x64,
  0x79, 0x6c, 0x69, 0x6e, 0x6b, 0xf0, 0x21, 0x04, 0x00, 0x00, 0x00, 0x01,
  0x13, 0x04, 0x60, 0x01, 0x7f, 0x00, 0x60, 0x01, 0x7f, 0x01, 0x7f, 0x60,
  0x02, 0x7f, 0x7f, 0x01, 0x7f, 0x60, 0x00, 0x00, 0x02, 0x71, 0x07, 0x03,
  0x65, 0x6e, 0x76, 0x12, 0x61, 0x62, 0x6f, 0x72, 0x74, 0x53, 0x74, 0x61,
  0x63, 0x6b, 0x4f, 0x76, 0x65, 0x72, 0x66, 0x6c, 0x6f, 0x77, 0x00, 0x00,
  0x03, 0x65, 0x6e, 0x76, 0x05, 0x5f, 0x66, 0x72, 0x65, 0x65, 0x00, 0x00,
  0x03, 0x65, 0x6e, 0x76, 0x07, 0x5f, 0x6d, 0x61, 0x6c, 0x6c, 0x6f, 0x63,
  0x00, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x07, 0x5f, 0x70, 0x72, 0x69, 0x6e,
  0x74, 0x66, 0x00, 0x02, 0x03, 0x65, 0x6e, 0x76, 0x05, 0x5f, 0x70, 0x75,
  0x74, 0x73, 0x00, 0x01, 0x03, 0x65, 0x6e, 0x76, 0x0d, 0x5f, 0x5f, 0x6d,
  0x65, 0x6d, 0x6f, 0x72, 0x79, 0x5f, 0x62, 0x61, 0x73, 0x65, 0x03, 0x7f,
  0x00, 0x03, 0x65, 0x6e, 0x76, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79,
  0x02, 0x00, 0x01, 0x03, 0x03, 0x02, 0x02, 0x03, 0x06, 0x10, 0x03, 0x7f,
  0x01, 0x41, 0x00, 0x0b, 0x7f, 0x01, 0x41, 0x00, 0x0b, 0x7f, 0x00, 0x41,
  0x1b, 0x0b, 0x07, 0x25, 0x03, 0x12, 0x5f, 0x5f, 0x70, 0x6f, 0x73, 0x74,
  0x5f, 0x69, 0x6e, 0x73, 0x74, 0x61, 0x6e, 0x74, 0x69, 0x61, 0x74, 0x65,
  0x00, 0x06, 0x05, 0x5f, 0x6d, 0x61, 0x69, 0x6e, 0x00, 0x05, 0x04, 0x5f,
  0x73, 0x74, 0x72, 0x03, 0x03, 0x0a, 0xbf, 0x01, 0x02, 0xaa, 0x01, 0x01,
  0x01, 0x7f, 0x23, 0x01, 0x21, 0x00, 0x23, 0x01, 0x41, 0x10, 0x6a, 0x24,
  0x01, 0x23, 0x01, 0x23, 0x02, 0x4e, 0x04, 0x40, 0x41, 0x10, 0x10, 0x00,
  0x0b, 0x20, 0x00, 0x41, 0x08, 0x6a, 0x21, 0x02, 0x23, 0x00, 0x41, 0x1b,
  0x6a, 0x10, 0x04, 0x1a, 0x41, 0x80, 0x08, 0x10, 0x02, 0x21, 0x01, 0x20,
  0x01, 0x04, 0x7f, 0x20, 0x00, 0x20, 0x01, 0x36, 0x02, 0x00, 0x23, 0x00,
  0x20, 0x00, 0x10, 0x03, 0x1a, 0x20, 0x01, 0x23, 0x00, 0x2c, 0x00, 0x0d,
  0x3a, 0x00, 0x00, 0x20, 0x01, 0x23, 0x00, 0x2c, 0x00, 0x0e, 0x3a, 0x00,
  0x01, 0x20, 0x01, 0x23, 0x00, 0x2c, 0x00, 0x0f, 0x3a, 0x00, 0x02, 0x20,
  0x01, 0x23, 0x00, 0x2c, 0x00, 0x10, 0x3a, 0x00, 0x03, 0x20, 0x01, 0x23,
  0x00, 0x2c, 0x00, 0x11, 0x3a, 0x00, 0x04, 0x20, 0x01, 0x23, 0x00, 0x2c,
  0x00, 0x12, 0x3a, 0x00, 0x05, 0x20, 0x02, 0x20, 0x01, 0x36, 0x02, 0x00,
  0x23, 0x00, 0x41, 0x13, 0x6a, 0x20, 0x02, 0x10, 0x03, 0x1a, 0x20, 0x01,
  0x10, 0x01, 0x20, 0x00, 0x24, 0x01, 0x41, 0x00, 0x05, 0x23, 0x00, 0x41,
  0x28, 0x6a, 0x10, 0x04, 0x1a, 0x20, 0x00, 0x24, 0x01, 0x41, 0x7f, 0x0b,
  0x0b, 0x11, 0x00, 0x23, 0x00, 0x41, 0x40, 0x6b, 0x24, 0x01, 0x23, 0x01,
  0x41, 0x80, 0x20, 0x6a, 0x24, 0x02, 0x0b, 0x0b, 0x3f, 0x01, 0x00, 0x23,
  0x00, 0x0b, 0x39, 0x62, 0x75, 0x66, 0x20, 0x70, 0x74, 0x72, 0x3a, 0x20,
  0x25, 0x70, 0x0a, 0x00, 0x31, 0x32, 0x33, 0x34, 0x0a, 0x00, 0x62, 0x75,
  0x66, 0x3a, 0x20, 0x25, 0x73, 0x00, 0x48, 0x65, 0x6c, 0x6c, 0x6f, 0x20,
  0x77, 0x6f, 0x72, 0x6c, 0x64, 0x21, 0x00, 0x6d, 0x61, 0x6c, 0x6c, 0x6f,
  0x63, 0x20, 0x62, 0x75, 0x66, 0x20, 0x66, 0x61, 0x69, 0x6c, 0x65, 0x64,
  0x00, 0x57, 0x04, 0x6e, 0x61, 0x6d, 0x65, 0x01, 0x50, 0x07, 0x00, 0x12,
  0x61, 0x62, 0x6f, 0x72, 0x74, 0x53, 0x74, 0x61, 0x63, 0x6b, 0x4f, 0x76,
  0x65, 0x72, 0x66, 0x6c, 0x6f, 0x77, 0x01, 0x05, 0x5f, 0x66, 0x72, 0x65,
  0x65, 0x02, 0x07, 0x5f, 0x6d, 0x61, 0x6c, 0x6c, 0x6f, 0x63, 0x03, 0x07,
  0x5f, 0x70, 0x72, 0x69, 0x6e, 0x74, 0x66, 0x04, 0x05, 0x5f, 0x70, 0x75,
  0x74, 0x73, 0x05, 0x05, 0x5f, 0x6d, 0x61, 0x69, 0x6e, 0x06, 0x12, 0x5f,
  0x5f, 0x70, 0x6f, 0x73, 0x74, 0x5f, 0x69, 0x6e, 0x73, 0x74, 0x61, 0x6e,
  0x74, 0x69, 0x61, 0x74, 0x65, 0x00, 0x1f, 0x10, 0x73, 0x6f, 0x75, 0x72,
  0x63, 0x65, 0x4d, 0x61, 0x70, 0x70, 0x69, 0x6e, 0x67, 0x55, 0x52, 0x4c,
  0x0d, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x77, 0x61, 0x73, 0x6d, 0x2e, 0x6d,
  0x61, 0x70
};
unsigned int test_wasm_len = 602;


TEST(IWasmTest, Basic) {
    static char global_heap_buf[512 * 1024];

    uint8_t * buffer;
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_function_inst_t func;
    wasm_exec_env_t env;
//    uint32_t argv[2];

    bh_memory_init_with_pool(global_heap_buf, sizeof(global_heap_buf));
    wasm_runtime_init();

    buffer = reinterpret_cast<uint8_t*>(test_wasm);
    size_t size = test_wasm_len;
    char err[100];
    module = wasm_runtime_load(buffer, size, err, sizeof(err));
    inst = wasm_runtime_instantiate(module, 0, 0, err, sizeof(err));
//    func = wasm_runtime_lookup_function(inst, "main", "(i32)i32");
    env = wasm_runtime_create_exec_env(8192);

//    argv[0] = 8;
//    if (!wasm_runtime_call_wasm(inst, env, func, 1, argv)) {
//        wasm_runtime_clear_exception(inst);
//    }
//    /* the return value is stored in argv[0] */
//    printf("fib function return: %d\n", argv[0]);
    char p1[] = "hello_world";
    char* argv[] = {p1};
    wasm_application_execute_main(inst, 1, argv);

    wasm_runtime_destory_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
    wasm_runtime_destroy();
    bh_memory_destroy();
}

}  // namespace mongo
