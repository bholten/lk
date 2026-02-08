#ifndef LK_INT_H
#define LK_INT_H

/*
 * Fixed-width integer types for C89.
 *
 * unsigned char   is >= 8 bits on all conforming implementations.
 * unsigned short  is >= 16 bits on all conforming implementations.
 * unsigned int    is >= 16 bits by the standard, but 32 bits on every
 *                 platform we realistically target (ILP32, LP64, LLP64).
 * int             same — 32 bits everywhere relevant.
 *
 * If you ever need to port to a platform where unsigned int is not 32 bits,
 * add a check here.
 */

typedef unsigned char  lk_u8;
typedef unsigned short lk_u16;
typedef unsigned int   lk_u32;
typedef int            lk_i32;

#endif
