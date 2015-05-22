#include "jscustomallocator.h"

#include "mongo/util/concurrency/threadlocal.h"

#ifdef HAVE_MALLOC_H
#include <malloc.h>
#elif defined(HAVE_MALLOC_SIZE)
#include <malloc/malloc.h>
#else
#error "need some kind of malloc usable size"
#endif

#include <iostream>

namespace mongo {
namespace sm {
ThreadLocalValue<size_t> total_bytes;
ThreadLocalValue<size_t> max_bytes;

size_t get_total_bytes() {
    return total_bytes.get();
}

void reset(size_t bytes) {
    total_bytes.set(0);
    max_bytes.set(bytes);
}

size_t get_max_bytes() {
    return max_bytes.get();
}

template <typename T>
void* wrap_alloc(T&& func, size_t bytes) {
    size_t mb = get_max_bytes();
    size_t tb = get_total_bytes();

    if (mb && tb + bytes > mb) {
        return nullptr;
    }

    void* p = func(bytes);

    if (!p) {
        return nullptr;
    }

    total_bytes.set(tb + bytes);

    return p;
}

size_t get_current(void* ptr) {
#if defined(HAVE_MALLOC_USABLE_SIZE)
    return malloc_usable_size(ptr);
#elif defined(HAVE_MALLOC_SIZE)
    return malloc_size(ptr);
#elif HAVE__MSIZE
    return _msize(ptr);
#else
#error "Must have something like malloc_usable_size"
#endif
}
};
};

void* js_malloc(size_t bytes) {
    return mongo::sm::wrap_alloc(std::malloc, bytes);
}

void* js_calloc(size_t bytes) {
    return mongo::sm::wrap_alloc([](size_t b) { return std::calloc(b, 1); }, bytes);
}

void* js_calloc(size_t nmemb, size_t size) {
    return mongo::sm::wrap_alloc([](size_t b) { return std::calloc(b, 1); }, nmemb * size);
}

void js_free(void* p) {
    size_t current = mongo::sm::get_current(p);
    size_t tb = mongo::sm::get_total_bytes();

    if (tb >= current) {
        mongo::sm::total_bytes.set(tb - current);
    }

    std::free(p);
}

void* js_realloc(void* p, size_t bytes) {
    if (!p) {
        return js_malloc(bytes);
    }

    if (!bytes) {
        js_free(p);
        return nullptr;
    }

    size_t current = mongo::sm::get_current(p);

    if (current >= bytes) {
        return p;
    }

    size_t tb = mongo::sm::total_bytes.get();

    if (tb >= current) {
        mongo::sm::total_bytes.set(tb - current);
    }

    return mongo::sm::wrap_alloc([p](size_t b) { return std::realloc(p, b); }, bytes);
}

char* js_strdup(const char* s) {
    size_t bytes = std::strlen(s) + 1;

    char* new_s = static_cast<char*>(js_malloc(bytes));

    if (!new_s) {
        return nullptr;
    }

    std::memcpy(new_s, s, bytes);

    return new_s;
}
