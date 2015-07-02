/**
 * Copyright (C) 2015 MongoDB Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * As a special exception, the copyright holders give permission to link the
 * code of portions of this program with the OpenSSL library under certain
 * conditions as described in each individual source file and distribute
 * linked combinations including the program with the OpenSSL library. You
 * must comply with the GNU Affero General Public License in all respects
 * for all of the code used other than as permitted herein. If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so. If you do not
 * wish to do so, delete this exception statement from your version. If you
 * delete this exception statement from all source files in the program,
 * then also delete it in the license file.
 */

#include "mongo/platform/basic.h"

#include <jscustomallocator.h>

#include "mongo/config.h"
#include "mongo/util/concurrency/threadlocal.h"
#include "mongo/scripting/mozjs/implscope.h"

#ifdef __linux__
#include <malloc.h>
#elif defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(_WIN32)
#include <malloc.h>
#else
#error "need some kind of malloc usable size"
#endif

/**
 * This shim interface (which controls dynamic allocation within SpiderMonkey),
 * consciously uses std::malloc and friends over mongoMalloc. It does this
 * because SpiderMonkey has some plausible options in the event of OOM,
 * specifically it can begin aggressive garbage collection. It would also be
 * reasonable to go the other route and fail, but for the moment I erred on the
 * side of maintaining the contract that SpiderMonkey expects.
 *
 * The overall strategy here is to keep track of allocations in a thread local,
 * offering us the chance to enforce soft limits on memory use rather than
 * waiting for the OS to OOM us.
 */

namespace mongo {
namespace sm {

namespace {
/**
 * These two variables track the total number of bytes handed out, and the
 * maximum number of bytes we will consider handing out. They are set by
 * MozJSImplScope on start up.
 */
MONGO_TRIVIALLY_CONSTRUCTIBLE_THREAD_LOCAL size_t total_bytes;
MONGO_TRIVIALLY_CONSTRUCTIBLE_THREAD_LOCAL size_t max_bytes;
}  // namespace

size_t get_total_bytes() {
    return total_bytes;
}

void reset(size_t bytes) {
    total_bytes = 0;
    max_bytes = bytes;
}

size_t get_max_bytes() {
    return max_bytes;
}

template <typename T>
void* wrap_alloc(T&& func, size_t bytes) {
    size_t mb = get_max_bytes();
    size_t tb = get_total_bytes();

    if (mb && (tb + bytes > mb)) {
        auto scope = mongo::mozjs::MozJSImplScope::getThreadScope();
        invariant(scope);

        scope->setOOM();

        return nullptr;
    }

    void* p = func(bytes);

    if (!p) {
        return nullptr;
    }

    total_bytes = tb + bytes;

    return p;
}

size_t get_current(void* ptr) {
#if defined(__linux__)
    return malloc_usable_size(ptr);
#elif defined(__APPLE__)
    return malloc_size(ptr);
#elif defined(_WIN32)
    return _msize(ptr);
#else
#error "Must have something like malloc_usable_size"
#endif
}

}  // namespace sm
}  // namespace mongo

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
        mongo::sm::total_bytes = tb - current;
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

    size_t tb = mongo::sm::total_bytes;

    if (tb >= current) {
        mongo::sm::total_bytes = tb - current;
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
