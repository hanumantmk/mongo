#include <cstdint>
#include <iostream>

#include "native_interface.h"
#include "wasm_export.h"

extern "C" void print(uint32_t ptr, uint32_t len) {
    wasm_module_inst_t module_inst = get_module_inst();

    if (len > (1 << 10)) {
        return;
    }

    if (!validate_app_addr(ptr, len)) {
        return;
    }

    char* actualPtr = (char*)addr_app_to_native(ptr);
    std::cout << "wasm debug: " << actualPtr << "\n";
}

extern "C" {

#include "lib_export.h"

static NativeSymbol extended_native_symbol_defs[] = {
    {"print", (void*)print},
};

#include "ext_lib_export.h"
}