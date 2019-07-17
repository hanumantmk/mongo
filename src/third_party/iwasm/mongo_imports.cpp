#include <cstdint>
#include <cmath>
#include <iostream>

extern "C" {

#include "lib_export.h"

uint32_t io_get_stdout(void) {
    std::cout << "in io_get_stdout\n";

    return 1;
}

int32_t resource_write(int32_t, int32_t, int32_t) {
    std::cout << "in resource_write\n";
    return 0;
}

void shout(void) {
    std::cout << "shout\n";
}

int invoke_ii(int, int) {
    return 0;
}

int invoke_iii(int, int, int) {
    return 0;
}

void invoke_v(int) {
}

void invoke_vi(int, int) {
}

void invoke_vii(int, int, int) {
}

void invoke_viiii(int, int, int, int, int) {
}

int fpclassify(double) {
    return 0;
}

int muldi3(int, int, int, int) {
    return 0;
}

double func6(double) {
    return 0;
}

static NativeSymbol extended_native_symbol_defs[] = {
    { "io_get_stdout", (void*)io_get_stdout },
    { "resource_write", (void*)resource_write },
    { "shout", (void*)shout },
    { "invoke_ii", (void*)invoke_ii },
    { "invoke_iii", (void*)invoke_iii },
    { "invoke_v", (void*)invoke_v },
    { "invoke_vi", (void*)invoke_vi },
    { "invoke_vii", (void*)invoke_vii },
    { "invoke_viiii", (void*)invoke_viiii },
    { "___fpclassify", (void*)fpclassify },
    { "___muldi3", (void*)muldi3},
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
    { "_i64Add", (void*)muldi3 },
    { "_i64Subtract", (void*)muldi3 },
    { "_llvm_log10_f64", (void*)func6 },
    { "_llvm_log2_f64", (void*)func6 },
    { "_llvm_trunc_f64", (void*)func6 },
    { "_log", (void*)log},
    { "_longjmp", (void*)invoke_vi},
    { "_pow", (void*)pow},
    { "_saveSetjmp", (void*)muldi3},
    { "_sin", (void*)sin},
    { "_tan", (void*)tan},
    { "_testSetjmp", (void*)invoke_iii},
    { "_sscanf", (void*)invoke_iii},
    { "_vsnprintf", (void*)muldi3},
};

#include "ext_lib_export.h"

}
