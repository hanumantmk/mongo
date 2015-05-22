#include <cstdlib>
#include <cstring>

#include <jstypes.h>

#define JS_OOM_POSSIBLY_FAIL() \
    do {                       \
    } while (0)
#define JS_OOM_POSSIBLY_FAIL_BOOL() \
    do {                            \
    } while (0)

namespace mongo {
namespace sm {
JS_PUBLIC_API(size_t) get_total_bytes();

JS_PUBLIC_API(void) reset(size_t max_bytes);
JS_PUBLIC_API(size_t) get_max_bytes();
};
};

JS_PUBLIC_API(void*) js_malloc(size_t bytes);
JS_PUBLIC_API(void*) js_calloc(size_t bytes);
JS_PUBLIC_API(void*) js_calloc(size_t nmemb, size_t size);
JS_PUBLIC_API(void) js_free(void* p);
JS_PUBLIC_API(void*) js_realloc(void* p, size_t bytes);
JS_PUBLIC_API(char*) js_strdup(const char* s);
