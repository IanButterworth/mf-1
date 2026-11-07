/* Single-precision maths for the DSP. The FM-1 has no libm and no double, so the firmware uses fm1-base's own
 * maths; the parity tests use libm to compare against MF-1. */
#pragma once

#ifdef ISC_FASTMATH
#include "fastmath.h"
#define isc_expf fm_expf
#define isc_powf fm_powf
#define isc_sinf fm_sinf
#define isc_cosf fm_cosf
#define isc_tanf fm_tanf
#define isc_sqrtf fm_sqrtf
#else
#include <math.h>
#define isc_expf expf
#define isc_powf powf
#define isc_sinf sinf
#define isc_cosf cosf
#define isc_tanf tanf
#define isc_sqrtf sqrtf
#endif

#ifndef ISC_POOL
#ifdef ISC_FASTMATH
#include "plat.h"
#define ISC_POOL PLAT_POOL
#else
#define ISC_POOL
#endif
#endif

#define ISC_PI 3.14159265358979f
