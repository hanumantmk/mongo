#include <cstdint>
#include <cmath>
#include <cstring>
#include <iostream>

extern "C" {

#include "lib_export.h"

int invoke_ii(int, int) {
    std::cout << "in invoke_ii\n";
    std::terminate();
    return 0;
}

int invoke_iii(int, int, int) {
    std::cout << "in invoke_iii\n";
    std::terminate();
    return 0;
}

void invoke_v(int) {
    std::cout << "in invoke_v\n";
    std::terminate();
}

void invoke_vi(int, int) {
    std::cout << "in invoke_vi\n";
    std::terminate();
}

void invoke_vii(int, int, int) {
    std::cout << "in invoke_vii\n";
    std::terminate();
}

void invoke_viii(int, int, int, int) {
    std::cout << "in invoke_viii\n";
    std::terminate();
}

void invoke_viiii(int, int, int, int, int) {
    std::cout << "in invoke_viiii\n";
    std::terminate();
}

void invoke_viiiiii(int, int, int, int, int, int, int) {
    std::cout << "in invoke_viiiiii\n";
    std::terminate();
}

int fpclassify(double d) {
    return std::fpclassify(d);
}

int rando4ints(int, int, int, int) {
    std::cout << "in rando4ints\n";
    return 0;
}

int savesetjmp(int, int, int, int) {
    std::cout << "in savesetjmp\n";
    std::terminate();
    return 0;
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

static NativeSymbol extended_native_symbol_defs[] = {
    { "invoke_ii", (void*)invoke_ii },
    { "invoke_iii", (void*)invoke_iii },
    { "invoke_v", (void*)invoke_v },
    { "invoke_vi", (void*)invoke_vi },
    { "invoke_vii", (void*)invoke_vii },
    { "invoke_viii", (void*)invoke_viii },
    { "invoke_viiii", (void*)invoke_viiii },
    { "invoke_viiiiii", (void*)invoke_viiiiii },
    { "___fpclassify", (void*)fpclassify },
    { "_acos", (void*)acos},
    { "_asin", (void*)asin},
    { "_atan", (void*)atan},
    { "_atan2", (void*)atan2},
    { "_bitshift64Ashr", (void*)invoke_iii},
    { "_bitshift64Lshr", (void*)invoke_iii},
    { "_bitshift64Shl", (void*)invoke_iii},
    { "_cbrt", (void*)cbrt},
    { "_cos", (void*)cos},
    { "_exp", (void*)exp},
    { "_llvm_log10_f64", (void*)func6 },
    { "_llvm_log2_f64", (void*)func6 },
    { "_llvm_trunc_f64", (void*)func6 },
    { "_log", (void*)log},
    { "_longjmp", (void*)invoke_vi},
    { "_pow", (void*)pow},
    { "_saveSetjmp", (void*)savesetjmp},
    { "_sin", (void*)sin},
    { "_tan", (void*)tan},
    { "_testSetjmp", (void*)invoke_iii},
    { "_sscanf", (void*)invoke_iii},
    { "_vsnprintf", (void*)vsnpr},
};

#include "ext_lib_export.h"

}
