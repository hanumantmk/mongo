#include "jscustomallocator.h"

namespace mongo {
namespace sm {
    static thread_local size_t total_bytes = 0;
    static thread_local size_t max_bytes = 0;

    size_t get_total_bytes() {
        return total_bytes;
    }

    void reset (size_t bytes) {
        total_bytes = 0;
        max_bytes = bytes;
    }

    size_t get_max_bytes() {
        return max_bytes;
    }

#ifndef MONGO_HAVE_SIZEOF_MALLOC
    template <typename T>
    void* wrap_alloc(T&& func, size_t bytes)
    {
        bytes += 16;
        char* ptr = static_cast<char*>(func(bytes));

        if (! ptr) {
            return nullptr;
        }

        std::memcpy(ptr, &bytes, sizeof(bytes));

        total_bytes += bytes;

        if (max_bytes && total_bytes > max_bytes) {
            return nullptr;
        }

        return ptr + 16;
    }

    size_t get_current(void* p)
    {
        char* ptr = static_cast<char*>(p) - 16;

        size_t current;

        std::memcpy(&current, ptr, sizeof(current));

        return current;
    }
#else
    template <typename T>
    void* wrap_alloc(T&& func, size_t bytes)
    {
        if (max_bytes && total_bytes + bytes > max_bytes) {
            return nullptr;
        }

        void* p = func(bytes);

        if (! p) {
            return nullptr;
        }

        total_bytes += bytes;

        return p;
    }

    size_t get_current(void* p)
    {
        return malloc_usable_size(p);
    }
#endif

};
};

void* js_malloc(size_t bytes)
{
    return mongo::sm::wrap_alloc(std::malloc, bytes);
}

void* js_calloc(size_t bytes)
{
    return mongo::sm::wrap_alloc([](size_t b){ return std::calloc(b, 1); }, bytes);
}

void* js_calloc(size_t nmemb, size_t size)
{
    return mongo::sm::wrap_alloc([](size_t b){ return std::calloc(b, 1); }, nmemb * size);
}

void js_free(void* p)
{
    size_t current = mongo::sm::get_current(p);

    mongo::sm::total_bytes -= current;

    std::free(p);
}

void* js_realloc(void* p, size_t bytes)
{
    if (! p) {
        return js_malloc(bytes);
    }

    if (! bytes) {
        js_free(p);
        return nullptr;
    }

    size_t current = mongo::sm::get_current(p);

#ifndef MONGO_HAVE_SIZEOF_MALLOC
    if (current >= bytes + 16) {
        return p;
    }

    mongo::sm::total_bytes -= current;

    p = static_cast<char*>(p) - 16;
#else
    if (current >= bytes) {
        return p;
    }

    mongo::sm::total_bytes -= current;
#endif

    return mongo::sm::wrap_alloc([p](size_t b){ return std::realloc(p, b); }, bytes);
}

char* js_strdup(const char* s)
{
    size_t bytes = std::strlen(s) + 1;

    char* new_s = static_cast<char*>(js_malloc(bytes));

    if (! new_s) {
        return nullptr;
    }

    std::memcpy(new_s, s, bytes);

    return new_s;
}
