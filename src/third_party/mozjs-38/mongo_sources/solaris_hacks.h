#include <cstddef>

/* Solaris doesn't expose madvise to c++ compilers, so just define in
 * posix_madvise
 */
#define madvise posix_madvise

/* This doesn't seem to be provided on solaris.  This no opt function is
 * similiar to a patch that was introduced into firefox after 38
 */
namespace js {
namespace gc {
static void* MapAlignedPagesLastDitch(unsigned long, unsigned long alignment) { return nullptr; }
}
}
