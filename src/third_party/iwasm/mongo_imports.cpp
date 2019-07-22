#include <array>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <iostream>

extern "C" {

#include "wasm_export.h"
#include "wasm_interp.h"
#include "lib_export.h"

int invoke_ii(int x, int y) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_ii", "(i32i32)i32");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 2> args{(uint32_t)x, (uint32_t)y};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (wasm_runtime_get_exception(inst)) {
        std::terminate();
    }

    return args[0];
}

int invoke_iii(int a , int b, int c) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_iii", "(i32i32i32)i32");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 3> args{(uint32_t)a, (uint32_t)b, (uint32_t)c};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (wasm_runtime_get_exception(inst)) {
        std::terminate();
    }

    return args[0];
}

void invoke_v(int x) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_v", "(i32)");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 1> args{(uint32_t)x};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (wasm_runtime_get_exception(inst)) {
        std::terminate();
    }
}

void invoke_vi(int x, int y) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_vi", "(i32i32)");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 2> args{(uint32_t)x, (uint32_t)y};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (auto e = wasm_runtime_get_exception(inst)) {
        printf("%s\n", e);
        std::terminate();
    }
}

void invoke_vii(int a, int b, int c) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_vii", "(i32i32i32)");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 3> args{(uint32_t)a, (uint32_t)b, (uint32_t)c};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (wasm_runtime_get_exception(inst)) {
        std::terminate();
    }
}

void invoke_viii(int a, int b, int c, int d) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_viii", "(i32i32i32i32)");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 4> args{(uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (wasm_runtime_get_exception(inst)) {
        std::terminate();
    }
}

void invoke_viiii(int a, int b, int c, int d, int e) {
    wasm_module_inst_t inst = wasm_runtime_get_current_module_inst();
    auto lfunc = wasm_runtime_lookup_function(inst, "dynCall_viiii", "(i32i32i32i32i32)");
    if (!lfunc) {
        std::terminate();
    }
    std::array<uint32_t, 5> args{(uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d, (uint32_t)e};
    wasm_interp_call_wasm(lfunc, args.size(), args.data());
    if (wasm_runtime_get_exception(inst)) {
        std::terminate();
    }
}

void invoke_viiiiii(int, int, int, int, int, int, int) {
    std::cout << "in invokeiiiiii\n";
    std::terminate();
}

int Xfpclassify(double d) {
    return std::fpclassify(d);
}

int savesetjmp(int, int, int, int) {
    std::cout << "in savesetjmp\n";
    return 0;
}

void print(uint32_t ptr, uint32_t len) {
    wasm_module_inst_t module_inst = wasm_runtime_get_current_module_inst();

    if (len > (1 << 10)) {
        return;
    }

    if (!wasm_runtime_validate_app_addr(module_inst, ptr, len)) {
        return;
    }

    char* actualPtr = (char*)wasm_runtime_addr_app_to_native(module_inst, ptr);
    std::cout << "wasm debug: " << actualPtr << "\n";
}

int vsnpr(int, int, int, int) {
    std::cout << "in vsnpr\n";
    return 0;
}

double func6(double) {
    std::cout << "in func6\n";
    std::terminate();
    return 0;
}

float Xacos(float x) {
    return std::acos(x);
}

float Xasin(float x) {
    return std::asin(x);
}

float Xatan(float x) {
    return std::atan(x);
}

float Xatan2(float x, float y) {
    return std::atan2(x, y);
}

float Xcbrt(float x) {
    return std::cbrt(x);
}

float Xcos(float x) {
    return std::cos(x);
}

float Xexp(float x) {
    return std::exp(x);
}

float Xlog(float x) {
    return std::log(x);
}

float Xpow(float x, float y) {
    return std::pow(x, y);
}

float Xsin(float x) {
    return std::sin(x);
}

float Xtan(float x) {
    return std::tan(x);
}

static NativeSymbol extended_native_symbol_defs[] = {
    { "invoke_ii", (void*)invoke_ii },
    { "invoke_iii", (void*)invoke_iii },
    { "invoke_v", (void*)invoke_v },
    { "invoke_vi", (void*)invoke_vi },
    { "invoke_vii", (void*)invoke_vii },
    { "invoke_viii", (void*)invoke_viii },
    { "invoke_viiii", (void*)invoke_viiii },
    { "invoke_viiiiii", (void*)invoke_viiiiii },
    { "___fpclassify", (void*)Xfpclassify },
    { "_acos", (void*)Xacos},
    { "_asin", (void*)Xasin},
    { "_atan", (void*)Xatan},
    { "_atan2", (void*)Xatan2},
    { "_bitshift64Ashr", (void*)invoke_iii},
    { "_bitshift64Lshr", (void*)invoke_iii},
    { "_bitshift64Shl", (void*)invoke_iii},
    { "_cbrt", (void*)Xcbrt},
    { "_cos", (void*)Xcos},
    { "_exp", (void*)Xexp},
    { "_llvm_log10_f64", (void*)func6 },
    { "_llvm_log2_f64", (void*)func6 },
    { "_llvm_trunc_f64", (void*)func6 },
    { "_log", (void*)Xlog},
    { "_longjmp", (void*)invoke_vi},
    { "_pow", (void*)Xpow},
    { "_saveSetjmp", (void*)savesetjmp},
    { "_sin", (void*)Xsin},
    { "_tan", (void*)Xtan},
    { "_testSetjmp", (void*)invoke_iii},
    { "_sscanf", (void*)invoke_iii},
    { "_vsnprintf", (void*)vsnpr},
    {"print", (void*)print},
};

#include "ext_lib_export.h"
}
