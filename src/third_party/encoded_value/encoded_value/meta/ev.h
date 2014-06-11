#pragma once

/* EV provides access to encoded_value generated classes as Pointers.  Because
 * it doesn't implement references, it only really needs size.
 *
 * Template params are as follows:
 *
 * T - The type to return
 *
 * */

#include <cstring>

namespace encoded_value {
namespace Meta {

template <typename T>
class EV {
public:
    static const std::size_t size = T::_size;
};

}
}
