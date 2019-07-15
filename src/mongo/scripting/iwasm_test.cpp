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

#include "mongo/scripting/iwasm_wasm.h"

namespace mongo {


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

TEST(IWasmTest, Func) {
    static char global_heap_buf[512 * 1024];

    uint8_t * buffer;
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_function_inst_t func;
    wasm_exec_env_t env;
    uint32_t argv[2];

    bh_memory_init_with_pool(global_heap_buf, sizeof(global_heap_buf));
    wasm_runtime_init();

    buffer = reinterpret_cast<uint8_t*>(test_wasm);
    size_t size = test_wasm_len;
    char err[100];
    module = wasm_runtime_load(buffer, size, err, sizeof(err));
    inst = wasm_runtime_instantiate(module, 0, 0, err, sizeof(err));
    func = wasm_runtime_lookup_function(inst, "_mysqrt", "(i32)i32");
    env = wasm_runtime_create_exec_env(8192);

    argv[0] = 8;
    if (!wasm_runtime_call_wasm(inst, env, func, 1, argv)) {
        wasm_runtime_clear_exception(inst);
    }
    /* the return value is stored in argv[0] */
    printf("fib function return: %d\n", argv[0]);

    wasm_runtime_destory_exec_env(env);
    wasm_runtime_deinstantiate(inst);
    wasm_runtime_unload(module);
    wasm_runtime_destroy();
    bh_memory_destroy();
}

}  // namespace mongo
