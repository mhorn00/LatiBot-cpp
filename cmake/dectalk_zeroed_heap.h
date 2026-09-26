/*
 * Force-included into every DECtalk source by cmake/dectalk.cmake.
 *
 * DECtalk reads heap memory it never wrote. With the debug CRT that memory
 * is always the fill pattern, so it goes unnoticed; with the release CRT it
 * is whatever an earlier engine left there, and the same request speaks
 * differently from one run to the next (plan §21.17). Every allocation is
 * zeroed instead, which makes the release build repeatable without changing
 * the submodule.
 *
 * <stdlib.h> and <malloc.h> are included first, so the macros below cannot
 * rewrite their declarations of malloc and realloc.
 */
#pragma once

#include <malloc.h>
#include <stdlib.h>

#define malloc(size) calloc(1, (size))

/* _recalloc zeroes whatever it adds to a block from calloc or itself. */
#define realloc(block, size) _recalloc((block), 1, (size))
