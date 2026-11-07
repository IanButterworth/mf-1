/* The few C library routines the firmware uses. The FM-1 build has no C library headers; the chip layer
 * provides memcpy and memset, which the compiler's builtins call, and the rest is here. */
#pragma once

#define isc_memcpy(d, s, n) __builtin_memcpy((d), (s), (n))
#define isc_memset(d, c, n) __builtin_memset((d), (c), (n))

static inline void isc_memmove(void *d, const void *s, unsigned n)
{
    unsigned char *dp = (unsigned char *)d;
    const unsigned char *sp = (const unsigned char *)s;
    if (dp < sp)
        while (n--)
            *dp++ = *sp++;
    else
        while (n--)
            dp[n] = sp[n];
}

static inline int isc_streq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
