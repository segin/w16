/*
 * types.h - fixed-width integer types for both the 16-bit target
 * (OpenWatcom, 16-bit int) and host tools (gcc/clang, 32/64-bit).
 */
#ifndef W16_TYPES_H
#define W16_TYPES_H

#if defined(__WATCOMC__) && !defined(__386__)
typedef unsigned char   u8;
typedef unsigned int    u16;
typedef unsigned long   u32;
typedef signed char     s8;
typedef signed int      s16;
typedef signed long     s32;
#else
#include <stdint.h>
typedef uint8_t         u8;
typedef uint16_t        u16;
typedef uint32_t        u32;
typedef int8_t          s8;
typedef int16_t         s16;
typedef int32_t         s32;
#endif

/* Compile-time size checks (negative array size on failure). */
#define W16_STATIC_ASSERT(name, cond) typedef char w16_sa_##name[(cond) ? 1 : -1]

#endif /* W16_TYPES_H */
