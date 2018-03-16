//
//  BBRUtilMath.h
//  breadwallet-core Ethereum
//
//  Created by Ed Gamble on 3/10/2018.
//  Copyright (c) 2018 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#ifndef BR_Util_Math_H
#define BR_Util_Math_H

#ifdef __cplusplus
extern "C" {
#endif

#include "BRInt.h"

#if LITTLE_ENDIAN != BYTE_ORDER
#error "Must be a `LITTLE ENDIAN` cpu architecture"
#endif

/**
 * Create from a single uint64_t value.
 */
extern UInt256
createUInt256 (uint64_t value);

/**
 * Create as 10^digits
 */
extern UInt256
createUInt256Power (uint8_t digits, int *overflow);

/**
 * Create from a string in the provided base.  The string must consist of only characters
 * in the base.  That is, avoid the '0x' prefix.  No decimal points.
 */
extern UInt256
createUInt256Parse (const char *digits, int base, int *error);

  /**
 * Return `x + y`
 */
extern UInt512
addUInt256 (UInt256 x, UInt256 y);

/**
 * Return `x + y`.  If the result is too big then overflow is set to 1 and zero is returned.
 */
extern UInt256
addUInt256_Overflow (UInt256 x, UInt256 y, int *overflow);

/**
 * If `x >= y` return `x - y` and set `negative` to 0; otherwise, return `y - x` and set
 * `negative` to 1.
 */
extern UInt256
subUInt256_Negative (UInt256 x, UInt256 y, int *negative);

/**
 * Return `x * y`
 */
extern UInt512
mulUInt256 (UInt256 x, UInt256 y);

/**
 * Multiple as `x * y`.  If the result is too big then overflow is set to 1 and
 * zero is returned.
 */
extern UInt256
mulUInt256_Overflow (UInt256 x, UInt256 y, int *overflow);

extern UInt256
divUInt256_Small (UInt256 x, uint32_t y, uint32_t *rem);

/**
 * Coerce `x`, a UInt512, to a UInt256.  If `x` is too big then overflow is set to 1 and
 * zero is returned.
 */
extern UInt256
coerceUInt256 (UInt512  x, int *overflow);

  /**
   * Returns the string representation of `x` in `base`.  The returned string is owned by the
   * caller.
   */
extern char *
coerceString (UInt256 x, int base);

//  static UInt256
//  divideUInt256 (UInt256 numerator, UInt256 denominator) {
//    // TODO: Implement
//    return UINT256_ZERO;
//  }
//
//  static UInt256
//  decodeUInt256 (const char *string) {
//    // TODO: Implement
//    return UINT256_ZERO;
//  }
//
//  static char *
//  encodeUInt256 (UInt256 value) {
//    // TODO: Implement
//    return NULL;
//  }

inline static int
eqUInt256 (UInt256 x, UInt256 y) {
  return UInt256Eq (x, y);
}

inline static int
gtUInt256 (UInt256 x, UInt256 y) {
  return x.u64[3] > y.u64[3]
  || (x.u64[3] == y.u64[3]
      && (x.u64[2] > y.u64[2]
          || (x.u64[2] == y.u64[2]
              && (x.u64[1] > y.u64[1]
                  || (x.u64[1] == y.u64[1]
                      && x.u64[0] > y.u64[0])))));
}

inline static int
geUInt256 (UInt256 x, UInt256 y) {
  return eqUInt256 (x, y) || gtUInt256 (x, y);
}

inline static int
ltUInt256 (UInt256 x, UInt256 y) {
  return x.u64[3] < y.u64[3]
  || (x.u64[3] == y.u64[3]
      && (x.u64[2] < y.u64[2]
          || (x.u64[2] == y.u64[2]
              && (x.u64[1] < y.u64[1]
                  || (x.u64[1] == y.u64[1]
                      && x.u64[0] < y.u64[0])))));
}

inline static int
leUInt256 (UInt256 x, UInt256 y) {
  return eqUInt256 (x, y) || ltUInt256 (x, y);
}

/**
 * Compare x and y returning:
 * -1 -> x  < y
 *  0 -> x == y
 * +1 -> x  > y
 */
extern int
compareUInt256 (UInt256 x, UInt256 y);

  //
  // Parsing
  //
  typedef enum {
    UTIL_MATH_PARSE_OK,
    UTIL_MATH_PARSE_STRANGE_DIGITS,
    UTIL_MATH_PARSE_UNDERFLOW,
    UTIL_MATH_PARSE_OVERFLOW
  } BRUtilMathParseStatus;


extern BRUtilMathParseStatus
parseIsInteger (const char *number);

extern BRUtilMathParseStatus
parseIsDecimal (const char *number);

extern BRUtilMathParseStatus
parseDecimalSplit (const char *string, char *whole, char *fract, unsigned long size);

#ifdef __cplusplus
}
#endif

#endif /* BR_Util_Math_H */