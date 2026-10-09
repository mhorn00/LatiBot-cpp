/*
 * Force-included into every DECtalk source by cmake/dectalk.cmake.
 *
 * DECtalk reads heap memory it never wrote. With the debug CRT that memory is
 * always the fill pattern, so it goes unnoticed; with the release CRT it is
 * whatever an earlier engine left there, and the same request speaks
 * differently from one run to the next (src/modules/dectalk/docs/Speech.md §4.2). Every
 * allocation is zeroed instead, which makes the release build repeatable
 * without changing the submodule.
 *
 * <stdlib.h> and <malloc.h> are included first, so the macros below cannot
 * rewrite their declarations of malloc and realloc.
 */
#pragma once

#include <malloc.h>
#include <stdlib.h>

#define malloc(size) calloc(1, (size))

#ifdef _MSC_VER

/* _recalloc zeroes whatever it adds to a block from calloc or itself. */
#define realloc(block, size) _recalloc((block), 1, (size))

#else

#include <string.h>

/*
 * glibc has no _recalloc. Every byte of a block up to its usable size is
 * zeroed or written, so what realloc adds past the old usable size is zeroed
 * up to the new one, which keeps that true.
 */
static inline void* dectalk_zeroed_realloc(void* block, size_t size) {
    const size_t before = block != NULL ? malloc_usable_size(block) : 0;
    void* grown = (realloc)(block, size);
    if (grown != NULL) {
        const size_t after = malloc_usable_size(grown);
        if (after > before) memset((char*)grown + before, 0, after - before);
    }
    return grown;
}
#define realloc(block, size) dectalk_zeroed_realloc((block), (size))

#endif
