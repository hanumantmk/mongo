#pragma once

namespace encoded_value {

/* We need this for computing union sizes at compile time */
template <int A, int B>
class _max {
public:
    static const int result = A > B ? A : B;
};

/* We need this to get around the operator&() overload on reference */
template <typename T>
inline T * addressof(T &val)
{
    /* A little complicated, but here goes:
     *
     * 1. reinterpret_cast can't cast away const or volatile, so cast with
     *    those initially.  We're targetting a char ref here to avoid alignment
     *    issues
     *
     * 2. const_cast away the const and volatile, which const_cast can do
     *    safely
     *
     * 3. reinterpret_cast back to a T* now, which should re-add const and
     *    volatile if they were there going in.
     * */
    return reinterpret_cast<T*>(
        &const_cast<char &>(reinterpret_cast<const volatile char &>(val))
    );
}

}
