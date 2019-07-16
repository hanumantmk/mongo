#include <cstdint>
#include <vector>

namespace mongo {

std::vector<uint8_t>& getWasmContext();
void setWasmContext(std::vector<uint8_t> value);

}
