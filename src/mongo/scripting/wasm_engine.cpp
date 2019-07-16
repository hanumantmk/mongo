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

#include "bh_memory.h"
#include "mongo_imports.h"
#include "wasm_export.h"

#include "mongo/scripting/wasm_engine.h"

namespace mongo {

Engine::Engine() {
    bh_memory_init_with_allocator((void*)mongoMalloc, (void*)free);
    wasm_runtime_init();
}

Engine::~Engine() {
    wasm_runtime_destroy();
    bh_memory_destroy();
}

Engine& Engine::get() {
    static Engine engine;

    return engine;
}

class ImplScope : public Engine::Scope {
public:
    struct Module {
        explicit Module(ConstDataRange bytes) {
            char err[100];
            _handle =
                wasm_runtime_load((const uint8_t*)bytes.data(), bytes.length(), err, sizeof(err));
            if (!_handle) {
                uasserted(ErrorCodes::BadValue, str::stream() << "could not load module: " << err);
            }
        }

        ~Module() {
            wasm_runtime_unload(_handle);
        }

        operator wasm_module_t() {
            return _handle;
        }

        wasm_module_t _handle;
    };

    struct Inst {
        explicit Inst(wasm_module_t module) {
            char err[100];
            _handle = wasm_runtime_instantiate(module, 0, 0, err, sizeof(err));
            if (!_handle) {
                uasserted(ErrorCodes::BadValue, str::stream() << "could not load inst: " << err);
            }
        }

        ~Inst() {
            wasm_runtime_deinstantiate(_handle);
        }

        operator wasm_module_inst_t() {
            return _handle;
        }

        wasm_module_inst_t _handle;
    };

    struct Exec {
        Exec() {
            char err[100];
            _handle = wasm_runtime_create_exec_env(8192);
            if (!_handle) {
                uasserted(ErrorCodes::BadValue, str::stream() << "could not load env: " << err);
            }
        }

        ~Exec() {
            wasm_runtime_destory_exec_env(_handle);
        }

        operator wasm_exec_env_t() {
            return _handle;
        }

        wasm_exec_env_t _handle;
    };

    ImplScope(ConstDataRange bytes) : _module(bytes), _inst(_module), _exec() {}

    void callStr(StringData name, StringData func, std::vector<uint32_t>& argv) override {
        auto lfunc = wasm_runtime_lookup_function(_inst, name.rawData(), func.rawData());

        if (!wasm_runtime_call_wasm(
                _inst, _exec, lfunc, argv.size(), argv.size() ? argv.data() : nullptr)) {
            wasm_runtime_clear_exception(_inst);
        }
    }

private:
    Module _module;
    Inst _inst;
    Exec _exec;
};

std::unique_ptr<Engine::Scope> Engine::createScope(ConstDataRange bytes) {
    return std::make_unique<ImplScope>(bytes);
}

BSONObj Engine::Scope::call(StringData name, BSONObj in) {
    std::vector<uint8_t> bytes;
    bytes.resize(in.objsize());
    memcpy(bytes.data(), in.objdata(), bytes.size());
    setWasmContext(std::move(bytes));

    std::vector<uint32_t> args;
    callStr(name, "()", args);

    auto& out = getWasmContext();

    return BSONObj((char*)out.data()).getOwned();
}

}  // namespace mongo
