/* SPDX-License-Identifier: MIT
 * What the engine needs from the C library, in one place. Desktop builds use <math.h>. Firmware
 * builds with no libm (the FM-1's fm1-base) define SQ80_FASTMATH and put fm1-base's fastmath.h
 * on the include path. Memory is cleared and copied by hand: no <string.h> needed anywhere. */
#ifndef SQ80_PORT_H
#define SQ80_PORT_H

#include <stddef.h>

#ifdef SQ80_FASTMATH
#include "fastmath.h"
#define sq80_exp2f fm_exp2f
#define sq80_powf fm_powf
#define sq80_tanf fm_tanf
#define sq80_fabsf fm_fabsf
#else
#include <math.h>
#define sq80_exp2f exp2f
#define sq80_powf powf
#define sq80_tanf tanf
#define sq80_fabsf fabsf
#endif

static inline void sq80_zero(void *p, size_t n) {
    unsigned char *d = (unsigned char *)p;
    while (n--) *d++ = 0;
}

static inline void sq80_copy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
}

static inline int sq80_same(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    while (n--)
        if (*x++ != *y++) return 0;
    return 1;
}

#endif
