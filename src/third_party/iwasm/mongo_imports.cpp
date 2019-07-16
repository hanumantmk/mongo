#include <cstdint>
#include <vector>

#include "native_interface.h"
#include "wasm_export.h"

namespace mongo{
namespace {
thread_local std::vector<uint8_t> _context;
}

std::vector<uint8_t>& getWasmContext() {
    return _context;
}

void setWasmContext(std::vector<uint8_t> value) {
    _context = std::move(value);
}

}

extern "C" void returnValue(uint32_t ptr, uint32_t len) {
    wasm_module_inst_t module_inst = get_module_inst();

    if (len > (1<<10)) {
        return;
    }

    if (!validate_app_addr(ptr, len)) {
        return;
    }

    auto actualPtr = addr_app_to_native(ptr);

    mongo::_context.resize(len);

    memcpy(mongo::_context.data(), actualPtr, len);
}

extern "C" uint32_t getInputSize(void) {
    return mongo::_context.size();
}

extern "C" uint32_t writeInputToLocation(uint32_t ptr) {
    wasm_module_inst_t module_inst = get_module_inst();

    if (!validate_app_addr(ptr, mongo::_context.size())) {
        return 1;
    }

    auto actualPtr = addr_app_to_native(ptr);
    memcpy(actualPtr, mongo::_context.data(), mongo::_context.size());

    return 0;
}

extern "C" {

#include "lib_export.h"

static NativeSymbol extended_native_symbol_defs[] = {
    {"returnValue", (void*)returnValue},
    {"getInputSize", (void*)getInputSize},
    {"writeInputToLocation", (void*)writeInputToLocation}
};

#include "ext_lib_export.h"

}
