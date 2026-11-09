/* The 64-bit divisions the processes need (process.c counts time in 64-bit ticks). The FM-1 build links no compiler
 * runtime unless it has Bluetooth, so these are the runtime's two routines, in 32-bit steps only: no 64-bit multiply
 * and no call to another runtime routine. While both numbers fit 32 bits, which is all the processes' arithmetic for
 * the first 46 hours of a piece, one 32-bit division does it. */
#include <stdint.h>

int64_t __divdi3(int64_t a, int64_t b);
int64_t __moddi3(int64_t a, int64_t b);

static uint64_t ph_udivmod(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint32_t nh = (uint32_t)(n >> 32), nl = (uint32_t)n, dh = (uint32_t)(d >> 32), dl = (uint32_t)d;
    uint32_t qh = 0, ql = 0, rh = 0, rl = 0;
    int i;
    if (!nh && !dh) {
        *rem = nl % dl;
        return nl / dl;
    }
    /* long division a bit at a time, the remainder in two words */
    for (i = 63; i >= 0; i--) {
        uint32_t bit = i >= 32 ? nh >> (i - 32) & 1u : nl >> i & 1u;
        rh = rh << 1 | rl >> 31;
        rl = rl << 1 | bit;
        if (rh > dh || (rh == dh && rl >= dl)) {
            rh -= dh + (rl < dl);
            rl -= dl;
            if (i >= 32)
                qh |= 1u << (i - 32);
            else
                ql |= 1u << i;
        }
    }
    *rem = (uint64_t)rh << 32 | rl;
    return (uint64_t)qh << 32 | ql;
}

/* As C divides: the quotient rounded toward zero, the remainder with the dividend's sign. */
int64_t __divdi3(int64_t a, int64_t b)
{
    uint64_t r, q = ph_udivmod(a < 0 ? 0u - (uint64_t)a : (uint64_t)a, b < 0 ? 0u - (uint64_t)b : (uint64_t)b, &r);
    return (a < 0) != (b < 0) ? (int64_t)(0u - q) : (int64_t)q;
}

int64_t __moddi3(int64_t a, int64_t b)
{
    uint64_t r;
    ph_udivmod(a < 0 ? 0u - (uint64_t)a : (uint64_t)a, b < 0 ? 0u - (uint64_t)b : (uint64_t)b, &r);
    return a < 0 ? (int64_t)(0u - r) : (int64_t)r;
}
