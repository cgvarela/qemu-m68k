/*
 * QEMU float support
 *
 * The code in this source file is derived from release 2a of the SoftFloat
 * IEC/IEEE Floating-point Arithmetic Package. Those parts of the code (and
 * some later contributions) are provided under that license, as detailed below.
 * It has subsequently been modified by contributors to the QEMU Project,
 * so some portions are provided under:
 *  the SoftFloat-2a license
 *  the BSD license
 *  GPL-v2-or-later
 *
 * Any future contributions to this file after December 1st 2014 will be
 * taken to be licensed under the Softfloat-2a license unless specifically
 * indicated otherwise.
 */

/*
===============================================================================
This C source file is part of the SoftFloat IEC/IEEE Floating-point
Arithmetic Package, Release 2a.

Written by John R. Hauser.  This work was made possible in part by the
International Computer Science Institute, located at Suite 600, 1947 Center
Street, Berkeley, California 94704.  Funding was partially provided by the
National Science Foundation under grant MIP-9311980.  The original version
of this code was written as part of a project to build a fixed-point vector
processor in collaboration with the University of California at Berkeley,
overseen by Profs. Nelson Morgan and John Wawrzynek.  More information
is available through the Web page `http://HTTP.CS.Berkeley.EDU/~jhauser/
arithmetic/SoftFloat.html'.

THIS SOFTWARE IS DISTRIBUTED AS IS, FOR FREE.  Although reasonable effort
has been made to avoid it, THIS SOFTWARE MAY CONTAIN FAULTS THAT WILL AT
TIMES RESULT IN INCORRECT BEHAVIOR.  USE OF THIS SOFTWARE IS RESTRICTED TO
PERSONS AND ORGANIZATIONS WHO CAN AND WILL TAKE FULL RESPONSIBILITY FOR ANY
AND ALL LOSSES, COSTS, OR OTHER PROBLEMS ARISING FROM ITS USE.

Derivative works are acceptable, even for commercial purposes, so long as
(1) they include prominent notice that the work is derivative, and (2) they
include prominent notice akin to these four paragraphs for those parts of
this code that are retained.

===============================================================================
*/

/* BSD licensing:
 * Copyright (c) 2006, Fabrice Bellard
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* Portions of this work are licensed under the terms of the GNU GPL,
 * version 2 or later. See the COPYING file in the top-level directory.
 */

/* softfloat (and in particular the code in softfloat-specialize.h) is
 * target-dependent and needs the TARGET_* macros.
 */
#include "qemu/osdep.h"

#include "fpu/softfloat.h"

/* We only need stdlib for abort() */

/*----------------------------------------------------------------------------
| Primitive arithmetic functions, including multi-word arithmetic, and
| division and square root approximations.  (Can be specialized to target if
| desired.)
*----------------------------------------------------------------------------*/
#include "softfloat-macros.h"

/*----------------------------------------------------------------------------
| Functions and definitions to determine:  (1) whether tininess for underflow
| is detected before or after rounding by default, (2) what (if anything)
| happens when exceptions are raised, (3) how signaling NaNs are distinguished
| from quiet NaNs, (4) the default generated quiet NaNs, and (5) how NaNs
| are propagated from function inputs to output.  These details are target-
| specific.
*----------------------------------------------------------------------------*/
#include "softfloat-specialize.h"

#include "softfloat_fpsp_tables.h"

/*----------------------------------------------------------------------------
| Returns the fraction bits of the half-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint32_t extractFloat16Frac(float16 a)
{
    return float16_val(a) & 0x3ff;
}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the half-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline int extractFloat16Exp(float16 a)
{
    return (float16_val(a) >> 10) & 0x1f;
}

/*----------------------------------------------------------------------------
| Returns the sign bit of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat16Sign(float16 a)
{
    return float16_val(a)>>15;
}

/*----------------------------------------------------------------------------
| Takes a 64-bit fixed-point value `absZ' with binary point between bits 6
| and 7, and returns the properly rounded 32-bit integer corresponding to the
| input.  If `zSign' is 1, the input is negated before being converted to an
| integer.  Bit 63 of `absZ' must be zero.  Ordinarily, the fixed-point input
| is simply rounded to an integer, with the inexact exception raised if the
| input cannot be represented exactly as an integer.  However, if the fixed-
| point input is too large, the invalid exception is raised and the largest
| positive or negative integer is returned.
*----------------------------------------------------------------------------*/

static int32_t roundAndPackInt32(flag zSign, uint64_t absZ, float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven;
    int8_t roundIncrement, roundBits;
    int32_t z;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        roundIncrement = 0x40;
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : 0x7f;
        break;
    case float_round_down:
        roundIncrement = zSign ? 0x7f : 0;
        break;
    default:
        abort();
    }
    roundBits = absZ & 0x7F;
    absZ = ( absZ + roundIncrement )>>7;
    absZ &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    z = absZ;
    if ( zSign ) z = - z;
    if ( ( absZ>>32 ) || ( z && ( ( z < 0 ) ^ zSign ) ) ) {
        float_raise(float_flag_invalid, status);
        return zSign ? (int32_t) 0x80000000 : 0x7FFFFFFF;
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Takes the 128-bit fixed-point value formed by concatenating `absZ0' and
| `absZ1', with binary point between bits 63 and 64 (between the input words),
| and returns the properly rounded 64-bit integer corresponding to the input.
| If `zSign' is 1, the input is negated before being converted to an integer.
| Ordinarily, the fixed-point input is simply rounded to an integer, with
| the inexact exception raised if the input cannot be represented exactly as
| an integer.  However, if the fixed-point input is too large, the invalid
| exception is raised and the largest positive or negative integer is
| returned.
*----------------------------------------------------------------------------*/

static int64_t roundAndPackInt64(flag zSign, uint64_t absZ0, uint64_t absZ1,
                               float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment;
    int64_t z;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t) absZ1 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && absZ1;
        break;
    case float_round_down:
        increment = zSign && absZ1;
        break;
    default:
        abort();
    }
    if ( increment ) {
        ++absZ0;
        if ( absZ0 == 0 ) goto overflow;
        absZ0 &= ~ ( ( (uint64_t) ( absZ1<<1 ) == 0 ) & roundNearestEven );
    }
    z = absZ0;
    if ( zSign ) z = - z;
    if ( z && ( ( z < 0 ) ^ zSign ) ) {
 overflow:
        float_raise(float_flag_invalid, status);
        return
              zSign ? (int64_t) LIT64( 0x8000000000000000 )
            : LIT64( 0x7FFFFFFFFFFFFFFF );
    }
    if (absZ1) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Takes the 128-bit fixed-point value formed by concatenating `absZ0' and
| `absZ1', with binary point between bits 63 and 64 (between the input words),
| and returns the properly rounded 64-bit unsigned integer corresponding to the
| input.  Ordinarily, the fixed-point input is simply rounded to an integer,
| with the inexact exception raised if the input cannot be represented exactly
| as an integer.  However, if the fixed-point input is too large, the invalid
| exception is raised and the largest unsigned integer is returned.
*----------------------------------------------------------------------------*/

static int64_t roundAndPackUint64(flag zSign, uint64_t absZ0,
                                uint64_t absZ1, float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = (roundingMode == float_round_nearest_even);
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t)absZ1 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && absZ1;
        break;
    case float_round_down:
        increment = zSign && absZ1;
        break;
    default:
        abort();
    }
    if (increment) {
        ++absZ0;
        if (absZ0 == 0) {
            float_raise(float_flag_invalid, status);
            return LIT64(0xFFFFFFFFFFFFFFFF);
        }
        absZ0 &= ~(((uint64_t)(absZ1<<1) == 0) & roundNearestEven);
    }

    if (zSign && absZ0) {
        float_raise(float_flag_invalid, status);
        return 0;
    }

    if (absZ1) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return absZ0;
}

/*----------------------------------------------------------------------------
| Returns the fraction bits of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint32_t extractFloat32Frac( float32 a )
{

    return float32_val(a) & 0x007FFFFF;

}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline int extractFloat32Exp(float32 a)
{

    return ( float32_val(a)>>23 ) & 0xFF;

}

/*----------------------------------------------------------------------------
| Returns the sign bit of the single-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat32Sign( float32 a )
{

    return float32_val(a)>>31;

}

/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/
float32 float32_squash_input_denormal(float32 a, float_status *status)
{
    if (status->flush_inputs_to_zero) {
        if (extractFloat32Exp(a) == 0 && extractFloat32Frac(a) != 0) {
            float_raise(float_flag_input_denormal, status);
            return make_float32(float32_val(a) & 0x80000000);
        }
    }
    return a;
}

/*----------------------------------------------------------------------------
| Normalizes the subnormal single-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat32Subnormal(uint32_t aSig, int *zExpPtr, uint32_t *zSigPtr)
{
    int8_t shiftCount;

    shiftCount = countLeadingZeros32( aSig ) - 8;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| single-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static inline float32 packFloat32(flag zSign, int zExp, uint32_t zSig)
{

    return make_float32(
          ( ( (uint32_t) zSign )<<31 ) + ( ( (uint32_t) zExp )<<23 ) + zSig);

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the single-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal single-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 30
| and 29, which is 7 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 roundAndPackFloat32(flag zSign, int zExp, uint32_t zSig,
                                   float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven;
    int8_t roundIncrement, roundBits;
    flag isTiny;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        roundIncrement = 0x40;
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : 0x7f;
        break;
    case float_round_down:
        roundIncrement = zSign ? 0x7f : 0;
        break;
    default:
        abort();
        break;
    }
    roundBits = zSig & 0x7F;
    if ( 0xFD <= (uint16_t) zExp ) {
        if (    ( 0xFD < zExp )
             || (    ( zExp == 0xFD )
                  && ( (int32_t) ( zSig + roundIncrement ) < 0 ) )
           ) {
            float_raise(float_flag_overflow | float_flag_inexact, status);
            return packFloat32( zSign, 0xFF, - ( roundIncrement == 0 ));
        }
        if ( zExp < 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat32(zSign, 0, 0);
            }
            isTiny =
                (status->float_detect_tininess
                 == float_tininess_before_rounding)
                || ( zExp < -1 )
                || ( zSig + roundIncrement < 0x80000000 );
            shift32RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x7F;
            if (isTiny && roundBits) {
                float_raise(float_flag_underflow, status);
            }
        }
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    zSig = ( zSig + roundIncrement )>>7;
    zSig &= ~ ( ( ( roundBits ^ 0x40 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat32( zSign, zExp, zSig );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper single-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat32' except that `zSig' does not have to be normalized.
| Bit 31 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

static float32
 normalizeRoundAndPackFloat32(flag zSign, int zExp, uint32_t zSig,
                              float_status *status)
{
    int8_t shiftCount;

    shiftCount = countLeadingZeros32( zSig ) - 1;
    return roundAndPackFloat32(zSign, zExp - shiftCount, zSig<<shiftCount,
                               status);

}

/*----------------------------------------------------------------------------
| Returns the fraction bits of the double-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloat64Frac( float64 a )
{

    return float64_val(a) & LIT64( 0x000FFFFFFFFFFFFF );

}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the double-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline int extractFloat64Exp(float64 a)
{

    return ( float64_val(a)>>52 ) & 0x7FF;

}

/*----------------------------------------------------------------------------
| Returns the sign bit of the double-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat64Sign( float64 a )
{

    return float64_val(a)>>63;

}

/*----------------------------------------------------------------------------
| If `a' is denormal and we are in flush-to-zero mode then set the
| input-denormal exception and return zero. Otherwise just return the value.
*----------------------------------------------------------------------------*/
float64 float64_squash_input_denormal(float64 a, float_status *status)
{
    if (status->flush_inputs_to_zero) {
        if (extractFloat64Exp(a) == 0 && extractFloat64Frac(a) != 0) {
            float_raise(float_flag_input_denormal, status);
            return make_float64(float64_val(a) & (1ULL << 63));
        }
    }
    return a;
}

/*----------------------------------------------------------------------------
| Normalizes the subnormal double-precision floating-point value represented
| by the denormalized significand `aSig'.  The normalized exponent and
| significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat64Subnormal(uint64_t aSig, int *zExpPtr, uint64_t *zSigPtr)
{
    int8_t shiftCount;

    shiftCount = countLeadingZeros64( aSig ) - 11;
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| double-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static inline float64 packFloat64(flag zSign, int zExp, uint64_t zSig)
{

    return make_float64(
        ( ( (uint64_t) zSign )<<63 ) + ( ( (uint64_t) zExp )<<52 ) + zSig);

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper double-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the double-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal double-
| precision floating-point number.
|     The input significand `zSig' has its binary point between bits 62
| and 61, which is 10 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float64 roundAndPackFloat64(flag zSign, int zExp, uint64_t zSig,
                                   float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven;
    int roundIncrement, roundBits;
    flag isTiny;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        roundIncrement = 0x200;
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : 0x3ff;
        break;
    case float_round_down:
        roundIncrement = zSign ? 0x3ff : 0;
        break;
    case float_round_to_odd:
        roundIncrement = (zSig & 0x400) ? 0 : 0x3ff;
        break;
    default:
        abort();
    }
    roundBits = zSig & 0x3FF;
    if ( 0x7FD <= (uint16_t) zExp ) {
        if (    ( 0x7FD < zExp )
             || (    ( zExp == 0x7FD )
                  && ( (int64_t) ( zSig + roundIncrement ) < 0 ) )
           ) {
            bool overflow_to_inf = roundingMode != float_round_to_odd &&
                                   roundIncrement != 0;
            float_raise(float_flag_overflow | float_flag_inexact, status);
            return packFloat64(zSign, 0x7FF, -(!overflow_to_inf));
        }
        if ( zExp < 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat64(zSign, 0, 0);
            }
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < -1 )
                || ( zSig + roundIncrement < LIT64( 0x8000000000000000 ) );
            shift64RightJamming( zSig, - zExp, &zSig );
            zExp = 0;
            roundBits = zSig & 0x3FF;
            if (isTiny && roundBits) {
                float_raise(float_flag_underflow, status);
            }
            if (roundingMode == float_round_to_odd) {
                /*
                 * For round-to-odd case, the roundIncrement depends on
                 * zSig which just changed.
                 */
                roundIncrement = (zSig & 0x400) ? 0 : 0x3ff;
            }
        }
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    zSig = ( zSig + roundIncrement )>>10;
    zSig &= ~ ( ( ( roundBits ^ 0x200 ) == 0 ) & roundNearestEven );
    if ( zSig == 0 ) zExp = 0;
    return packFloat64( zSign, zExp, zSig );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper double-precision floating-
| point value corresponding to the abstract input.  This routine is just like
| `roundAndPackFloat64' except that `zSig' does not have to be normalized.
| Bit 63 of `zSig' must be zero, and `zExp' must be 1 less than the ``true''
| floating-point exponent.
*----------------------------------------------------------------------------*/

static float64
 normalizeRoundAndPackFloat64(flag zSign, int zExp, uint64_t zSig,
                              float_status *status)
{
    int8_t shiftCount;

    shiftCount = countLeadingZeros64( zSig ) - 1;
    return roundAndPackFloat64(zSign, zExp - shiftCount, zSig<<shiftCount,
                               status);

}

/*----------------------------------------------------------------------------
| Returns the fraction bits of the extended double-precision floating-point
| value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloatx80Frac( floatx80 a )
{

    return a.low;

}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the extended double-precision floating-point
| value `a'.
*----------------------------------------------------------------------------*/

static inline int32_t extractFloatx80Exp( floatx80 a )
{

    return a.high & 0x7FFF;

}

/*----------------------------------------------------------------------------
| Returns the sign bit of the extended double-precision floating-point value
| `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloatx80Sign( floatx80 a )
{

    return a.high>>15;

}

/*----------------------------------------------------------------------------
| Normalizes the subnormal extended double-precision floating-point value
| represented by the denormalized significand `aSig'.  The normalized exponent
| and significand are stored at the locations pointed to by `zExpPtr' and
| `zSigPtr', respectively.
*----------------------------------------------------------------------------*/

static void
 normalizeFloatx80Subnormal( uint64_t aSig, int32_t *zExpPtr, uint64_t *zSigPtr )
{
    int8_t shiftCount;

    shiftCount = countLeadingZeros64( aSig );
    *zSigPtr = aSig<<shiftCount;
    *zExpPtr = 1 - shiftCount;

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into an
| extended double-precision floating-point value, returning the result.
*----------------------------------------------------------------------------*/

static inline floatx80 packFloatx80( flag zSign, int32_t zExp, uint64_t zSig )
{
    floatx80 z;

    z.low = zSig;
    z.high = ( ( (uint16_t) zSign )<<15 ) + zExp;
    return z;

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and extended significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  Ordinarily, the abstract value is
| rounded and packed into the extended double-precision format, with the
| inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal extended
| double-precision floating-point number.
|     If `roundingPrecision' is 32 or 64, the result is rounded to the same
| number of bits as single or double precision, respectively.  Otherwise, the
| result is rounded to the full precision of the extended double-precision
| format.
|     The input significand must be normalized or smaller.  If the input
| significand is not normalized, `zExp' must be 0; in that case, the result
| returned is a subnormal number, and it must not require rounding.  The
| handling of underflow and overflow follows the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 roundAndPackFloatx80(int8_t roundingPrecision, flag zSign,
                                     int32_t zExp, uint64_t zSig0, uint64_t zSig1,
                                     float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment, isTiny;
    int64_t roundIncrement, roundMask, roundBits;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    if ( roundingPrecision == 80 ) goto precision80;
    if ( roundingPrecision == 64 ) {
        roundIncrement = LIT64( 0x0000000000000400 );
        roundMask = LIT64( 0x00000000000007FF );
    }
    else if ( roundingPrecision == 32 ) {
        roundIncrement = LIT64( 0x0000008000000000 );
        roundMask = LIT64( 0x000000FFFFFFFFFF );
    }
    else {
        goto precision80;
    }
    zSig0 |= ( zSig1 != 0 );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        break;
    case float_round_to_zero:
        roundIncrement = 0;
        break;
    case float_round_up:
        roundIncrement = zSign ? 0 : roundMask;
        break;
    case float_round_down:
        roundIncrement = zSign ? roundMask : 0;
        break;
    default:
        abort();
    }
    roundBits = zSig0 & roundMask;
    if ( 0x7FFD <= (uint32_t) ( zExp - 1 ) ) {
        if (    ( 0x7FFE < zExp )
             || ( ( zExp == 0x7FFE ) && ( zSig0 + roundIncrement < zSig0 ) )
           ) {
            goto overflow;
        }
        if ( zExp <= 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloatx80(zSign, 0, 0);
            }
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < 0 )
                || ( zSig0 <= zSig0 + roundIncrement );
            shift64RightJamming( zSig0, 1 - zExp, &zSig0 );
            zExp = 0;
            roundBits = zSig0 & roundMask;
            if (isTiny && roundBits) {
                float_raise(float_flag_underflow, status);
            }
            if (roundBits) {
                status->float_exception_flags |= float_flag_inexact;
            }
            zSig0 += roundIncrement;
            if ( (int64_t) zSig0 < 0 ) zExp = 1;
            roundIncrement = roundMask + 1;
            if ( roundNearestEven && ( roundBits<<1 == roundIncrement ) ) {
                roundMask |= roundIncrement;
            }
            zSig0 &= ~ roundMask;
            return packFloatx80( zSign, zExp, zSig0 );
        }
    }
    if (roundBits) {
        status->float_exception_flags |= float_flag_inexact;
    }
    zSig0 += roundIncrement;
    if ( zSig0 < roundIncrement ) {
        ++zExp;
        zSig0 = LIT64( 0x8000000000000000 );
    }
    roundIncrement = roundMask + 1;
    if ( roundNearestEven && ( roundBits<<1 == roundIncrement ) ) {
        roundMask |= roundIncrement;
    }
    zSig0 &= ~ roundMask;
    if ( zSig0 == 0 ) zExp = 0;
    return packFloatx80( zSign, zExp, zSig0 );
 precision80:
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t)zSig1 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && zSig1;
        break;
    case float_round_down:
        increment = zSign && zSig1;
        break;
    default:
        abort();
    }
    if ( 0x7FFD <= (uint32_t) ( zExp - 1 ) ) {
        if (    ( 0x7FFE < zExp )
             || (    ( zExp == 0x7FFE )
                  && ( zSig0 == LIT64( 0xFFFFFFFFFFFFFFFF ) )
                  && increment
                )
           ) {
            roundMask = 0;
 overflow:
            float_raise(float_flag_overflow | float_flag_inexact, status);
            if (    ( roundingMode == float_round_to_zero )
                 || ( zSign && ( roundingMode == float_round_up ) )
                 || ( ! zSign && ( roundingMode == float_round_down ) )
               ) {
                return packFloatx80( zSign, 0x7FFE, ~ roundMask );
            }
            return packFloatx80(zSign,
                                floatx80_infinity_high,
                                floatx80_infinity_low);
        }
        if ( zExp <= 0 ) {
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < 0 )
                || ! increment
                || ( zSig0 < LIT64( 0xFFFFFFFFFFFFFFFF ) );
            shift64ExtraRightJamming( zSig0, zSig1, 1 - zExp, &zSig0, &zSig1 );
            zExp = 0;
            if (isTiny && zSig1) {
                float_raise(float_flag_underflow, status);
            }
            if (zSig1) {
                status->float_exception_flags |= float_flag_inexact;
            }
            switch (roundingMode) {
            case float_round_nearest_even:
            case float_round_ties_away:
                increment = ((int64_t)zSig1 < 0);
                break;
            case float_round_to_zero:
                increment = 0;
                break;
            case float_round_up:
                increment = !zSign && zSig1;
                break;
            case float_round_down:
                increment = zSign && zSig1;
                break;
            default:
                abort();
            }
            if ( increment ) {
                ++zSig0;
                zSig0 &=
                    ~ ( ( (uint64_t) ( zSig1<<1 ) == 0 ) & roundNearestEven );
                if ( (int64_t) zSig0 < 0 ) zExp = 1;
            }
            return packFloatx80( zSign, zExp, zSig0 );
        }
    }
    if (zSig1) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( increment ) {
        ++zSig0;
        if ( zSig0 == 0 ) {
            ++zExp;
            zSig0 = LIT64( 0x8000000000000000 );
        }
        else {
            zSig0 &= ~ ( ( (uint64_t) ( zSig1<<1 ) == 0 ) & roundNearestEven );
        }
    }
    else {
        if ( zSig0 == 0 ) zExp = 0;
    }
    return packFloatx80( zSign, zExp, zSig0 );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent
| `zExp', and significand formed by the concatenation of `zSig0' and `zSig1',
| and returns the proper extended double-precision floating-point value
| corresponding to the abstract input.  This routine is just like
| `roundAndPackFloatx80' except that the input significand does not have to be
| normalized.
*----------------------------------------------------------------------------*/

static floatx80 normalizeRoundAndPackFloatx80(int8_t roundingPrecision,
                                              flag zSign, int32_t zExp,
                                              uint64_t zSig0, uint64_t zSig1,
                                              float_status *status)
{
    int8_t shiftCount;

    if ( zSig0 == 0 ) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    shiftCount = countLeadingZeros64( zSig0 );
    shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    zExp -= shiftCount;
    return roundAndPackFloatx80(roundingPrecision, zSign, zExp,
                                zSig0, zSig1, status);

}

/*----------------------------------------------------------------------------
| Returns the least-significant 64 fraction bits of the quadruple-precision
| floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloat128Frac1( float128 a )
{

    return a.low;

}

/*----------------------------------------------------------------------------
| Returns the most-significant 48 fraction bits of the quadruple-precision
| floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline uint64_t extractFloat128Frac0( float128 a )
{

    return a.high & LIT64( 0x0000FFFFFFFFFFFF );

}

/*----------------------------------------------------------------------------
| Returns the exponent bits of the quadruple-precision floating-point value
| `a'.
*----------------------------------------------------------------------------*/

static inline int32_t extractFloat128Exp( float128 a )
{

    return ( a.high>>48 ) & 0x7FFF;

}

/*----------------------------------------------------------------------------
| Returns the sign bit of the quadruple-precision floating-point value `a'.
*----------------------------------------------------------------------------*/

static inline flag extractFloat128Sign( float128 a )
{

    return a.high>>63;

}

/*----------------------------------------------------------------------------
| Normalizes the subnormal quadruple-precision floating-point value
| represented by the denormalized significand formed by the concatenation of
| `aSig0' and `aSig1'.  The normalized exponent is stored at the location
| pointed to by `zExpPtr'.  The most significant 49 bits of the normalized
| significand are stored at the location pointed to by `zSig0Ptr', and the
| least significant 64 bits of the normalized significand are stored at the
| location pointed to by `zSig1Ptr'.
*----------------------------------------------------------------------------*/

static void
 normalizeFloat128Subnormal(
     uint64_t aSig0,
     uint64_t aSig1,
     int32_t *zExpPtr,
     uint64_t *zSig0Ptr,
     uint64_t *zSig1Ptr
 )
{
    int8_t shiftCount;

    if ( aSig0 == 0 ) {
        shiftCount = countLeadingZeros64( aSig1 ) - 15;
        if ( shiftCount < 0 ) {
            *zSig0Ptr = aSig1>>( - shiftCount );
            *zSig1Ptr = aSig1<<( shiftCount & 63 );
        }
        else {
            *zSig0Ptr = aSig1<<shiftCount;
            *zSig1Ptr = 0;
        }
        *zExpPtr = - shiftCount - 63;
    }
    else {
        shiftCount = countLeadingZeros64( aSig0 ) - 15;
        shortShift128Left( aSig0, aSig1, shiftCount, zSig0Ptr, zSig1Ptr );
        *zExpPtr = 1 - shiftCount;
    }

}

/*----------------------------------------------------------------------------
| Packs the sign `zSign', the exponent `zExp', and the significand formed
| by the concatenation of `zSig0' and `zSig1' into a quadruple-precision
| floating-point value, returning the result.  After being shifted into the
| proper positions, the three fields `zSign', `zExp', and `zSig0' are simply
| added together to form the most significant 32 bits of the result.  This
| means that any integer portion of `zSig0' will be added into the exponent.
| Since a properly normalized significand will have an integer portion equal
| to 1, the `zExp' input should be 1 less than the desired result exponent
| whenever `zSig0' and `zSig1' concatenated form a complete, normalized
| significand.
*----------------------------------------------------------------------------*/

static inline float128
 packFloat128( flag zSign, int32_t zExp, uint64_t zSig0, uint64_t zSig1 )
{
    float128 z;

    z.low = zSig1;
    z.high = ( ( (uint64_t) zSign )<<63 ) + ( ( (uint64_t) zExp )<<48 ) + zSig0;
    return z;

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and extended significand formed by the concatenation of `zSig0', `zSig1',
| and `zSig2', and returns the proper quadruple-precision floating-point value
| corresponding to the abstract input.  Ordinarily, the abstract value is
| simply rounded and packed into the quadruple-precision format, with the
| inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal quadruple-
| precision floating-point number.
|     The input significand must be normalized or smaller.  If the input
| significand is not normalized, `zExp' must be 0; in that case, the result
| returned is a subnormal number, and it must not require rounding.  In the
| usual case that the input significand is normalized, `zExp' must be 1 less
| than the ``true'' floating-point exponent.  The handling of underflow and
| overflow follows the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 roundAndPackFloat128(flag zSign, int32_t zExp,
                                     uint64_t zSig0, uint64_t zSig1,
                                     uint64_t zSig2, float_status *status)
{
    int8_t roundingMode;
    flag roundNearestEven, increment, isTiny;

    roundingMode = status->float_rounding_mode;
    roundNearestEven = ( roundingMode == float_round_nearest_even );
    switch (roundingMode) {
    case float_round_nearest_even:
    case float_round_ties_away:
        increment = ((int64_t)zSig2 < 0);
        break;
    case float_round_to_zero:
        increment = 0;
        break;
    case float_round_up:
        increment = !zSign && zSig2;
        break;
    case float_round_down:
        increment = zSign && zSig2;
        break;
    case float_round_to_odd:
        increment = !(zSig1 & 0x1) && zSig2;
        break;
    default:
        abort();
    }
    if ( 0x7FFD <= (uint32_t) zExp ) {
        if (    ( 0x7FFD < zExp )
             || (    ( zExp == 0x7FFD )
                  && eq128(
                         LIT64( 0x0001FFFFFFFFFFFF ),
                         LIT64( 0xFFFFFFFFFFFFFFFF ),
                         zSig0,
                         zSig1
                     )
                  && increment
                )
           ) {
            float_raise(float_flag_overflow | float_flag_inexact, status);
            if (    ( roundingMode == float_round_to_zero )
                 || ( zSign && ( roundingMode == float_round_up ) )
                 || ( ! zSign && ( roundingMode == float_round_down ) )
                 || (roundingMode == float_round_to_odd)
               ) {
                return
                    packFloat128(
                        zSign,
                        0x7FFE,
                        LIT64( 0x0000FFFFFFFFFFFF ),
                        LIT64( 0xFFFFFFFFFFFFFFFF )
                    );
            }
            return packFloat128( zSign, 0x7FFF, 0, 0 );
        }
        if ( zExp < 0 ) {
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat128(zSign, 0, 0, 0);
            }
            isTiny =
                   (status->float_detect_tininess
                    == float_tininess_before_rounding)
                || ( zExp < -1 )
                || ! increment
                || lt128(
                       zSig0,
                       zSig1,
                       LIT64( 0x0001FFFFFFFFFFFF ),
                       LIT64( 0xFFFFFFFFFFFFFFFF )
                   );
            shift128ExtraRightJamming(
                zSig0, zSig1, zSig2, - zExp, &zSig0, &zSig1, &zSig2 );
            zExp = 0;
            if (isTiny && zSig2) {
                float_raise(float_flag_underflow, status);
            }
            switch (roundingMode) {
            case float_round_nearest_even:
            case float_round_ties_away:
                increment = ((int64_t)zSig2 < 0);
                break;
            case float_round_to_zero:
                increment = 0;
                break;
            case float_round_up:
                increment = !zSign && zSig2;
                break;
            case float_round_down:
                increment = zSign && zSig2;
                break;
            case float_round_to_odd:
                increment = !(zSig1 & 0x1) && zSig2;
                break;
            default:
                abort();
            }
        }
    }
    if (zSig2) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( increment ) {
        add128( zSig0, zSig1, 0, 1, &zSig0, &zSig1 );
        zSig1 &= ~ ( ( zSig2 + zSig2 == 0 ) & roundNearestEven );
    }
    else {
        if ( ( zSig0 | zSig1 ) == 0 ) zExp = 0;
    }
    return packFloat128( zSign, zExp, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand formed by the concatenation of `zSig0' and `zSig1', and
| returns the proper quadruple-precision floating-point value corresponding
| to the abstract input.  This routine is just like `roundAndPackFloat128'
| except that the input significand has fewer bits and does not have to be
| normalized.  In all cases, `zExp' must be 1 less than the ``true'' floating-
| point exponent.
*----------------------------------------------------------------------------*/

static float128 normalizeRoundAndPackFloat128(flag zSign, int32_t zExp,
                                              uint64_t zSig0, uint64_t zSig1,
                                              float_status *status)
{
    int8_t shiftCount;
    uint64_t zSig2;

    if ( zSig0 == 0 ) {
        zSig0 = zSig1;
        zSig1 = 0;
        zExp -= 64;
    }
    shiftCount = countLeadingZeros64( zSig0 ) - 15;
    if ( 0 <= shiftCount ) {
        zSig2 = 0;
        shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    }
    else {
        shift128ExtraRightJamming(
            zSig0, zSig1, 0, - shiftCount, &zSig0, &zSig1, &zSig2 );
    }
    zExp -= shiftCount;
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 int32_to_float32(int32_t a, float_status *status)
{
    flag zSign;

    if ( a == 0 ) return float32_zero;
    if ( a == (int32_t) 0x80000000 ) return packFloat32( 1, 0x9E, 0 );
    zSign = ( a < 0 );
    return normalizeRoundAndPackFloat32(zSign, 0x9C, zSign ? -a : a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 int32_to_float64(int32_t a, float_status *status)
{
    flag zSign;
    uint32_t absA;
    int8_t shiftCount;
    uint64_t zSig;

    if ( a == 0 ) return float64_zero;
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros32( absA ) + 21;
    zSig = absA;
    return packFloat64( zSign, 0x432 - shiftCount, zSig<<shiftCount );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a'
| to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 int32_to_floatx80(int32_t a, float_status *status)
{
    flag zSign;
    uint32_t absA;
    int8_t shiftCount;
    uint64_t zSig;

    if ( a == 0 ) return packFloatx80( 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros32( absA ) + 32;
    zSig = absA;
    return packFloatx80( zSign, 0x403E - shiftCount, zSig<<shiftCount );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 32-bit two's complement integer `a' to
| the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 int32_to_float128(int32_t a, float_status *status)
{
    flag zSign;
    uint32_t absA;
    int8_t shiftCount;
    uint64_t zSig0;

    if ( a == 0 ) return packFloat128( 0, 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros32( absA ) + 17;
    zSig0 = absA;
    return packFloat128( zSign, 0x402E - shiftCount, zSig0<<shiftCount, 0 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 int64_to_float32(int64_t a, float_status *status)
{
    flag zSign;
    uint64_t absA;
    int8_t shiftCount;

    if ( a == 0 ) return float32_zero;
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros64( absA ) - 40;
    if ( 0 <= shiftCount ) {
        return packFloat32( zSign, 0x95 - shiftCount, absA<<shiftCount );
    }
    else {
        shiftCount += 7;
        if ( shiftCount < 0 ) {
            shift64RightJamming( absA, - shiftCount, &absA );
        }
        else {
            absA <<= shiftCount;
        }
        return roundAndPackFloat32(zSign, 0x9C - shiftCount, absA, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 int64_to_float64(int64_t a, float_status *status)
{
    flag zSign;

    if ( a == 0 ) return float64_zero;
    if ( a == (int64_t) LIT64( 0x8000000000000000 ) ) {
        return packFloat64( 1, 0x43E, 0 );
    }
    zSign = ( a < 0 );
    return normalizeRoundAndPackFloat64(zSign, 0x43C, zSign ? -a : a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a'
| to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 int64_to_floatx80(int64_t a, float_status *status)
{
    flag zSign;
    uint64_t absA;
    int8_t shiftCount;

    if ( a == 0 ) return packFloatx80( 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros64( absA );
    return packFloatx80( zSign, 0x403E - shiftCount, absA<<shiftCount );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit two's complement integer `a' to
| the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 int64_to_float128(int64_t a, float_status *status)
{
    flag zSign;
    uint64_t absA;
    int8_t shiftCount;
    int32_t zExp;
    uint64_t zSig0, zSig1;

    if ( a == 0 ) return packFloat128( 0, 0, 0, 0 );
    zSign = ( a < 0 );
    absA = zSign ? - a : a;
    shiftCount = countLeadingZeros64( absA ) + 49;
    zExp = 0x406E - shiftCount;
    if ( 64 <= shiftCount ) {
        zSig1 = 0;
        zSig0 = absA;
        shiftCount -= 64;
    }
    else {
        zSig1 = absA;
        zSig0 = 0;
    }
    shortShift128Left( zSig0, zSig1, shiftCount, &zSig0, &zSig1 );
    return packFloat128( zSign, zExp, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit unsigned integer `a'
| to the single-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 uint64_to_float32(uint64_t a, float_status *status)
{
    int shiftcount;

    if (a == 0) {
        return float32_zero;
    }

    /* Determine (left) shift needed to put first set bit into bit posn 23
     * (since packFloat32() expects the binary point between bits 23 and 22);
     * this is the fast case for smallish numbers.
     */
    shiftcount = countLeadingZeros64(a) - 40;
    if (shiftcount >= 0) {
        return packFloat32(0, 0x95 - shiftcount, a << shiftcount);
    }
    /* Otherwise we need to do a round-and-pack. roundAndPackFloat32()
     * expects the binary point between bits 30 and 29, hence the + 7.
     */
    shiftcount += 7;
    if (shiftcount < 0) {
        shift64RightJamming(a, -shiftcount, &a);
    } else {
        a <<= shiftcount;
    }

    return roundAndPackFloat32(0, 0x9c - shiftcount, a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit unsigned integer `a'
| to the double-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 uint64_to_float64(uint64_t a, float_status *status)
{
    int exp = 0x43C;
    int shiftcount;

    if (a == 0) {
        return float64_zero;
    }

    shiftcount = countLeadingZeros64(a) - 1;
    if (shiftcount < 0) {
        shift64RightJamming(a, -shiftcount, &a);
    } else {
        a <<= shiftcount;
    }
    return roundAndPackFloat64(0, exp - shiftcount, a, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the 64-bit unsigned integer `a'
| to the quadruple-precision floating-point format.  The conversion is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 uint64_to_float128(uint64_t a, float_status *status)
{
    if (a == 0) {
        return float128_zero;
    }
    return normalizeRoundAndPackFloat128(0, 0x406E, a, 0, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t float32_to_int32(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint32_t aSig;
    uint64_t aSig64;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( ( aExp == 0xFF ) && aSig ) aSign = 0;
    if ( aExp ) aSig |= 0x00800000;
    shiftCount = 0xAF - aExp;
    aSig64 = aSig;
    aSig64 <<= 32;
    if ( 0 < shiftCount ) shift64RightJamming( aSig64, shiftCount, &aSig64 );
    return roundAndPackInt32(aSign, aSig64, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int32_t float32_to_int32_round_to_zero(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint32_t aSig;
    int32_t z;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = aExp - 0x9E;
    if ( 0 <= shiftCount ) {
        if ( float32_val(a) != 0xCF000000 ) {
            float_raise(float_flag_invalid, status);
            if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) return 0x7FFFFFFF;
        }
        return (int32_t) 0x80000000;
    }
    else if ( aExp <= 0x7E ) {
        if (aExp | aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    aSig = ( aSig | 0x00800000 )<<8;
    z = aSig>>( - shiftCount );
    if ( (uint32_t) ( aSig<<( shiftCount & 31 ) ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 16-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int16_t float32_to_int16_round_to_zero(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint32_t aSig;
    int32_t z;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = aExp - 0x8E;
    if ( 0 <= shiftCount ) {
        if ( float32_val(a) != 0xC7000000 ) {
            float_raise(float_flag_invalid, status);
            if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) {
                return 0x7FFF;
            }
        }
        return (int32_t) 0xffff8000;
    }
    else if ( aExp <= 0x7E ) {
        if ( aExp | aSig ) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    shiftCount -= 0x10;
    aSig = ( aSig | 0x00800000 )<<8;
    z = aSig>>( - shiftCount );
    if ( (uint32_t) ( aSig<<( shiftCount & 31 ) ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( aSign ) {
        z = - z;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t float32_to_int64(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint32_t aSig;
    uint64_t aSig64, aSigExtra;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = 0xBE - aExp;
    if ( shiftCount < 0 ) {
        float_raise(float_flag_invalid, status);
        if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) {
            return LIT64( 0x7FFFFFFFFFFFFFFF );
        }
        return (int64_t) LIT64( 0x8000000000000000 );
    }
    if ( aExp ) aSig |= 0x00800000;
    aSig64 = aSig;
    aSig64 <<= 40;
    shift64ExtraRightJamming( aSig64, 0, shiftCount, &aSig64, &aSigExtra );
    return roundAndPackInt64(aSign, aSig64, aSigExtra, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| unsigned integer is returned.  Otherwise, if the conversion overflows, the
| largest unsigned integer is returned.  If the 'a' is negative, the result
| is rounded and zero is returned; values that do not round to zero will
| raise the inexact exception flag.
*----------------------------------------------------------------------------*/

uint64_t float32_to_uint64(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint32_t aSig;
    uint64_t aSig64, aSigExtra;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    if ((aSign) && (aExp > 126)) {
        float_raise(float_flag_invalid, status);
        if (float32_is_any_nan(a)) {
            return LIT64(0xFFFFFFFFFFFFFFFF);
        } else {
            return 0;
        }
    }
    shiftCount = 0xBE - aExp;
    if (aExp) {
        aSig |= 0x00800000;
    }
    if (shiftCount < 0) {
        float_raise(float_flag_invalid, status);
        return LIT64(0xFFFFFFFFFFFFFFFF);
    }

    aSig64 = aSig;
    aSig64 <<= 40;
    shift64ExtraRightJamming(aSig64, 0, shiftCount, &aSig64, &aSigExtra);
    return roundAndPackUint64(aSign, aSig64, aSigExtra, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.  If
| `a' is a NaN, the largest unsigned integer is returned.  Otherwise, if the
| conversion overflows, the largest unsigned integer is returned.  If the
| 'a' is negative, the result is rounded and zero is returned; values that do
| not round to zero will raise the inexact flag.
*----------------------------------------------------------------------------*/

uint64_t float32_to_uint64_round_to_zero(float32 a, float_status *status)
{
    signed char current_rounding_mode = status->float_rounding_mode;
    set_float_rounding_mode(float_round_to_zero, status);
    int64_t v = float32_to_uint64(a, status);
    set_float_rounding_mode(current_rounding_mode, status);
    return v;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.  If
| `a' is a NaN, the largest positive integer is returned.  Otherwise, if the
| conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int64_t float32_to_int64_round_to_zero(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint32_t aSig;
    uint64_t aSig64;
    int64_t z;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    shiftCount = aExp - 0xBE;
    if ( 0 <= shiftCount ) {
        if ( float32_val(a) != 0xDF000000 ) {
            float_raise(float_flag_invalid, status);
            if ( ! aSign || ( ( aExp == 0xFF ) && aSig ) ) {
                return LIT64( 0x7FFFFFFFFFFFFFFF );
            }
        }
        return (int64_t) LIT64( 0x8000000000000000 );
    }
    else if ( aExp <= 0x7E ) {
        if (aExp | aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    aSig64 = aSig | 0x00800000;
    aSig64 <<= 40;
    z = aSig64>>( - shiftCount );
    if ( (uint64_t) ( aSig64<<( shiftCount & 63 ) ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the double-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float64 float32_to_float64(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            return commonNaNToFloat64(float32ToCommonNaN(a, status), status);
        }
        return packFloat64( aSign, 0x7FF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( aSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    return packFloat64( aSign, aExp + 0x380, ( (uint64_t) aSig )<<29 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float32_to_floatx80(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            return commonNaNToFloatx80(float32ToCommonNaN(a, status), status);
        }
        return packFloatx80(aSign,
                            floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    aSig |= 0x00800000;
    return packFloatx80( aSign, aExp + 0x3F80, ( (uint64_t) aSig )<<40 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the single-precision floating-point value
| `a' to the double-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float128 float32_to_float128(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            return commonNaNToFloat128(float32ToCommonNaN(a, status), status);
        }
        return packFloat128( aSign, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat128( aSign, 0, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    return packFloat128( aSign, aExp + 0x3F80, ( (uint64_t) aSig )<<25, 0 );

}

/*----------------------------------------------------------------------------
| Rounds the single-precision floating-point value `a' to an integer, and
| returns the result as a single-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_round_to_int(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t lastBitMask, roundBitsMask;
    uint32_t z;
    a = float32_squash_input_denormal(a, status);

    aExp = extractFloat32Exp( a );
    if ( 0x96 <= aExp ) {
        if ( ( aExp == 0xFF ) && extractFloat32Frac( a ) ) {
            return propagateFloat32NaN(a, a, status);
        }
        return a;
    }
    if ( aExp <= 0x7E ) {
        if ( (uint32_t) ( float32_val(a)<<1 ) == 0 ) return a;
        status->float_exception_flags |= float_flag_inexact;
        aSign = extractFloat32Sign( a );
        switch (status->float_rounding_mode) {
         case float_round_nearest_even:
            if ( ( aExp == 0x7E ) && extractFloat32Frac( a ) ) {
                return packFloat32( aSign, 0x7F, 0 );
            }
            break;
        case float_round_ties_away:
            if (aExp == 0x7E) {
                return packFloat32(aSign, 0x7F, 0);
            }
            break;
         case float_round_down:
            return make_float32(aSign ? 0xBF800000 : 0);
         case float_round_up:
            return make_float32(aSign ? 0x80000000 : 0x3F800000);
        }
        return packFloat32( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x96 - aExp;
    roundBitsMask = lastBitMask - 1;
    z = float32_val(a);
    switch (status->float_rounding_mode) {
    case float_round_nearest_even:
        z += lastBitMask>>1;
        if ((z & roundBitsMask) == 0) {
            z &= ~lastBitMask;
        }
        break;
    case float_round_ties_away:
        z += lastBitMask >> 1;
        break;
    case float_round_to_zero:
        break;
    case float_round_up:
        if (!extractFloat32Sign(make_float32(z))) {
            z += roundBitsMask;
        }
        break;
    case float_round_down:
        if (extractFloat32Sign(make_float32(z))) {
            z += roundBitsMask;
        }
        break;
    default:
        abort();
    }
    z &= ~ roundBitsMask;
    if (z != float32_val(a)) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return make_float32(z);

}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the single-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 addFloat32Sigs(float32 a, float32 b, flag zSign,
                              float_status *status)
{
    int aExp, bExp, zExp;
    uint32_t aSig, bSig, zSig;
    int expDiff;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 6;
    bSig <<= 6;
    if ( 0 < expDiff ) {
        if ( aExp == 0xFF ) {
            if (aSig) {
                return propagateFloat32NaN(a, b, status);
            }
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig |= 0x20000000;
        }
        shift32RightJamming( bSig, expDiff, &bSig );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0xFF ) {
            if (bSig) {
                return propagateFloat32NaN(a, b, status);
            }
            return packFloat32( zSign, 0xFF, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig |= 0x20000000;
        }
        shift32RightJamming( aSig, - expDiff, &aSig );
        zExp = bExp;
    }
    else {
        if ( aExp == 0xFF ) {
            if (aSig | bSig) {
                return propagateFloat32NaN(a, b, status);
            }
            return a;
        }
        if ( aExp == 0 ) {
            if (status->flush_to_zero) {
                if (aSig | bSig) {
                    float_raise(float_flag_output_denormal, status);
                }
                return packFloat32(zSign, 0, 0);
            }
            return packFloat32( zSign, 0, ( aSig + bSig )>>6 );
        }
        zSig = 0x40000000 + aSig + bSig;
        zExp = aExp;
        goto roundAndPack;
    }
    aSig |= 0x20000000;
    zSig = ( aSig + bSig )<<1;
    --zExp;
    if ( (int32_t) zSig < 0 ) {
        zSig = aSig + bSig;
        ++zExp;
    }
 roundAndPack:
    return roundAndPackFloat32(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the single-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float32 subFloat32Sigs(float32 a, float32 b, flag zSign,
                              float_status *status)
{
    int aExp, bExp, zExp;
    uint32_t aSig, bSig, zSig;
    int expDiff;

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 7;
    bSig <<= 7;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0xFF ) {
        if (aSig | bSig) {
            return propagateFloat32NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloat32(status->float_rounding_mode == float_round_down, 0, 0);
 bExpBigger:
    if ( bExp == 0xFF ) {
        if (bSig) {
            return propagateFloat32NaN(a, b, status);
        }
        return packFloat32( zSign ^ 1, 0xFF, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig |= 0x40000000;
    }
    shift32RightJamming( aSig, - expDiff, &aSig );
    bSig |= 0x40000000;
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0xFF ) {
        if (aSig) {
            return propagateFloat32NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig |= 0x40000000;
    }
    shift32RightJamming( bSig, expDiff, &bSig );
    aSig |= 0x40000000;
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat32(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of adding the single-precision floating-point values `a'
| and `b'.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_add(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign == bSign ) {
        return addFloat32Sigs(a, b, aSign, status);
    }
    else {
        return subFloat32Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the single-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_sub(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    if ( aSign == bSign ) {
        return subFloat32Sigs(a, b, aSign, status);
    }
    else {
        return addFloat32Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the single-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_mul(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int aExp, bExp, zExp;
    uint32_t aSig, bSig;
    uint64_t zSig64;
    uint32_t zSig;

    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN(a, b, status);
        }
        if ( ( bExp | bSig ) == 0 ) {
            float_raise(float_flag_invalid, status);
            return float32_default_nan(status);
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( bExp == 0xFF ) {
        if (bSig) {
            return propagateFloat32NaN(a, b, status);
        }
        if ( ( aExp | aSig ) == 0 ) {
            float_raise(float_flag_invalid, status);
            return float32_default_nan(status);
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x7F;
    aSig = ( aSig | 0x00800000 )<<7;
    bSig = ( bSig | 0x00800000 )<<8;
    shift64RightJamming( ( (uint64_t) aSig ) * bSig, 32, &zSig64 );
    zSig = zSig64;
    if ( 0 <= (int32_t) ( zSig<<1 ) ) {
        zSig <<= 1;
        --zExp;
    }
    return roundAndPackFloat32(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of dividing the single-precision floating-point value `a'
| by the corresponding value `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_div(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int aExp, bExp, zExp;
    uint32_t aSig, bSig, zSig;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    bSign = extractFloat32Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0xFF ) {
        if (aSig) {
            return propagateFloat32NaN(a, b, status);
        }
        if ( bExp == 0xFF ) {
            if (bSig) {
                return propagateFloat32NaN(a, b, status);
            }
            float_raise(float_flag_invalid, status);
            return float32_default_nan(status);
        }
        return packFloat32( zSign, 0xFF, 0 );
    }
    if ( bExp == 0xFF ) {
        if (bSig) {
            return propagateFloat32NaN(a, b, status);
        }
        return packFloat32( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
                float_raise(float_flag_invalid, status);
                return float32_default_nan(status);
            }
            float_raise(float_flag_divbyzero, status);
            return packFloat32( zSign, 0xFF, 0 );
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( zSign, 0, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x7D;
    aSig = ( aSig | 0x00800000 )<<7;
    bSig = ( bSig | 0x00800000 )<<8;
    if ( bSig <= ( aSig + aSig ) ) {
        aSig >>= 1;
        ++zExp;
    }
    zSig = ( ( (uint64_t) aSig )<<32 ) / bSig;
    if ( ( zSig & 0x3F ) == 0 ) {
        zSig |= ( (uint64_t) bSig * zSig != ( (uint64_t) aSig )<<32 );
    }
    return roundAndPackFloat32(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the remainder of the single-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_rem(float32 a, float32 b, float_status *status)
{
    flag aSign, zSign;
    int aExp, bExp, expDiff;
    uint32_t aSig, bSig;
    uint32_t q;
    uint64_t aSig64, bSig64, q64;
    uint32_t alternateASig;
    int32_t sigMean;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    bSig = extractFloat32Frac( b );
    bExp = extractFloat32Exp( b );
    if ( aExp == 0xFF ) {
        if ( aSig || ( ( bExp == 0xFF ) && bSig ) ) {
            return propagateFloat32NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( bExp == 0xFF ) {
        if (bSig) {
            return propagateFloat32NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            float_raise(float_flag_invalid, status);
            return float32_default_nan(status);
        }
        normalizeFloat32Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig |= 0x00800000;
    bSig |= 0x00800000;
    if ( expDiff < 32 ) {
        aSig <<= 8;
        bSig <<= 8;
        if ( expDiff < 0 ) {
            if ( expDiff < -1 ) return a;
            aSig >>= 1;
        }
        q = ( bSig <= aSig );
        if ( q ) aSig -= bSig;
        if ( 0 < expDiff ) {
            q = ( ( (uint64_t) aSig )<<32 ) / bSig;
            q >>= 32 - expDiff;
            bSig >>= 2;
            aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
        }
        else {
            aSig >>= 2;
            bSig >>= 2;
        }
    }
    else {
        if ( bSig <= aSig ) aSig -= bSig;
        aSig64 = ( (uint64_t) aSig )<<40;
        bSig64 = ( (uint64_t) bSig )<<40;
        expDiff -= 64;
        while ( 0 < expDiff ) {
            q64 = estimateDiv128To64( aSig64, 0, bSig64 );
            q64 = ( 2 < q64 ) ? q64 - 2 : 0;
            aSig64 = - ( ( bSig * q64 )<<38 );
            expDiff -= 62;
        }
        expDiff += 64;
        q64 = estimateDiv128To64( aSig64, 0, bSig64 );
        q64 = ( 2 < q64 ) ? q64 - 2 : 0;
        q = q64>>( 64 - expDiff );
        bSig <<= 6;
        aSig = ( ( aSig64>>33 )<<( expDiff - 1 ) ) - bSig * q;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (int32_t) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (int32_t) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat32(aSign ^ zSign, bExp, aSig, status);
}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the single-precision floating-point values
| `a' and `b' then adding 'c', with no intermediate rounding step after the
| multiplication.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic 754-2008.
| The flags argument allows the caller to select negation of the
| addend, the intermediate product, or the final result. (The difference
| between this and having the caller do a separate negation is that negating
| externally will flip the sign bit on NaNs.)
*----------------------------------------------------------------------------*/

float32 float32_muladd(float32 a, float32 b, float32 c, int flags,
                       float_status *status)
{
    flag aSign, bSign, cSign, zSign;
    int aExp, bExp, cExp, pExp, zExp, expDiff;
    uint32_t aSig, bSig, cSig;
    flag pInf, pZero, pSign;
    uint64_t pSig64, cSig64, zSig64;
    uint32_t pSig;
    int shiftcount;
    flag signflip, infzero;

    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);
    c = float32_squash_input_denormal(c, status);
    aSig = extractFloat32Frac(a);
    aExp = extractFloat32Exp(a);
    aSign = extractFloat32Sign(a);
    bSig = extractFloat32Frac(b);
    bExp = extractFloat32Exp(b);
    bSign = extractFloat32Sign(b);
    cSig = extractFloat32Frac(c);
    cExp = extractFloat32Exp(c);
    cSign = extractFloat32Sign(c);

    infzero = ((aExp == 0 && aSig == 0 && bExp == 0xff && bSig == 0) ||
               (aExp == 0xff && aSig == 0 && bExp == 0 && bSig == 0));

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (((aExp == 0xff) && aSig) ||
        ((bExp == 0xff) && bSig) ||
        ((cExp == 0xff) && cSig)) {
        return propagateFloat32MulAddNaN(a, b, c, infzero, status);
    }

    if (infzero) {
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }

    if (flags & float_muladd_negate_c) {
        cSign ^= 1;
    }

    signflip = (flags & float_muladd_negate_result) ? 1 : 0;

    /* Work out the sign and type of the product */
    pSign = aSign ^ bSign;
    if (flags & float_muladd_negate_product) {
        pSign ^= 1;
    }
    pInf = (aExp == 0xff) || (bExp == 0xff);
    pZero = ((aExp | aSig) == 0) || ((bExp | bSig) == 0);

    if (cExp == 0xff) {
        if (pInf && (pSign ^ cSign)) {
            /* addition of opposite-signed infinities => InvalidOperation */
            float_raise(float_flag_invalid, status);
            return float32_default_nan(status);
        }
        /* Otherwise generate an infinity of the same sign */
        return packFloat32(cSign ^ signflip, 0xff, 0);
    }

    if (pInf) {
        return packFloat32(pSign ^ signflip, 0xff, 0);
    }

    if (pZero) {
        if (cExp == 0) {
            if (cSig == 0) {
                /* Adding two exact zeroes */
                if (pSign == cSign) {
                    zSign = pSign;
                } else if (status->float_rounding_mode == float_round_down) {
                    zSign = 1;
                } else {
                    zSign = 0;
                }
                return packFloat32(zSign ^ signflip, 0, 0);
            }
            /* Exact zero plus a denorm */
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat32(cSign ^ signflip, 0, 0);
            }
        }
        /* Zero plus something non-zero : just return the something */
        if (flags & float_muladd_halve_result) {
            if (cExp == 0) {
                normalizeFloat32Subnormal(cSig, &cExp, &cSig);
            }
            /* Subtract one to halve, and one again because roundAndPackFloat32
             * wants one less than the true exponent.
             */
            cExp -= 2;
            cSig = (cSig | 0x00800000) << 7;
            return roundAndPackFloat32(cSign ^ signflip, cExp, cSig, status);
        }
        return packFloat32(cSign ^ signflip, cExp, cSig);
    }

    if (aExp == 0) {
        normalizeFloat32Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        normalizeFloat32Subnormal(bSig, &bExp, &bSig);
    }

    /* Calculate the actual result a * b + c */

    /* Multiply first; this is easy. */
    /* NB: we subtract 0x7e where float32_mul() subtracts 0x7f
     * because we want the true exponent, not the "one-less-than"
     * flavour that roundAndPackFloat32() takes.
     */
    pExp = aExp + bExp - 0x7e;
    aSig = (aSig | 0x00800000) << 7;
    bSig = (bSig | 0x00800000) << 8;
    pSig64 = (uint64_t)aSig * bSig;
    if ((int64_t)(pSig64 << 1) >= 0) {
        pSig64 <<= 1;
        pExp--;
    }

    zSign = pSign ^ signflip;

    /* Now pSig64 is the significand of the multiply, with the explicit bit in
     * position 62.
     */
    if (cExp == 0) {
        if (!cSig) {
            /* Throw out the special case of c being an exact zero now */
            shift64RightJamming(pSig64, 32, &pSig64);
            pSig = pSig64;
            if (flags & float_muladd_halve_result) {
                pExp--;
            }
            return roundAndPackFloat32(zSign, pExp - 1,
                                       pSig, status);
        }
        normalizeFloat32Subnormal(cSig, &cExp, &cSig);
    }

    cSig64 = (uint64_t)cSig << (62 - 23);
    cSig64 |= LIT64(0x4000000000000000);
    expDiff = pExp - cExp;

    if (pSign == cSign) {
        /* Addition */
        if (expDiff > 0) {
            /* scale c to match p */
            shift64RightJamming(cSig64, expDiff, &cSig64);
            zExp = pExp;
        } else if (expDiff < 0) {
            /* scale p to match c */
            shift64RightJamming(pSig64, -expDiff, &pSig64);
            zExp = cExp;
        } else {
            /* no scaling needed */
            zExp = cExp;
        }
        /* Add significands and make sure explicit bit ends up in posn 62 */
        zSig64 = pSig64 + cSig64;
        if ((int64_t)zSig64 < 0) {
            shift64RightJamming(zSig64, 1, &zSig64);
        } else {
            zExp--;
        }
    } else {
        /* Subtraction */
        if (expDiff > 0) {
            shift64RightJamming(cSig64, expDiff, &cSig64);
            zSig64 = pSig64 - cSig64;
            zExp = pExp;
        } else if (expDiff < 0) {
            shift64RightJamming(pSig64, -expDiff, &pSig64);
            zSig64 = cSig64 - pSig64;
            zExp = cExp;
            zSign ^= 1;
        } else {
            zExp = pExp;
            if (cSig64 < pSig64) {
                zSig64 = pSig64 - cSig64;
            } else if (pSig64 < cSig64) {
                zSig64 = cSig64 - pSig64;
                zSign ^= 1;
            } else {
                /* Exact zero */
                zSign = signflip;
                if (status->float_rounding_mode == float_round_down) {
                    zSign ^= 1;
                }
                return packFloat32(zSign, 0, 0);
            }
        }
        --zExp;
        /* Normalize to put the explicit bit back into bit 62. */
        shiftcount = countLeadingZeros64(zSig64) - 1;
        zSig64 <<= shiftcount;
        zExp -= shiftcount;
    }
    if (flags & float_muladd_halve_result) {
        zExp--;
    }

    shift64RightJamming(zSig64, 32, &zSig64);
    return roundAndPackFloat32(zSign, zExp, zSig64, status);
}


/*----------------------------------------------------------------------------
| Returns the square root of the single-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 float32_sqrt(float32 a, float_status *status)
{
    flag aSign;
    int aExp, zExp;
    uint32_t aSig, zSig;
    uint64_t rem, term;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            return propagateFloat32NaN(a, float32_zero, status);
        }
        if ( ! aSign ) return a;
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( aSign ) {
        if ( ( aExp | aSig ) == 0 ) return a;
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return float32_zero;
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    zExp = ( ( aExp - 0x7F )>>1 ) + 0x7E;
    aSig = ( aSig | 0x00800000 )<<8;
    zSig = estimateSqrt32( aExp, aSig ) + 2;
    if ( ( zSig & 0x7F ) <= 5 ) {
        if ( zSig < 2 ) {
            zSig = 0x7FFFFFFF;
            goto roundAndPack;
        }
        aSig >>= aExp & 1;
        term = ( (uint64_t) zSig ) * zSig;
        rem = ( ( (uint64_t) aSig )<<32 ) - term;
        while ( (int64_t) rem < 0 ) {
            --zSig;
            rem += ( ( (uint64_t) zSig )<<1 ) | 1;
        }
        zSig |= ( rem != 0 );
    }
    shift32RightJamming( zSig, 1, &zSig );
 roundAndPack:
    return roundAndPackFloat32(0, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the binary exponential of the single-precision floating-point value
| `a'. The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
|
| Uses the following identities:
|
| 1. -------------------------------------------------------------------------
|      x    x*ln(2)
|     2  = e
|
| 2. -------------------------------------------------------------------------
|                      2     3     4     5           n
|      x        x     x     x     x     x           x
|     e  = 1 + --- + --- + --- + --- + --- + ... + --- + ...
|               1!    2!    3!    4!    5!          n!
*----------------------------------------------------------------------------*/

static const float64 float32_exp2_coefficients[15] =
{
    const_float64( 0x3ff0000000000000ll ), /*  1 */
    const_float64( 0x3fe0000000000000ll ), /*  2 */
    const_float64( 0x3fc5555555555555ll ), /*  3 */
    const_float64( 0x3fa5555555555555ll ), /*  4 */
    const_float64( 0x3f81111111111111ll ), /*  5 */
    const_float64( 0x3f56c16c16c16c17ll ), /*  6 */
    const_float64( 0x3f2a01a01a01a01all ), /*  7 */
    const_float64( 0x3efa01a01a01a01all ), /*  8 */
    const_float64( 0x3ec71de3a556c734ll ), /*  9 */
    const_float64( 0x3e927e4fb7789f5cll ), /* 10 */
    const_float64( 0x3e5ae64567f544e4ll ), /* 11 */
    const_float64( 0x3e21eed8eff8d898ll ), /* 12 */
    const_float64( 0x3de6124613a86d09ll ), /* 13 */
    const_float64( 0x3da93974a8c07c9dll ), /* 14 */
    const_float64( 0x3d6ae7f3e733b81fll ), /* 15 */
};

float32 float32_exp2(float32 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;
    float64 r, x, xn;
    int i;
    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );

    if ( aExp == 0xFF) {
        if (aSig) {
            return propagateFloat32NaN(a, float32_zero, status);
        }
        return (aSign) ? float32_zero : a;
    }
    if (aExp == 0) {
        if (aSig == 0) return float32_one;
    }

    float_raise(float_flag_inexact, status);

    /* ******************************* */
    /* using float64 for approximation */
    /* ******************************* */
    x = float32_to_float64(a, status);
    x = float64_mul(x, float64_ln2, status);

    xn = x;
    r = float64_one;
    for (i = 0 ; i < 15 ; i++) {
        float64 f;

        f = float64_mul(xn, float32_exp2_coefficients[i], status);
        r = float64_add(r, f, status);

        xn = float64_mul(xn, x, status);
    }

    return float64_to_float32(r, status);
}

/*----------------------------------------------------------------------------
| Returns the binary log of the single-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
float32 float32_log2(float32 a, float_status *status)
{
    flag aSign, zSign;
    int aExp;
    uint32_t aSig, zSig, i;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );

    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat32( 1, 0xFF, 0 );
        normalizeFloat32Subnormal( aSig, &aExp, &aSig );
    }
    if ( aSign ) {
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    if ( aExp == 0xFF ) {
        if (aSig) {
            return propagateFloat32NaN(a, float32_zero, status);
        }
        return a;
    }

    aExp -= 0x7F;
    aSig |= 0x00800000;
    zSign = aExp < 0;
    zSig = aExp << 23;

    for (i = 1 << 22; i > 0; i >>= 1) {
        aSig = ( (uint64_t)aSig * aSig ) >> 23;
        if ( aSig & 0x01000000 ) {
            aSig >>= 1;
            zSig |= i;
        }
    }

    if ( zSign )
        zSig = -zSig;

    return normalizeRoundAndPackFloat32(zSign, 0x85, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_eq(float32 a, float32 b, float_status *status)
{
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    av = float32_val(a);
    bv = float32_val(b);
    return ( av == bv ) || ( (uint32_t) ( ( av | bv )<<1 ) == 0 );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_le(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign || ( (uint32_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  The comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_lt(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign && ( (uint32_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  The invalid exception is raised if either
| operand is a NaN.  The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_unordered(float32 a, float32 b, float_status *status)
{
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  The comparison is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_eq_quiet(float32 a, float32 b, float_status *status)
{
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    return ( float32_val(a) == float32_val(b) ) ||
            ( (uint32_t) ( ( float32_val(a) | float32_val(b) )<<1 ) == 0 );
}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_le_quiet(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign || ( (uint32_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_lt_quiet(float32 a, float32 b, float_status *status)
{
    flag aSign, bSign;
    uint32_t av, bv;
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat32Sign( a );
    bSign = extractFloat32Sign( b );
    av = float32_val(a);
    bv = float32_val(b);
    if ( aSign != bSign ) return aSign && ( (uint32_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the single-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.  The
| comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float32_unordered_quiet(float32 a, float32 b, float_status *status)
{
    a = float32_squash_input_denormal(a, status);
    b = float32_squash_input_denormal(b, status);

    if (    ( ( extractFloat32Exp( a ) == 0xFF ) && extractFloat32Frac( a ) )
         || ( ( extractFloat32Exp( b ) == 0xFF ) && extractFloat32Frac( b ) )
       ) {
        if (float32_is_signaling_nan(a, status)
         || float32_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t float64_to_int32(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
    if ( aExp ) aSig |= LIT64( 0x0010000000000000 );
    shiftCount = 0x42C - aExp;
    if ( 0 < shiftCount ) shift64RightJamming( aSig, shiftCount, &aSig );
    return roundAndPackInt32(aSign, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 32-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int32_t float64_to_int32_round_to_zero(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig, savedASig;
    int32_t z;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( 0x41E < aExp ) {
        if ( ( aExp == 0x7FF ) && aSig ) aSign = 0;
        goto invalid;
    }
    else if ( aExp < 0x3FF ) {
        if (aExp || aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    aSig |= LIT64( 0x0010000000000000 );
    shiftCount = 0x433 - aExp;
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise(float_flag_invalid, status);
        return aSign ? (int32_t) 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 16-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int16_t float64_to_int16_round_to_zero(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig, savedASig;
    int32_t z;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( 0x40E < aExp ) {
        if ( ( aExp == 0x7FF ) && aSig ) {
            aSign = 0;
        }
        goto invalid;
    }
    else if ( aExp < 0x3FF ) {
        if ( aExp || aSig ) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    aSig |= LIT64( 0x0010000000000000 );
    shiftCount = 0x433 - aExp;
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) {
        z = - z;
    }
    if ( ( (int16_t)z < 0 ) ^ aSign ) {
 invalid:
        float_raise(float_flag_invalid, status);
        return aSign ? (int32_t) 0xffff8000 : 0x7FFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t float64_to_int64(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig, aSigExtra;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp ) aSig |= LIT64( 0x0010000000000000 );
    shiftCount = 0x433 - aExp;
    if ( shiftCount <= 0 ) {
        if ( 0x43E < aExp ) {
            float_raise(float_flag_invalid, status);
            if (    ! aSign
                 || (    ( aExp == 0x7FF )
                      && ( aSig != LIT64( 0x0010000000000000 ) ) )
               ) {
                return LIT64( 0x7FFFFFFFFFFFFFFF );
            }
            return (int64_t) LIT64( 0x8000000000000000 );
        }
        aSigExtra = 0;
        aSig <<= - shiftCount;
    }
    else {
        shift64ExtraRightJamming( aSig, 0, shiftCount, &aSig, &aSigExtra );
    }
    return roundAndPackInt64(aSign, aSig, aSigExtra, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit two's complement integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int64_t float64_to_int64_round_to_zero(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig;
    int64_t z;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp ) aSig |= LIT64( 0x0010000000000000 );
    shiftCount = aExp - 0x433;
    if ( 0 <= shiftCount ) {
        if ( 0x43E <= aExp ) {
            if ( float64_val(a) != LIT64( 0xC3E0000000000000 ) ) {
                float_raise(float_flag_invalid, status);
                if (    ! aSign
                     || (    ( aExp == 0x7FF )
                          && ( aSig != LIT64( 0x0010000000000000 ) ) )
                   ) {
                    return LIT64( 0x7FFFFFFFFFFFFFFF );
                }
            }
            return (int64_t) LIT64( 0x8000000000000000 );
        }
        z = aSig<<shiftCount;
    }
    else {
        if ( aExp < 0x3FE ) {
            if (aExp | aSig) {
                status->float_exception_flags |= float_flag_inexact;
            }
            return 0;
        }
        z = aSig>>( - shiftCount );
        if ( (uint64_t) ( aSig<<( shiftCount & 63 ) ) ) {
            status->float_exception_flags |= float_flag_inexact;
        }
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the single-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float32 float64_to_float32(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig;
    uint32_t zSig;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return commonNaNToFloat32(float64ToCommonNaN(a, status), status);
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    shift64RightJamming( aSig, 22, &aSig );
    zSig = aSig;
    if ( aExp || zSig ) {
        zSig |= 0x40000000;
        aExp -= 0x381;
    }
    return roundAndPackFloat32(aSign, aExp, zSig, status);

}


/*----------------------------------------------------------------------------
| Packs the sign `zSign', exponent `zExp', and significand `zSig' into a
| half-precision floating-point value, returning the result.  After being
| shifted into the proper positions, the three fields are simply added
| together to form the result.  This means that any integer portion of `zSig'
| will be added into the exponent.  Since a properly normalized significand
| will have an integer portion equal to 1, the `zExp' input should be 1 less
| than the desired result exponent whenever `zSig' is a complete, normalized
| significand.
*----------------------------------------------------------------------------*/
static float16 packFloat16(flag zSign, int zExp, uint16_t zSig)
{
    return make_float16(
        (((uint32_t)zSign) << 15) + (((uint32_t)zExp) << 10) + zSig);
}

/*----------------------------------------------------------------------------
| Takes an abstract floating-point value having sign `zSign', exponent `zExp',
| and significand `zSig', and returns the proper half-precision floating-
| point value corresponding to the abstract input.  Ordinarily, the abstract
| value is simply rounded and packed into the half-precision format, with
| the inexact exception raised if the abstract input cannot be represented
| exactly.  However, if the abstract value is too large, the overflow and
| inexact exceptions are raised and an infinity or maximal finite value is
| returned.  If the abstract value is too small, the input value is rounded to
| a subnormal number, and the underflow and inexact exceptions are raised if
| the abstract input cannot be represented exactly as a subnormal half-
| precision floating-point number.
| The `ieee' flag indicates whether to use IEEE standard half precision, or
| ARM-style "alternative representation", which omits the NaN and Inf
| encodings in order to raise the maximum representable exponent by one.
|     The input significand `zSig' has its binary point between bits 22
| and 23, which is 13 bits to the left of the usual location.  This shifted
| significand must be normalized or smaller.  If `zSig' is not normalized,
| `zExp' must be 0; in that case, the result returned is a subnormal number,
| and it must not require rounding.  In the usual case that `zSig' is
| normalized, `zExp' must be 1 less than the ``true'' floating-point exponent.
| Note the slightly odd position of the binary point in zSig compared with the
| other roundAndPackFloat functions. This should probably be fixed if we
| need to implement more float16 routines than just conversion.
| The handling of underflow and overflow follows the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float16 roundAndPackFloat16(flag zSign, int zExp,
                                   uint32_t zSig, flag ieee,
                                   float_status *status)
{
    int maxexp = ieee ? 29 : 30;
    uint32_t mask;
    uint32_t increment;
    bool rounding_bumps_exp;
    bool is_tiny = false;

    /* Calculate the mask of bits of the mantissa which are not
     * representable in half-precision and will be lost.
     */
    if (zExp < 1) {
        /* Will be denormal in halfprec */
        mask = 0x00ffffff;
        if (zExp >= -11) {
            mask >>= 11 + zExp;
        }
    } else {
        /* Normal number in halfprec */
        mask = 0x00001fff;
    }

    switch (status->float_rounding_mode) {
    case float_round_nearest_even:
        increment = (mask + 1) >> 1;
        if ((zSig & mask) == increment) {
            increment = zSig & (increment << 1);
        }
        break;
    case float_round_ties_away:
        increment = (mask + 1) >> 1;
        break;
    case float_round_up:
        increment = zSign ? 0 : mask;
        break;
    case float_round_down:
        increment = zSign ? mask : 0;
        break;
    default: /* round_to_zero */
        increment = 0;
        break;
    }

    rounding_bumps_exp = (zSig + increment >= 0x01000000);

    if (zExp > maxexp || (zExp == maxexp && rounding_bumps_exp)) {
        if (ieee) {
            float_raise(float_flag_overflow | float_flag_inexact, status);
            return packFloat16(zSign, 0x1f, 0);
        } else {
            float_raise(float_flag_invalid, status);
            return packFloat16(zSign, 0x1f, 0x3ff);
        }
    }

    if (zExp < 0) {
        /* Note that flush-to-zero does not affect half-precision results */
        is_tiny =
            (status->float_detect_tininess == float_tininess_before_rounding)
            || (zExp < -1)
            || (!rounding_bumps_exp);
    }
    if (zSig & mask) {
        float_raise(float_flag_inexact, status);
        if (is_tiny) {
            float_raise(float_flag_underflow, status);
        }
    }

    zSig += increment;
    if (rounding_bumps_exp) {
        zSig >>= 1;
        zExp++;
    }

    if (zExp < -10) {
        return packFloat16(zSign, 0, 0);
    }
    if (zExp < 0) {
        zSig >>= -zExp;
        zExp = 0;
    }
    return packFloat16(zSign, zExp, zSig >> 13);
}

static void normalizeFloat16Subnormal(uint32_t aSig, int *zExpPtr,
                                      uint32_t *zSigPtr)
{
    int8_t shiftCount = countLeadingZeros32(aSig) - 21;
    *zSigPtr = aSig << shiftCount;
    *zExpPtr = 1 - shiftCount;
}

/* Half precision floats come in two formats: standard IEEE and "ARM" format.
   The latter gains extra exponent range by omitting the NaN/Inf encodings.  */

float32 float16_to_float32(float16 a, flag ieee, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    aSign = extractFloat16Sign(a);
    aExp = extractFloat16Exp(a);
    aSig = extractFloat16Frac(a);

    if (aExp == 0x1f && ieee) {
        if (aSig) {
            return commonNaNToFloat32(float16ToCommonNaN(a, status), status);
        }
        return packFloat32(aSign, 0xff, 0);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return packFloat32(aSign, 0, 0);
        }

        normalizeFloat16Subnormal(aSig, &aExp, &aSig);
        aExp--;
    }
    return packFloat32( aSign, aExp + 0x70, aSig << 13);
}

float16 float32_to_float16(float32 a, flag ieee, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    a = float32_squash_input_denormal(a, status);

    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );
    if ( aExp == 0xFF ) {
        if (aSig) {
            /* Input is a NaN */
            if (!ieee) {
                float_raise(float_flag_invalid, status);
                return packFloat16(aSign, 0, 0);
            }
            return commonNaNToFloat16(
                float32ToCommonNaN(a, status), status);
        }
        /* Infinity */
        if (!ieee) {
            float_raise(float_flag_invalid, status);
            return packFloat16(aSign, 0x1f, 0x3ff);
        }
        return packFloat16(aSign, 0x1f, 0);
    }
    if (aExp == 0 && aSig == 0) {
        return packFloat16(aSign, 0, 0);
    }
    /* Decimal point between bits 22 and 23. Note that we add the 1 bit
     * even if the input is denormal; however this is harmless because
     * the largest possible single-precision denormal is still smaller
     * than the smallest representable half-precision denormal, and so we
     * will end up ignoring aSig and returning via the "always return zero"
     * codepath.
     */
    aSig |= 0x00800000;
    aExp -= 0x71;

    return roundAndPackFloat16(aSign, aExp, aSig, ieee, status);
}

float64 float16_to_float64(float16 a, flag ieee, float_status *status)
{
    flag aSign;
    int aExp;
    uint32_t aSig;

    aSign = extractFloat16Sign(a);
    aExp = extractFloat16Exp(a);
    aSig = extractFloat16Frac(a);

    if (aExp == 0x1f && ieee) {
        if (aSig) {
            return commonNaNToFloat64(
                float16ToCommonNaN(a, status), status);
        }
        return packFloat64(aSign, 0x7ff, 0);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return packFloat64(aSign, 0, 0);
        }

        normalizeFloat16Subnormal(aSig, &aExp, &aSig);
        aExp--;
    }
    return packFloat64(aSign, aExp + 0x3f0, ((uint64_t)aSig) << 42);
}

float16 float64_to_float16(float64 a, flag ieee, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig;
    uint32_t zSig;

    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    if (aExp == 0x7FF) {
        if (aSig) {
            /* Input is a NaN */
            if (!ieee) {
                float_raise(float_flag_invalid, status);
                return packFloat16(aSign, 0, 0);
            }
            return commonNaNToFloat16(
                float64ToCommonNaN(a, status), status);
        }
        /* Infinity */
        if (!ieee) {
            float_raise(float_flag_invalid, status);
            return packFloat16(aSign, 0x1f, 0x3ff);
        }
        return packFloat16(aSign, 0x1f, 0);
    }
    shift64RightJamming(aSig, 29, &aSig);
    zSig = aSig;
    if (aExp == 0 && zSig == 0) {
        return packFloat16(aSign, 0, 0);
    }
    /* Decimal point between bits 22 and 23. Note that we add the 1 bit
     * even if the input is denormal; however this is harmless because
     * the largest possible single-precision denormal is still smaller
     * than the smallest representable half-precision denormal, and so we
     * will end up ignoring aSig and returning via the "always return zero"
     * codepath.
     */
    zSig |= 0x00800000;
    aExp -= 0x3F1;

    return roundAndPackFloat16(aSign, aExp, zSig, ieee, status);
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the extended double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float64_to_floatx80(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig;

    a = float64_squash_input_denormal(a, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return commonNaNToFloatx80(float64ToCommonNaN(a, status), status);
        }
        return packFloatx80(aSign,
                            floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    return
        packFloatx80(
            aSign, aExp + 0x3C00, ( aSig | LIT64( 0x0010000000000000 ) )<<11 );

}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the quadruple-precision floating-point format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float128 float64_to_float128(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig, zSig0, zSig1;

    a = float64_squash_input_denormal(a, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return commonNaNToFloat128(float64ToCommonNaN(a, status), status);
        }
        return packFloat128( aSign, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat128( aSign, 0, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
        --aExp;
    }
    shift128Right( aSig, 0, 4, &zSig0, &zSig1 );
    return packFloat128( aSign, aExp + 0x3C00, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Rounds the double-precision floating-point value `a' to an integer, and
| returns the result as a double-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_round_to_int(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t lastBitMask, roundBitsMask;
    uint64_t z;
    a = float64_squash_input_denormal(a, status);

    aExp = extractFloat64Exp( a );
    if ( 0x433 <= aExp ) {
        if ( ( aExp == 0x7FF ) && extractFloat64Frac( a ) ) {
            return propagateFloat64NaN(a, a, status);
        }
        return a;
    }
    if ( aExp < 0x3FF ) {
        if ( (uint64_t) ( float64_val(a)<<1 ) == 0 ) return a;
        status->float_exception_flags |= float_flag_inexact;
        aSign = extractFloat64Sign( a );
        switch (status->float_rounding_mode) {
         case float_round_nearest_even:
            if ( ( aExp == 0x3FE ) && extractFloat64Frac( a ) ) {
                return packFloat64( aSign, 0x3FF, 0 );
            }
            break;
        case float_round_ties_away:
            if (aExp == 0x3FE) {
                return packFloat64(aSign, 0x3ff, 0);
            }
            break;
         case float_round_down:
            return make_float64(aSign ? LIT64( 0xBFF0000000000000 ) : 0);
         case float_round_up:
            return make_float64(
            aSign ? LIT64( 0x8000000000000000 ) : LIT64( 0x3FF0000000000000 ));
        }
        return packFloat64( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x433 - aExp;
    roundBitsMask = lastBitMask - 1;
    z = float64_val(a);
    switch (status->float_rounding_mode) {
    case float_round_nearest_even:
        z += lastBitMask >> 1;
        if ((z & roundBitsMask) == 0) {
            z &= ~lastBitMask;
        }
        break;
    case float_round_ties_away:
        z += lastBitMask >> 1;
        break;
    case float_round_to_zero:
        break;
    case float_round_up:
        if (!extractFloat64Sign(make_float64(z))) {
            z += roundBitsMask;
        }
        break;
    case float_round_down:
        if (extractFloat64Sign(make_float64(z))) {
            z += roundBitsMask;
        }
        break;
    default:
        abort();
    }
    z &= ~ roundBitsMask;
    if (z != float64_val(a)) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return make_float64(z);

}

float64 float64_trunc_to_int(float64 a, float_status *status)
{
    int oldmode;
    float64 res;
    oldmode = status->float_rounding_mode;
    status->float_rounding_mode = float_round_to_zero;
    res = float64_round_to_int(a, status);
    status->float_rounding_mode = oldmode;
    return res;
}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the double-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float64 addFloat64Sigs(float64 a, float64 b, flag zSign,
                              float_status *status)
{
    int aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig;
    int expDiff;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 9;
    bSig <<= 9;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FF ) {
            if (aSig) {
                return propagateFloat64NaN(a, b, status);
            }
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig |= LIT64( 0x2000000000000000 );
        }
        shift64RightJamming( bSig, expDiff, &bSig );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FF ) {
            if (bSig) {
                return propagateFloat64NaN(a, b, status);
            }
            return packFloat64( zSign, 0x7FF, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig |= LIT64( 0x2000000000000000 );
        }
        shift64RightJamming( aSig, - expDiff, &aSig );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FF ) {
            if (aSig | bSig) {
                return propagateFloat64NaN(a, b, status);
            }
            return a;
        }
        if ( aExp == 0 ) {
            if (status->flush_to_zero) {
                if (aSig | bSig) {
                    float_raise(float_flag_output_denormal, status);
                }
                return packFloat64(zSign, 0, 0);
            }
            return packFloat64( zSign, 0, ( aSig + bSig )>>9 );
        }
        zSig = LIT64( 0x4000000000000000 ) + aSig + bSig;
        zExp = aExp;
        goto roundAndPack;
    }
    aSig |= LIT64( 0x2000000000000000 );
    zSig = ( aSig + bSig )<<1;
    --zExp;
    if ( (int64_t) zSig < 0 ) {
        zSig = aSig + bSig;
        ++zExp;
    }
 roundAndPack:
    return roundAndPackFloat64(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the double-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float64 subFloat64Sigs(float64 a, float64 b, flag zSign,
                              float_status *status)
{
    int aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig;
    int expDiff;

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    expDiff = aExp - bExp;
    aSig <<= 10;
    bSig <<= 10;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FF ) {
        if (aSig | bSig) {
            return propagateFloat64NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloat64(status->float_rounding_mode == float_round_down, 0, 0);
 bExpBigger:
    if ( bExp == 0x7FF ) {
        if (bSig) {
            return propagateFloat64NaN(a, b, status);
        }
        return packFloat64( zSign ^ 1, 0x7FF, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig |= LIT64( 0x4000000000000000 );
    }
    shift64RightJamming( aSig, - expDiff, &aSig );
    bSig |= LIT64( 0x4000000000000000 );
 bBigger:
    zSig = bSig - aSig;
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return propagateFloat64NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig |= LIT64( 0x4000000000000000 );
    }
    shift64RightJamming( bSig, expDiff, &bSig );
    aSig |= LIT64( 0x4000000000000000 );
 aBigger:
    zSig = aSig - bSig;
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat64(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of adding the double-precision floating-point values `a'
| and `b'.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_add(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign == bSign ) {
        return addFloat64Sigs(a, b, aSign, status);
    }
    else {
        return subFloat64Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the double-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_sub(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    if ( aSign == bSign ) {
        return subFloat64Sigs(a, b, aSign, status);
    }
    else {
        return addFloat64Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the double-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_mul(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FF ) {
        if ( aSig || ( ( bExp == 0x7FF ) && bSig ) ) {
            return propagateFloat64NaN(a, b, status);
        }
        if ( ( bExp | bSig ) == 0 ) {
            float_raise(float_flag_invalid, status);
            return float64_default_nan(status);
        }
        return packFloat64( zSign, 0x7FF, 0 );
    }
    if ( bExp == 0x7FF ) {
        if (bSig) {
            return propagateFloat64NaN(a, b, status);
        }
        if ( ( aExp | aSig ) == 0 ) {
            float_raise(float_flag_invalid, status);
            return float64_default_nan(status);
        }
        return packFloat64( zSign, 0x7FF, 0 );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( zSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloat64( zSign, 0, 0 );
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x3FF;
    aSig = ( aSig | LIT64( 0x0010000000000000 ) )<<10;
    bSig = ( bSig | LIT64( 0x0010000000000000 ) )<<11;
    mul64To128( aSig, bSig, &zSig0, &zSig1 );
    zSig0 |= ( zSig1 != 0 );
    if ( 0 <= (int64_t) ( zSig0<<1 ) ) {
        zSig0 <<= 1;
        --zExp;
    }
    return roundAndPackFloat64(zSign, zExp, zSig0, status);

}

/*----------------------------------------------------------------------------
| Returns the result of dividing the double-precision floating-point value `a'
| by the corresponding value `b'.  The operation is performed according to
| the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_div(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig;
    uint64_t rem0, rem1;
    uint64_t term0, term1;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    bSign = extractFloat64Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return propagateFloat64NaN(a, b, status);
        }
        if ( bExp == 0x7FF ) {
            if (bSig) {
                return propagateFloat64NaN(a, b, status);
            }
            float_raise(float_flag_invalid, status);
            return float64_default_nan(status);
        }
        return packFloat64( zSign, 0x7FF, 0 );
    }
    if ( bExp == 0x7FF ) {
        if (bSig) {
            return propagateFloat64NaN(a, b, status);
        }
        return packFloat64( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
                float_raise(float_flag_invalid, status);
                return float64_default_nan(status);
            }
            float_raise(float_flag_divbyzero, status);
            return packFloat64( zSign, 0x7FF, 0 );
        }
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( zSign, 0, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x3FD;
    aSig = ( aSig | LIT64( 0x0010000000000000 ) )<<10;
    bSig = ( bSig | LIT64( 0x0010000000000000 ) )<<11;
    if ( bSig <= ( aSig + aSig ) ) {
        aSig >>= 1;
        ++zExp;
    }
    zSig = estimateDiv128To64( aSig, 0, bSig );
    if ( ( zSig & 0x1FF ) <= 2 ) {
        mul64To128( bSig, zSig, &term0, &term1 );
        sub128( aSig, 0, term0, term1, &rem0, &rem1 );
        while ( (int64_t) rem0 < 0 ) {
            --zSig;
            add128( rem0, rem1, 0, bSig, &rem0, &rem1 );
        }
        zSig |= ( rem1 != 0 );
    }
    return roundAndPackFloat64(zSign, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the remainder of the double-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_rem(float64 a, float64 b, float_status *status)
{
    flag aSign, zSign;
    int aExp, bExp, expDiff;
    uint64_t aSig, bSig;
    uint64_t q, alternateASig;
    int64_t sigMean;

    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    bSig = extractFloat64Frac( b );
    bExp = extractFloat64Exp( b );
    if ( aExp == 0x7FF ) {
        if ( aSig || ( ( bExp == 0x7FF ) && bSig ) ) {
            return propagateFloat64NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( bExp == 0x7FF ) {
        if (bSig) {
            return propagateFloat64NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            float_raise(float_flag_invalid, status);
            return float64_default_nan(status);
        }
        normalizeFloat64Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return a;
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    expDiff = aExp - bExp;
    aSig = ( aSig | LIT64( 0x0010000000000000 ) )<<11;
    bSig = ( bSig | LIT64( 0x0010000000000000 ) )<<11;
    if ( expDiff < 0 ) {
        if ( expDiff < -1 ) return a;
        aSig >>= 1;
    }
    q = ( bSig <= aSig );
    if ( q ) aSig -= bSig;
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        aSig = - ( ( bSig>>2 ) * q );
        expDiff -= 62;
    }
    expDiff += 64;
    if ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig, 0, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        q >>= 64 - expDiff;
        bSig >>= 2;
        aSig = ( ( aSig>>1 )<<( expDiff - 1 ) ) - bSig * q;
    }
    else {
        aSig >>= 2;
        bSig >>= 2;
    }
    do {
        alternateASig = aSig;
        ++q;
        aSig -= bSig;
    } while ( 0 <= (int64_t) aSig );
    sigMean = aSig + alternateASig;
    if ( ( sigMean < 0 ) || ( ( sigMean == 0 ) && ( q & 1 ) ) ) {
        aSig = alternateASig;
    }
    zSign = ( (int64_t) aSig < 0 );
    if ( zSign ) aSig = - aSig;
    return normalizeRoundAndPackFloat64(aSign ^ zSign, bExp, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the double-precision floating-point values
| `a' and `b' then adding 'c', with no intermediate rounding step after the
| multiplication.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic 754-2008.
| The flags argument allows the caller to select negation of the
| addend, the intermediate product, or the final result. (The difference
| between this and having the caller do a separate negation is that negating
| externally will flip the sign bit on NaNs.)
*----------------------------------------------------------------------------*/

float64 float64_muladd(float64 a, float64 b, float64 c, int flags,
                       float_status *status)
{
    flag aSign, bSign, cSign, zSign;
    int aExp, bExp, cExp, pExp, zExp, expDiff;
    uint64_t aSig, bSig, cSig;
    flag pInf, pZero, pSign;
    uint64_t pSig0, pSig1, cSig0, cSig1, zSig0, zSig1;
    int shiftcount;
    flag signflip, infzero;

    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);
    c = float64_squash_input_denormal(c, status);
    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    bSig = extractFloat64Frac(b);
    bExp = extractFloat64Exp(b);
    bSign = extractFloat64Sign(b);
    cSig = extractFloat64Frac(c);
    cExp = extractFloat64Exp(c);
    cSign = extractFloat64Sign(c);

    infzero = ((aExp == 0 && aSig == 0 && bExp == 0x7ff && bSig == 0) ||
               (aExp == 0x7ff && aSig == 0 && bExp == 0 && bSig == 0));

    /* It is implementation-defined whether the cases of (0,inf,qnan)
     * and (inf,0,qnan) raise InvalidOperation or not (and what QNaN
     * they return if they do), so we have to hand this information
     * off to the target-specific pick-a-NaN routine.
     */
    if (((aExp == 0x7ff) && aSig) ||
        ((bExp == 0x7ff) && bSig) ||
        ((cExp == 0x7ff) && cSig)) {
        return propagateFloat64MulAddNaN(a, b, c, infzero, status);
    }

    if (infzero) {
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }

    if (flags & float_muladd_negate_c) {
        cSign ^= 1;
    }

    signflip = (flags & float_muladd_negate_result) ? 1 : 0;

    /* Work out the sign and type of the product */
    pSign = aSign ^ bSign;
    if (flags & float_muladd_negate_product) {
        pSign ^= 1;
    }
    pInf = (aExp == 0x7ff) || (bExp == 0x7ff);
    pZero = ((aExp | aSig) == 0) || ((bExp | bSig) == 0);

    if (cExp == 0x7ff) {
        if (pInf && (pSign ^ cSign)) {
            /* addition of opposite-signed infinities => InvalidOperation */
            float_raise(float_flag_invalid, status);
            return float64_default_nan(status);
        }
        /* Otherwise generate an infinity of the same sign */
        return packFloat64(cSign ^ signflip, 0x7ff, 0);
    }

    if (pInf) {
        return packFloat64(pSign ^ signflip, 0x7ff, 0);
    }

    if (pZero) {
        if (cExp == 0) {
            if (cSig == 0) {
                /* Adding two exact zeroes */
                if (pSign == cSign) {
                    zSign = pSign;
                } else if (status->float_rounding_mode == float_round_down) {
                    zSign = 1;
                } else {
                    zSign = 0;
                }
                return packFloat64(zSign ^ signflip, 0, 0);
            }
            /* Exact zero plus a denorm */
            if (status->flush_to_zero) {
                float_raise(float_flag_output_denormal, status);
                return packFloat64(cSign ^ signflip, 0, 0);
            }
        }
        /* Zero plus something non-zero : just return the something */
        if (flags & float_muladd_halve_result) {
            if (cExp == 0) {
                normalizeFloat64Subnormal(cSig, &cExp, &cSig);
            }
            /* Subtract one to halve, and one again because roundAndPackFloat64
             * wants one less than the true exponent.
             */
            cExp -= 2;
            cSig = (cSig | 0x0010000000000000ULL) << 10;
            return roundAndPackFloat64(cSign ^ signflip, cExp, cSig, status);
        }
        return packFloat64(cSign ^ signflip, cExp, cSig);
    }

    if (aExp == 0) {
        normalizeFloat64Subnormal(aSig, &aExp, &aSig);
    }
    if (bExp == 0) {
        normalizeFloat64Subnormal(bSig, &bExp, &bSig);
    }

    /* Calculate the actual result a * b + c */

    /* Multiply first; this is easy. */
    /* NB: we subtract 0x3fe where float64_mul() subtracts 0x3ff
     * because we want the true exponent, not the "one-less-than"
     * flavour that roundAndPackFloat64() takes.
     */
    pExp = aExp + bExp - 0x3fe;
    aSig = (aSig | LIT64(0x0010000000000000))<<10;
    bSig = (bSig | LIT64(0x0010000000000000))<<11;
    mul64To128(aSig, bSig, &pSig0, &pSig1);
    if ((int64_t)(pSig0 << 1) >= 0) {
        shortShift128Left(pSig0, pSig1, 1, &pSig0, &pSig1);
        pExp--;
    }

    zSign = pSign ^ signflip;

    /* Now [pSig0:pSig1] is the significand of the multiply, with the explicit
     * bit in position 126.
     */
    if (cExp == 0) {
        if (!cSig) {
            /* Throw out the special case of c being an exact zero now */
            shift128RightJamming(pSig0, pSig1, 64, &pSig0, &pSig1);
            if (flags & float_muladd_halve_result) {
                pExp--;
            }
            return roundAndPackFloat64(zSign, pExp - 1,
                                       pSig1, status);
        }
        normalizeFloat64Subnormal(cSig, &cExp, &cSig);
    }

    /* Shift cSig and add the explicit bit so [cSig0:cSig1] is the
     * significand of the addend, with the explicit bit in position 126.
     */
    cSig0 = cSig << (126 - 64 - 52);
    cSig1 = 0;
    cSig0 |= LIT64(0x4000000000000000);
    expDiff = pExp - cExp;

    if (pSign == cSign) {
        /* Addition */
        if (expDiff > 0) {
            /* scale c to match p */
            shift128RightJamming(cSig0, cSig1, expDiff, &cSig0, &cSig1);
            zExp = pExp;
        } else if (expDiff < 0) {
            /* scale p to match c */
            shift128RightJamming(pSig0, pSig1, -expDiff, &pSig0, &pSig1);
            zExp = cExp;
        } else {
            /* no scaling needed */
            zExp = cExp;
        }
        /* Add significands and make sure explicit bit ends up in posn 126 */
        add128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
        if ((int64_t)zSig0 < 0) {
            shift128RightJamming(zSig0, zSig1, 1, &zSig0, &zSig1);
        } else {
            zExp--;
        }
        shift128RightJamming(zSig0, zSig1, 64, &zSig0, &zSig1);
        if (flags & float_muladd_halve_result) {
            zExp--;
        }
        return roundAndPackFloat64(zSign, zExp, zSig1, status);
    } else {
        /* Subtraction */
        if (expDiff > 0) {
            shift128RightJamming(cSig0, cSig1, expDiff, &cSig0, &cSig1);
            sub128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
            zExp = pExp;
        } else if (expDiff < 0) {
            shift128RightJamming(pSig0, pSig1, -expDiff, &pSig0, &pSig1);
            sub128(cSig0, cSig1, pSig0, pSig1, &zSig0, &zSig1);
            zExp = cExp;
            zSign ^= 1;
        } else {
            zExp = pExp;
            if (lt128(cSig0, cSig1, pSig0, pSig1)) {
                sub128(pSig0, pSig1, cSig0, cSig1, &zSig0, &zSig1);
            } else if (lt128(pSig0, pSig1, cSig0, cSig1)) {
                sub128(cSig0, cSig1, pSig0, pSig1, &zSig0, &zSig1);
                zSign ^= 1;
            } else {
                /* Exact zero */
                zSign = signflip;
                if (status->float_rounding_mode == float_round_down) {
                    zSign ^= 1;
                }
                return packFloat64(zSign, 0, 0);
            }
        }
        --zExp;
        /* Do the equivalent of normalizeRoundAndPackFloat64() but
         * starting with the significand in a pair of uint64_t.
         */
        if (zSig0) {
            shiftcount = countLeadingZeros64(zSig0) - 1;
            shortShift128Left(zSig0, zSig1, shiftcount, &zSig0, &zSig1);
            if (zSig1) {
                zSig0 |= 1;
            }
            zExp -= shiftcount;
        } else {
            shiftcount = countLeadingZeros64(zSig1);
            if (shiftcount == 0) {
                zSig0 = (zSig1 >> 1) | (zSig1 & 1);
                zExp -= 63;
            } else {
                shiftcount--;
                zSig0 = zSig1 << shiftcount;
                zExp -= (shiftcount + 64);
            }
        }
        if (flags & float_muladd_halve_result) {
            zExp--;
        }
        return roundAndPackFloat64(zSign, zExp, zSig0, status);
    }
}

/*----------------------------------------------------------------------------
| Returns the square root of the double-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 float64_sqrt(float64 a, float_status *status)
{
    flag aSign;
    int aExp, zExp;
    uint64_t aSig, zSig, doubleZSig;
    uint64_t rem0, rem1, term0, term1;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return propagateFloat64NaN(a, a, status);
        }
        if ( ! aSign ) return a;
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( aSign ) {
        if ( ( aExp | aSig ) == 0 ) return a;
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return float64_zero;
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    zExp = ( ( aExp - 0x3FF )>>1 ) + 0x3FE;
    aSig |= LIT64( 0x0010000000000000 );
    zSig = estimateSqrt32( aExp, aSig>>21 );
    aSig <<= 9 - ( aExp & 1 );
    zSig = estimateDiv128To64( aSig, 0, zSig<<32 ) + ( zSig<<30 );
    if ( ( zSig & 0x1FF ) <= 5 ) {
        doubleZSig = zSig<<1;
        mul64To128( zSig, zSig, &term0, &term1 );
        sub128( aSig, 0, term0, term1, &rem0, &rem1 );
        while ( (int64_t) rem0 < 0 ) {
            --zSig;
            doubleZSig -= 2;
            add128( rem0, rem1, zSig>>63, doubleZSig | 1, &rem0, &rem1 );
        }
        zSig |= ( ( rem0 | rem1 ) != 0 );
    }
    return roundAndPackFloat64(0, zExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the binary log of the double-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
float64 float64_log2(float64 a, float_status *status)
{
    flag aSign, zSign;
    int aExp;
    uint64_t aSig, aSig0, aSig1, zSig, i;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );

    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloat64( 1, 0x7FF, 0 );
        normalizeFloat64Subnormal( aSig, &aExp, &aSig );
    }
    if ( aSign ) {
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    if ( aExp == 0x7FF ) {
        if (aSig) {
            return propagateFloat64NaN(a, float64_zero, status);
        }
        return a;
    }

    aExp -= 0x3FF;
    aSig |= LIT64( 0x0010000000000000 );
    zSign = aExp < 0;
    zSig = (uint64_t)aExp << 52;
    for (i = 1LL << 51; i > 0; i >>= 1) {
        mul64To128( aSig, aSig, &aSig0, &aSig1 );
        aSig = ( aSig0 << 12 ) | ( aSig1 >> 52 );
        if ( aSig & LIT64( 0x0020000000000000 ) ) {
            aSig >>= 1;
            zSig |= i;
        }
    }

    if ( zSign )
        zSig = -zSig;
    return normalizeRoundAndPackFloat64(zSign, 0x408, zSig, status);
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is equal to the
| corresponding value `b', and 0 otherwise.  The invalid exception is raised
| if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_eq(float64 a, float64 b, float_status *status)
{
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    av = float64_val(a);
    bv = float64_val(b);
    return ( av == bv ) || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_le(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  The comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_lt(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;

    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);
    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign && ( (uint64_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  The invalid exception is raised if either
| operand is a NaN.  The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_unordered(float64 a, float64 b, float_status *status)
{
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is equal to the
| corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.The comparison is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_eq_quiet(float64 a, float64 b, float_status *status)
{
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    av = float64_val(a);
    bv = float64_val(b);
    return ( av == bv ) || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than or
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_le_quiet(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign || ( (uint64_t) ( ( av | bv )<<1 ) == 0 );
    return ( av == bv ) || ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_lt_quiet(float64 a, float64 b, float_status *status)
{
    flag aSign, bSign;
    uint64_t av, bv;
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat64Sign( a );
    bSign = extractFloat64Sign( b );
    av = float64_val(a);
    bv = float64_val(b);
    if ( aSign != bSign ) return aSign && ( (uint64_t) ( ( av | bv )<<1 ) != 0 );
    return ( av != bv ) && ( aSign ^ ( av < bv ) );

}

/*----------------------------------------------------------------------------
| Returns 1 if the double-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.  The
| comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float64_unordered_quiet(float64 a, float64 b, float_status *status)
{
    a = float64_squash_input_denormal(a, status);
    b = float64_squash_input_denormal(b, status);

    if (    ( ( extractFloat64Exp( a ) == 0x7FF ) && extractFloat64Frac( a ) )
         || ( ( extractFloat64Exp( b ) == 0x7FF ) && extractFloat64Frac( b ) )
       ) {
        if (float64_is_signaling_nan(a, status)
         || float64_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 32-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic---which means in particular that the conversion
| is rounded according to the current rounding mode.  If `a' is a NaN, the
| largest positive integer is returned.  Otherwise, if the conversion
| overflows, the largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t floatx80_to_int32(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1 << 31;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( ( aExp == 0x7FFF ) && (uint64_t) ( aSig<<1 ) ) aSign = 0;
    shiftCount = 0x4037 - aExp;
    if ( shiftCount <= 0 ) shiftCount = 1;
    shift64RightJamming( aSig, shiftCount, &aSig );
    return roundAndPackInt32(aSign, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 32-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN, the largest positive integer is returned.
| Otherwise, if the conversion overflows, the largest integer with the same
| sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t floatx80_to_int32_round_to_zero(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig, savedASig;
    int32_t z;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1 << 31;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( 0x401E < aExp ) {
        if ( ( aExp == 0x7FFF ) && (uint64_t) ( aSig<<1 ) ) aSign = 0;
        goto invalid;
    }
    else if ( aExp < 0x3FFF ) {
        if (aExp || aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    shiftCount = 0x403E - aExp;
    savedASig = aSig;
    aSig >>= shiftCount;
    z = aSig;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise(float_flag_invalid, status);
        return aSign ? (int32_t) 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig<<shiftCount ) != savedASig ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 64-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic---which means in particular that the conversion
| is rounded according to the current rounding mode.  If `a' is a NaN,
| the largest positive integer is returned.  Otherwise, if the conversion
| overflows, the largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t floatx80_to_int64(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig, aSigExtra;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1ULL << 63;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    shiftCount = 0x403E - aExp;
    if ( shiftCount <= 0 ) {
        if ( shiftCount ) {
            float_raise(float_flag_invalid, status);
            if (    ! aSign
                 || ((aExp == floatx80_infinity_high)
                     && (aSig != floatx80_infinity_low))
               ) {
                return LIT64( 0x7FFFFFFFFFFFFFFF );
            }
            return (int64_t) LIT64( 0x8000000000000000 );
        }
        aSigExtra = 0;
    }
    else {
        shift64ExtraRightJamming( aSig, 0, shiftCount, &aSig, &aSigExtra );
    }
    return roundAndPackInt64(aSign, aSig, aSigExtra, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the 64-bit two's complement integer format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic, except that the conversion is always rounded
| toward zero.  If `a' is a NaN, the largest positive integer is returned.
| Otherwise, if the conversion overflows, the largest integer with the same
| sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t floatx80_to_int64_round_to_zero(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig;
    int64_t z;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return 1ULL << 63;
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    shiftCount = aExp - 0x403E;
    if ( 0 <= shiftCount ) {
        aSig &= LIT64( 0x7FFFFFFFFFFFFFFF );
        if ( ( a.high != 0xC03E ) || aSig ) {
            float_raise(float_flag_invalid, status);
            if ( ! aSign || ( ( aExp == 0x7FFF ) && aSig ) ) {
                return LIT64( 0x7FFFFFFFFFFFFFFF );
            }
        }
        return (int64_t) LIT64( 0x8000000000000000 );
    }
    else if ( aExp < 0x3FFF ) {
        if (aExp | aSig) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    z = aSig>>( - shiftCount );
    if ( (uint64_t) ( aSig<<( shiftCount & 63 ) ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the single-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float32 floatx80_to_float32(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return float32_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( (uint64_t) ( aSig<<1 ) ) {
            return commonNaNToFloat32(floatx80ToCommonNaN(a, status), status);
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    shift64RightJamming( aSig, 33, &aSig );
    if ( aExp || aSig ) aExp -= 0x3F81;
    return roundAndPackFloat32(aSign, aExp, aSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the double-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float64 floatx80_to_float64(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig, zSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return float64_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( (uint64_t) ( aSig<<1 ) ) {
            return commonNaNToFloat64(floatx80ToCommonNaN(a, status), status);
        }
        return packFloat64( aSign, 0x7FF, 0 );
    }
    shift64RightJamming( aSig, 1, &zSig );
    if ( aExp || aSig ) aExp -= 0x3C01;
    return roundAndPackFloat64(aSign, aExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the extended double-precision floating-
| point value `a' to the quadruple-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 floatx80_to_float128(floatx80 a, float_status *status)
{
    flag aSign;
    int aExp;
    uint64_t aSig, zSig0, zSig1;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return float128_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( ( aExp == 0x7FFF ) && (uint64_t) ( aSig<<1 ) ) {
        return commonNaNToFloat128(floatx80ToCommonNaN(a, status), status);
    }
    shift128Right( aSig<<1, 0, 16, &zSig0, &zSig1 );
    return packFloat128( aSign, aExp, zSig0, zSig1 );

}

/*----------------------------------------------------------------------------
| Rounds the extended double-precision floating-point value `a'
| to the precision provided by floatx80_rounding_precision and returns the
| result as an extended double-precision floating-point value.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_round(floatx80 a, float_status *status)
{
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                extractFloatx80Sign(a),
                                extractFloatx80Exp(a),
                                extractFloatx80Frac(a), 0, status);
}

/*----------------------------------------------------------------------------
| Rounds the extended double-precision floating-point value `a' to an integer,
| and returns the result as an extended quadruple-precision floating-point
| value.  The operation is performed according to the IEC/IEEE Standard for
| Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_round_to_int(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t lastBitMask, roundBitsMask;
    floatx80 z;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aExp = extractFloatx80Exp( a );
    if ( 0x403E <= aExp ) {
        if ( ( aExp == 0x7FFF ) && (uint64_t) ( extractFloatx80Frac( a )<<1 ) ) {
            return propagateFloatx80NaN(a, a, status);
        }
        return a;
    }
    if ( aExp < 0x3FFF ) {
        if (    ( aExp == 0 )
             && ( (uint64_t) ( extractFloatx80Frac( a )<<1 ) == 0 ) ) {
            return a;
        }
        status->float_exception_flags |= float_flag_inexact;
        aSign = extractFloatx80Sign( a );
        switch (status->float_rounding_mode) {
         case float_round_nearest_even:
            if ( ( aExp == 0x3FFE ) && (uint64_t) ( extractFloatx80Frac( a )<<1 )
               ) {
                return
                    packFloatx80( aSign, 0x3FFF, LIT64( 0x8000000000000000 ) );
            }
            break;
        case float_round_ties_away:
            if (aExp == 0x3FFE) {
                return packFloatx80(aSign, 0x3FFF, LIT64(0x8000000000000000));
            }
            break;
         case float_round_down:
            return
                  aSign ?
                      packFloatx80( 1, 0x3FFF, LIT64( 0x8000000000000000 ) )
                : packFloatx80( 0, 0, 0 );
         case float_round_up:
            return
                  aSign ? packFloatx80( 1, 0, 0 )
                : packFloatx80( 0, 0x3FFF, LIT64( 0x8000000000000000 ) );
        }
        return packFloatx80( aSign, 0, 0 );
    }
    lastBitMask = 1;
    lastBitMask <<= 0x403E - aExp;
    roundBitsMask = lastBitMask - 1;
    z = a;
    switch (status->float_rounding_mode) {
    case float_round_nearest_even:
        z.low += lastBitMask>>1;
        if ((z.low & roundBitsMask) == 0) {
            z.low &= ~lastBitMask;
        }
        break;
    case float_round_ties_away:
        z.low += lastBitMask >> 1;
        break;
    case float_round_to_zero:
        break;
    case float_round_up:
        if (!extractFloatx80Sign(z)) {
            z.low += roundBitsMask;
        }
        break;
    case float_round_down:
        if (extractFloatx80Sign(z)) {
            z.low += roundBitsMask;
        }
        break;
    default:
        abort();
    }
    z.low &= ~ roundBitsMask;
    if ( z.low == 0 ) {
        ++z.high;
        z.low = LIT64( 0x8000000000000000 );
    }
    if (z.low != a.low) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the extended double-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the sum is
| negated before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 addFloatx80Sigs(floatx80 a, floatx80 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    int32_t expDiff;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FFF ) {
            if ((uint64_t)(aSig << 1)) {
                return propagateFloatx80NaN(a, b, status);
            }
            return a;
        }
        if ( bExp == 0 ) --expDiff;
        shift64ExtraRightJamming( bSig, 0, expDiff, &bSig, &zSig1 );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FFF ) {
            if ((uint64_t)(bSig << 1)) {
                return propagateFloatx80NaN(a, b, status);
            }
            return packFloatx80(zSign,
                                floatx80_infinity_high,
                                floatx80_infinity_low);
        }
        if ( aExp == 0 ) ++expDiff;
        shift64ExtraRightJamming( aSig, 0, - expDiff, &aSig, &zSig1 );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FFF ) {
            if ( (uint64_t) ( ( aSig | bSig )<<1 ) ) {
                return propagateFloatx80NaN(a, b, status);
            }
            return a;
        }
        zSig1 = 0;
        zSig0 = aSig + bSig;
        if ( aExp == 0 ) {
            normalizeFloatx80Subnormal( zSig0, &zExp, &zSig0 );
            goto roundAndPack;
        }
        zExp = aExp;
        goto shiftRight1;
    }
    zSig0 = aSig + bSig;
    if ( (int64_t) zSig0 < 0 ) goto roundAndPack;
 shiftRight1:
    shift64ExtraRightJamming( zSig0, zSig1, 1, &zSig0, &zSig1 );
    zSig0 |= LIT64( 0x8000000000000000 );
    ++zExp;
 roundAndPack:
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the extended
| double-precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static floatx80 subFloatx80Sigs(floatx80 a, floatx80 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    int32_t expDiff;

    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FFF ) {
        if ( (uint64_t) ( ( aSig | bSig )<<1 ) ) {
            return propagateFloatx80NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    zSig1 = 0;
    if ( bSig < aSig ) goto aBigger;
    if ( aSig < bSig ) goto bBigger;
    return packFloatx80(status->float_rounding_mode == float_round_down, 0, 0);
 bExpBigger:
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return packFloatx80(zSign ^ 1, floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if ( aExp == 0 ) ++expDiff;
    shift128RightJamming( aSig, 0, - expDiff, &aSig, &zSig1 );
 bBigger:
    sub128( bSig, 0, aSig, zSig1, &zSig0, &zSig1 );
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FFF ) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) --expDiff;
    shift128RightJamming( bSig, 0, expDiff, &bSig, &zSig1 );
 aBigger:
    sub128( aSig, 0, bSig, zSig1, &zSig0, &zSig1 );
    zExp = aExp;
 normalizeRoundAndPack:
    return normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                         zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of adding the extended double-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_add(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign == bSign ) {
        return addFloatx80Sigs(a, b, aSign, status);
    }
    else {
        return subFloatx80Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the extended double-precision floating-
| point values `a' and `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_sub(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign == bSign ) {
        return subFloatx80Sigs(a, b, aSign, status);
    }
    else {
        return addFloatx80Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the extended double-precision floating-
| point values `a' and `b'.  The operation is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_mul(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    bSign = extractFloatx80Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (    (uint64_t) ( aSig<<1 )
             || ( ( bExp == 0x7FFF ) && (uint64_t) ( bSig<<1 ) ) ) {
            return propagateFloatx80NaN(a, b, status);
        }
        if ( ( bExp | bSig ) == 0 ) goto invalid;
        return packFloatx80(zSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        if ( ( aExp | aSig ) == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        return packFloatx80(zSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( zSign, 0, 0 );
        normalizeFloatx80Subnormal( aSig, &aExp, &aSig );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) return packFloatx80( zSign, 0, 0 );
        normalizeFloatx80Subnormal( bSig, &bExp, &bSig );
    }
    zExp = aExp + bExp - 0x3FFE;
    mul64To128( aSig, bSig, &zSig0, &zSig1 );
    if ( 0 < (int64_t) zSig0 ) {
        shortShift128Left( zSig0, zSig1, 1, &zSig0, &zSig1 );
        --zExp;
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the result of dividing the extended double-precision floating-point
| value `a' by the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_div(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig, bSig, zSig0, zSig1;
    uint64_t rem0, rem1, rem2, term0, term1, term2;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    bSign = extractFloatx80Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        if ( bExp == 0x7FFF ) {
            if ((uint64_t)(bSig << 1)) {
                return propagateFloatx80NaN(a, b, status);
            }
            goto invalid;
        }
        return packFloatx80(zSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return packFloatx80( zSign, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
            if ( ( aExp | aSig ) == 0 ) {
 invalid:
                float_raise(float_flag_invalid, status);
                return floatx80_default_nan(status);
            }
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(zSign, floatx80_infinity_high,
                                       floatx80_infinity_low);
        }
        normalizeFloatx80Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( aSig == 0 ) return packFloatx80( zSign, 0, 0 );
        normalizeFloatx80Subnormal( aSig, &aExp, &aSig );
    }
    zExp = aExp - bExp + 0x3FFE;
    rem1 = 0;
    if ( bSig <= aSig ) {
        shift128Right( aSig, 0, 1, &aSig, &rem1 );
        ++zExp;
    }
    zSig0 = estimateDiv128To64( aSig, rem1, bSig );
    mul64To128( bSig, zSig0, &term0, &term1 );
    sub128( aSig, rem1, term0, term1, &rem0, &rem1 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        add128( rem0, rem1, 0, bSig, &rem0, &rem1 );
    }
    zSig1 = estimateDiv128To64( rem1, 0, bSig );
    if ( (uint64_t) ( zSig1<<1 ) <= 8 ) {
        mul64To128( bSig, zSig1, &term1, &term2 );
        sub128( rem1, 0, term1, term2, &rem1, &rem2 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            add128( rem1, rem2, 0, bSig, &rem1, &rem2 );
        }
        zSig1 |= ( ( rem1 | rem2 ) != 0 );
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                zSign, zExp, zSig0, zSig1, status);
}

/*----------------------------------------------------------------------------
| Returns the remainder of the extended double-precision floating-point value
| `a' with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_rem(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, zSign;
    int32_t aExp, bExp, expDiff;
    uint64_t aSig0, aSig1, bSig;
    uint64_t q, term0, term1, alternateASig0, alternateASig1;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig0 = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    bSig = extractFloatx80Frac( b );
    bExp = extractFloatx80Exp( b );
    if ( aExp == 0x7FFF ) {
        if (    (uint64_t) ( aSig0<<1 )
             || ( ( bExp == 0x7FFF ) && (uint64_t) ( bSig<<1 ) ) ) {
            return propagateFloatx80NaN(a, b, status);
        }
        goto invalid;
    }
    if ( bExp == 0x7FFF ) {
        if ((uint64_t)(bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( bSig == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        normalizeFloatx80Subnormal( bSig, &bExp, &bSig );
    }
    if ( aExp == 0 ) {
        if ( (uint64_t) ( aSig0<<1 ) == 0 ) return a;
        normalizeFloatx80Subnormal( aSig0, &aExp, &aSig0 );
    }
    bSig |= LIT64( 0x8000000000000000 );
    zSign = aSign;
    expDiff = aExp - bExp;
    aSig1 = 0;
    if ( expDiff < 0 ) {
        if ( expDiff < -1 ) return a;
        shift128Right( aSig0, 0, 1, &aSig0, &aSig1 );
        expDiff = 0;
    }
    q = ( bSig <= aSig0 );
    if ( q ) aSig0 -= bSig;
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        mul64To128( bSig, q, &term0, &term1 );
        sub128( aSig0, aSig1, term0, term1, &aSig0, &aSig1 );
        shortShift128Left( aSig0, aSig1, 62, &aSig0, &aSig1 );
        expDiff -= 62;
    }
    expDiff += 64;
    if ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig );
        q = ( 2 < q ) ? q - 2 : 0;
        q >>= 64 - expDiff;
        mul64To128( bSig, q<<( 64 - expDiff ), &term0, &term1 );
        sub128( aSig0, aSig1, term0, term1, &aSig0, &aSig1 );
        shortShift128Left( 0, bSig, 64 - expDiff, &term0, &term1 );
        while ( le128( term0, term1, aSig0, aSig1 ) ) {
            ++q;
            sub128( aSig0, aSig1, term0, term1, &aSig0, &aSig1 );
        }
    }
    else {
        term1 = 0;
        term0 = bSig;
    }
    sub128( term0, term1, aSig0, aSig1, &alternateASig0, &alternateASig1 );
    if (    lt128( alternateASig0, alternateASig1, aSig0, aSig1 )
         || (    eq128( alternateASig0, alternateASig1, aSig0, aSig1 )
              && ( q & 1 ) )
       ) {
        aSig0 = alternateASig0;
        aSig1 = alternateASig1;
        zSign = ! zSign;
    }
    return
        normalizeRoundAndPackFloatx80(
            80, zSign, bExp + expDiff, aSig0, aSig1, status);

}

/*----------------------------------------------------------------------------
| Returns the square root of the extended double-precision floating-point
| value `a'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 floatx80_sqrt(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp, zExp;
    uint64_t aSig0, aSig1, zSig0, zSig1, doubleZSig0;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig0 = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );
    if ( aExp == 0x7FFF ) {
        if ((uint64_t)(aSig0 << 1)) {
            return propagateFloatx80NaN(a, a, status);
        }
        if ( ! aSign ) return a;
        goto invalid;
    }
    if ( aSign ) {
        if ( ( aExp | aSig0 ) == 0 ) return a;
 invalid:
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    if ( aExp == 0 ) {
        if ( aSig0 == 0 ) return packFloatx80( 0, 0, 0 );
        normalizeFloatx80Subnormal( aSig0, &aExp, &aSig0 );
    }
    zExp = ( ( aExp - 0x3FFF )>>1 ) + 0x3FFF;
    zSig0 = estimateSqrt32( aExp, aSig0>>32 );
    shift128Right( aSig0, 0, 2 + ( aExp & 1 ), &aSig0, &aSig1 );
    zSig0 = estimateDiv128To64( aSig0, aSig1, zSig0<<32 ) + ( zSig0<<30 );
    doubleZSig0 = zSig0<<1;
    mul64To128( zSig0, zSig0, &term0, &term1 );
    sub128( aSig0, aSig1, term0, term1, &rem0, &rem1 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        doubleZSig0 -= 2;
        add128( rem0, rem1, zSig0>>63, doubleZSig0 | 1, &rem0, &rem1 );
    }
    zSig1 = estimateDiv128To64( rem1, 0, doubleZSig0 );
    if ( ( zSig1 & LIT64( 0x3FFFFFFFFFFFFFFF ) ) <= 5 ) {
        if ( zSig1 == 0 ) zSig1 = 1;
        mul64To128( doubleZSig0, zSig1, &term1, &term2 );
        sub128( rem1, 0, term1, term2, &rem1, &rem2 );
        mul64To128( zSig1, zSig1, &term2, &term3 );
        sub192( rem1, rem2, 0, 0, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            shortShift128Left( 0, zSig1, 1, &term2, &term3 );
            term3 |= 1;
            term2 |= doubleZSig0;
            add192( rem1, rem2, rem3, 0, term2, term3, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shortShift128Left( 0, zSig1, 1, &zSig0, &zSig1 );
    zSig0 |= doubleZSig0;
    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                0, zExp, zSig0, zSig1, status);
}

#if defined(TARGET_M68K)
/* This part is copied from previous:
 *  Written by Andreas Grabher for Previous, NeXT Computer Emulator.
 */

/*----------------------------------------------------------------------------
 | Returns the modulo remainder of the extended double-precision floating-point
 | value `a' with respect to the corresponding value `b'.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_mod(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, zSign;
    int32_t aExp, bExp, expDiff;
    uint64_t aSig0, aSig1, bSig;
    uint64_t qTemp, term0, term1;

    aSig0 = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig0 << 1)
            || ((bExp == 0x7FFF) && (uint64_t) (bSig << 1))) {
            return propagateFloatx80NaN(a, b, status);
        }
        goto invalid;
    }
    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return a;
    }
    if (bExp == 0) {
        if (bSig == 0) {
        invalid:
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        normalizeFloatx80Subnormal(bSig, &bExp, &bSig);
    }
    if (aExp == 0) {
        if ((uint64_t) (aSig0 << 1) == 0) {
            return a;
        }
        normalizeFloatx80Subnormal(aSig0, &aExp, &aSig0);
    }
    bSig |= LIT64(0x8000000000000000);
    zSign = aSign;
    expDiff = aExp - bExp;
    aSig1 = 0;
    if (expDiff < 0) {
        return a;
    }
    qTemp = (bSig <= aSig0);
    if (qTemp) {
        aSig0 -= bSig;
    }
    expDiff -= 64;
    while (0 < expDiff) {
        qTemp = estimateDiv128To64(aSig0, aSig1, bSig);
        qTemp = (2 < qTemp) ? qTemp - 2 : 0;
        mul64To128(bSig, qTemp, &term0, &term1);
        sub128(aSig0, aSig1, term0, term1, &aSig0, &aSig1);
        shortShift128Left(aSig0, aSig1, 62, &aSig0, &aSig1);
    }
    expDiff += 64;
    if (0 < expDiff) {
        qTemp = estimateDiv128To64(aSig0, aSig1, bSig);
        qTemp = (2 < qTemp) ? qTemp - 2 : 0;
        qTemp >>= 64 - expDiff;
        mul64To128(bSig, qTemp << (64 - expDiff), &term0, &term1);
        sub128(aSig0, aSig1, term0, term1, &aSig0, &aSig1);
        shortShift128Left(0, bSig, 64 - expDiff, &term0, &term1);
        while (le128(term0, term1, aSig0, aSig1)) {
            ++qTemp;
            sub128(aSig0, aSig1, term0, term1, &aSig0, &aSig1);
        }
    }
    return
        normalizeRoundAndPackFloatx80(
            80, zSign, bExp + expDiff, aSig0, aSig1, status);
}

/*----------------------------------------------------------------------------
 | Returns the mantissa of the extended double-precision floating-point
 | value `a'.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_getman(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a , status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    return roundAndPackFloatx80(status->floatx80_rounding_precision, aSign,
                                0x3FFF, aSig, 0, status);
}

/*----------------------------------------------------------------------------
 | Returns the exponent of the extended double-precision floating-point
 | value `a' as an extended double-precision value.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_getexp(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a , status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    return int32_to_floatx80(aExp - 0x3FFF, status);
}

/*----------------------------------------------------------------------------
 | Scales extended double-precision floating-point value in operand `a' by
 | value `b'. The function truncates the value in the second operand 'b' to
 | an integral value and adds that value to the exponent of the operand 'a'.
 | The operation performed according to the IEC/IEEE Standard for Binary
 | Floating-Point Arithmetic.
 *----------------------------------------------------------------------------*/

floatx80 floatx80_scale(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;
    int32_t aExp, bExp, shiftCount;
    uint64_t aSig, bSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);
    bSig = extractFloatx80Frac(b);
    bExp = extractFloatx80Exp(b);
    bSign = extractFloatx80Sign(b);

    if (bExp == 0x7FFF) {
        if ((uint64_t) (bSig << 1) ||
            ((aExp == 0x7FFF) && (uint64_t) (aSig << 1))) {
            return propagateFloatx80NaN(a, b, status);
        }
        float_raise(float_flag_invalid , status);
        return floatx80_default_nan(status);
    }
    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaN(a, b, status);
        }
        return packFloatx80(aSign, floatx80_infinity_high,
                            floatx80_infinity_low);
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return packFloatx80(aSign, 0, 0);
        }
        if (bExp < 0x3FFF) {
            return a;
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    if (bExp < 0x3FFF) {
        return a;
    }

    if (0x400F < bExp) {
        aExp = bSign ? -0x6001 : 0xE000;
        return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                    aSign, aExp, aSig, 0, status);
    }

    shiftCount = 0x403E - bExp;
    bSig >>= shiftCount;
    aExp = bSign ? (aExp - bSig) : (aExp + bSig);

    return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                aSign, aExp, aSig, 0, status);

}

floatx80 floatx80_move(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t)(aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return a;
    }
    if (aExp == 0) {
        if (aSig == 0) {
            return a;
        }
        normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                      aSign, aExp, aSig, 0, status);
    }
    return roundAndPackFloatx80(status->floatx80_rounding_precision, aSign,
                                aExp, aSig, 0, status);
}

/*----------------------------------------------------------------------------
| Algorithms for transcendental functions supported by MC68881 and MC68882
| mathematical coprocessors. The functions are derived from FPSP library.
*----------------------------------------------------------------------------*/

#define one_exp     0x3FFF
#define one_sig     LIT64(0x8000000000000000)

#define pi_exp      0x4000
#define piby2_exp   0x3FFF
#define pi_sig      LIT64(0xc90fdaa22168c235)

/*----------------------------------------------------------------------------
 | Function for compactifying extended double-precision floating point values.
 *----------------------------------------------------------------------------*/

static int32_t floatx80_make_compact(int32_t aExp, uint64_t aSig)
{
    return (aExp<<16)|(aSig>>48);
}


/*----------------------------------------------------------------------------
 | Log base e of x plus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_lognp1(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig, fSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, j, k;
    floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
        return packFloatx80(0, floatx80_infinity_high, floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    if (aSign && aExp >= one_exp) {
        if (aExp == one_exp && aSig == one_sig) {
            float_raise(float_flag_divbyzero, status);
            packFloatx80(aSign, floatx80_infinity_high, floatx80_infinity_low);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp < 0x3f99 || (aExp == 0x3f99 && aSig == one_sig)) {
        /* <= min threshold */
        float_raise(float_flag_inexact, status);
        return floatx80_move(a, status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a; /* Z */
    fp1 = a;

    fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                       status), status); /* X = (1+Z) */

    aExp = extractFloatx80Exp(fp0);
    aSig = extractFloatx80Frac(fp0);

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FFE8000 || compact > 0x3FFFC000) {
        /* |X| < 1/2 or |X| > 3/2 */
        k = aExp - 0x3FFF;
        fp1 = int32_to_floatx80(k, status);

        fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
        j = (fSig >> 56) & 0x7E; /* DISPLACEMENT FOR 1/F */

        f = packFloatx80(0, 0x3FFF, fSig); /* F */
        fp0 = packFloatx80(0, 0x3FFF, aSig); /* Y */

        fp0 = floatx80_sub(fp0, f, status); /* Y-F */

    lp1cont1:
        /* LP1CONT1 */
        fp0 = floatx80_mul(fp0, log_tbl[j], status); /* FP0 IS U = (Y-F)/F */
        logof2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC));
        klog2 = floatx80_mul(fp1, logof2, status); /* FP1 IS K*LOG2 */
        fp2 = floatx80_mul(fp0, fp0, status); /* FP2 IS V=U*U */

        fp3 = fp2;
        fp1 = fp2;

        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3FC2499AB5E4040B), status),
                           status); /* V*A6 */
        fp2 = floatx80_mul(fp2, float64_to_floatx80(
                           make_float64(0xBFC555B5848CB7DB), status),
                           status); /* V*A5 */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FC99999987D8730), status),
                           status); /* A4+V*A6 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFCFFFFFFF6F7E97), status),
                           status); /* A3+V*A5 */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A4+V*A6) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A3+V*A5) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FD55555555555A4), status),
                           status); /* A2+V*(A4+V*A6) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFE0000000000008), status),
                           status); /* A1+V*(A3+V*A5) */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A2+V*(A4+V*A6)) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A1+V*(A3+V*A5)) */
        fp1 = floatx80_mul(fp1, fp0, status); /* U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp2, status); /* U+V*(A1+V*(A3+V*A5)) */

        fp1 = floatx80_add(fp1, log_tbl[j + 1],
                           status); /* LOG(F)+U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS LOG(F) + LOG(1+U) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, klog2, status);

        float_raise(float_flag_inexact, status);

        return a;
    } else if (compact < 0x3FFEF07D || compact > 0x3FFF8841) {
        /* |X| < 1/16 or |X| > -1/16 */
        /* LP1CARE */
        fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
        f = packFloatx80(0, 0x3FFF, fSig); /* F */
        j = (fSig >> 56) & 0x7E; /* DISPLACEMENT FOR 1/F */

        if (compact >= 0x3FFF8000) { /* 1+Z >= 1 */
            /* KISZERO */
            fp0 = floatx80_sub(float32_to_floatx80(make_float32(0x3F800000),
                               status), f, status); /* 1-F */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS Y-F = (1-F)+Z */
            fp1 = packFloatx80(0, 0, 0); /* K = 0 */
        } else {
            /* KISNEG */
            fp0 = floatx80_sub(float32_to_floatx80(make_float32(0x40000000),
                               status), f, status); /* 2-F */
            fp1 = floatx80_add(fp1, fp1, status); /* 2Z */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS Y-F = (2-F)+2Z */
            fp1 = packFloatx80(1, one_exp, one_sig); /* K = -1 */
        }
        goto lp1cont1;
    } else {
        /* LP1ONE16 */
        fp1 = floatx80_add(fp1, fp1, status); /* FP1 IS 2Z */
        fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* FP0 IS 1+X */

        /* LP1CONT2 */
        fp1 = floatx80_div(fp1, fp0, status); /* U */
        saveu = fp1;
        fp0 = floatx80_mul(fp1, fp1, status); /* FP0 IS V = U*U */
        fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS W = V*V */

        fp3 = float64_to_floatx80(make_float64(0x3F175496ADD7DAD6),
                                  status); /* B5 */
        fp2 = float64_to_floatx80(make_float64(0x3F3C71C2FE80C7E0),
                                  status); /* B4 */
        fp3 = floatx80_mul(fp3, fp1, status); /* W*B5 */
        fp2 = floatx80_mul(fp2, fp1, status); /* W*B4 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3F624924928BCCFF), status),
                           status); /* B3+W*B5 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3F899999999995EC), status),
                           status); /* B2+W*B4 */
        fp1 = floatx80_mul(fp1, fp3, status); /* W*(B3+W*B5) */
        fp2 = floatx80_mul(fp2, fp0, status); /* V*(B2+W*B4) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FB5555555555555), status),
                           status); /* B1+W*(B3+W*B5) */

        fp0 = floatx80_mul(fp0, saveu, status); /* FP0 IS U*V */
        fp1 = floatx80_add(fp1, fp2,
                           status); /* B1+W*(B3+W*B5) + V*(B2+W*B4) */
        fp0 = floatx80_mul(fp0, fp1,
                           status); /* U*V*([B1+W*(B3+W*B5)] + [V*(B2+W*B4)]) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, saveu, status);

        /*if (!floatx80_is_zero(a)) { */
            float_raise(float_flag_inexact, status);
        /*} */

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Log base e
 *----------------------------------------------------------------------------*/

floatx80 floatx80_logn(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig, fSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, j, k, adjk;
    floatx80 fp0, fp1, fp2, fp3, f, logof2, klog2, saveu;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return packFloatx80(0, floatx80_infinity_high,
                                floatx80_infinity_low);
        }
    }

    adjk = 0;

    if (aExp == 0) {
        if (aSig == 0) { /* zero */
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, floatx80_infinity_high,
                                floatx80_infinity_low);
        }
        if ((aSig & one_sig) == 0) { /* denormal */
            normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
            adjk = -100;
            aExp += 100;
            a = packFloatx80(aSign, aExp, aSig);
        }
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FFEF07D || compact > 0x3FFF8841) {
        /* |X| < 15/16 or |X| > 17/16 */
        k = aExp - 0x3FFF;
        k += adjk;
        fp1 = int32_to_floatx80(k, status);

        fSig = (aSig & LIT64(0xFE00000000000000)) | LIT64(0x0100000000000000);
        j = (fSig >> 56) & 0x7E; /* DISPLACEMENT FOR 1/F */

        f = packFloatx80(0, 0x3FFF, fSig); /* F */
        fp0 = packFloatx80(0, 0x3FFF, aSig); /* Y */

        fp0 = floatx80_sub(fp0, f, status); /* Y-F */

        /* LP1CONT1 */
        fp0 = floatx80_mul(fp0, log_tbl[j], status); /* FP0 IS U = (Y-F)/F */
        logof2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC));
        klog2 = floatx80_mul(fp1, logof2, status); /* FP1 IS K*LOG2 */
        fp2 = floatx80_mul(fp0, fp0, status); /* FP2 IS V=U*U */

        fp3 = fp2;
        fp1 = fp2;

        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3FC2499AB5E4040B), status),
                           status); /* V*A6 */
        fp2 = floatx80_mul(fp2, float64_to_floatx80(
                           make_float64(0xBFC555B5848CB7DB), status),
                           status); /* V*A5 */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FC99999987D8730), status),
                           status); /* A4+V*A6 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFCFFFFFFF6F7E97), status),
                           status); /* A3+V*A5 */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A4+V*A6) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A3+V*A5) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FD55555555555A4), status),
                           status); /* A2+V*(A4+V*A6) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0xBFE0000000000008), status),
                           status); /* A1+V*(A3+V*A5) */
        fp1 = floatx80_mul(fp1, fp3, status); /* V*(A2+V*(A4+V*A6)) */
        fp2 = floatx80_mul(fp2, fp3, status); /* V*(A1+V*(A3+V*A5)) */
        fp1 = floatx80_mul(fp1, fp0, status); /* U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp2, status); /* U+V*(A1+V*(A3+V*A5)) */

        fp1 = floatx80_add(fp1, log_tbl[j + 1],
                           status); /* LOG(F)+U*V*(A2+V*(A4+V*A6)) */
        fp0 = floatx80_add(fp0, fp1, status); /* FP0 IS LOG(F) + LOG(1+U) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, klog2, status);

        float_raise(float_flag_inexact, status);

        return a;
    } else { /* |X-1| >= 1/16 */
        fp0 = a;
        fp1 = a;
        fp1 = floatx80_sub(fp1, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* FP1 IS X-1 */
        fp0 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* FP0 IS X+1 */
        fp1 = floatx80_add(fp1, fp1, status); /* FP1 IS 2(X-1) */

        /* LP1CONT2 */
        fp1 = floatx80_div(fp1, fp0, status); /* U */
        saveu = fp1;
        fp0 = floatx80_mul(fp1, fp1, status); /* FP0 IS V = U*U */
        fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS W = V*V */

        fp3 = float64_to_floatx80(make_float64(0x3F175496ADD7DAD6),
                                  status); /* B5 */
        fp2 = float64_to_floatx80(make_float64(0x3F3C71C2FE80C7E0),
                                  status); /* B4 */
        fp3 = floatx80_mul(fp3, fp1, status); /* W*B5 */
        fp2 = floatx80_mul(fp2, fp1, status); /* W*B4 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3F624924928BCCFF), status),
                           status); /* B3+W*B5 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3F899999999995EC), status),
                           status); /* B2+W*B4 */
        fp1 = floatx80_mul(fp1, fp3, status); /* W*(B3+W*B5) */
        fp2 = floatx80_mul(fp2, fp0, status); /* V*(B2+W*B4) */
        fp1 = floatx80_add(fp1, float64_to_floatx80(
                           make_float64(0x3FB5555555555555), status),
                           status); /* B1+W*(B3+W*B5) */

        fp0 = floatx80_mul(fp0, saveu, status); /* FP0 IS U*V */
        fp1 = floatx80_add(fp1, fp2, status); /* B1+W*(B3+W*B5) + V*(B2+W*B4) */
        fp0 = floatx80_mul(fp0, fp1,
                           status); /* U*V*([B1+W*(B3+W*B5)] + [V*(B2+W*B4)]) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, saveu, status);

        /*if (!floatx80_is_zero(a)) { */
            float_raise(float_flag_inexact, status);
        /*} */

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Log base 10
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log10(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return packFloatx80(0, floatx80_infinity_high,
                                floatx80_infinity_low);
        }
    }

    if (aExp == 0 && aSig == 0) {
        float_raise(float_flag_divbyzero, status);
        return packFloatx80(1, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    fp0 = floatx80_logn(a, status);
    fp1 = packFloatx80(0, 0x3FFD, LIT64(0xDE5BD8A937287195)); /* INV_L10 */

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_mul(fp0, fp1, status); /* LOGN(X)*INV_L10 */

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | Log base 2
 *----------------------------------------------------------------------------*/

floatx80 floatx80_log2(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign == 0) {
            return packFloatx80(0, floatx80_infinity_high,
                                floatx80_infinity_low);
        }
    }

    if (aExp == 0) {
        if (aSig == 0) {
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(1, floatx80_infinity_high,
                                floatx80_infinity_low);
        }
        normalizeFloatx80Subnormal(aSig, &aExp, &aSig);
    }

    if (aSign) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    if (aSig == one_sig) { /* X is 2^k */
        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = int32_to_floatx80(aExp - 0x3FFF, status);
    } else {
        fp0 = floatx80_logn(a, status);
        fp1 = packFloatx80(0, 0x3FFF, LIT64(0xB8AA3B295C17F0BC)); /* INV_L2 */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, fp1, status); /* LOGN(X)*INV_L2 */
    }

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | e to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etox(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, k, m, m1;
    floatx80 fp0, fp1, fp2, fp3, l2, scale, adjscale;
    flag adjflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return packFloatx80(0, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    adjflag = 0;

    if (aExp >= 0x3FBE) { /* |X| >= 2^(-65) */
        compact = floatx80_make_compact(aExp, aSig);

        if (compact < 0x400CB167) { /* |X| < 16380 log2 */
            fp0 = a;
            fp1 = a;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x42B8AA3B), status),
                               status); /* 64/log2 * X */
            adjflag = 0;
            n = floatx80_to_int32(fp0, status); /* int(64/log2*X) */
            fp0 = int32_to_floatx80(n, status);

            j = n & 0x3F; /* J = N mod 64 */
            m = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
            if (n < 0 && j) {
                /* arithmetic right shift is division and
                 * round towards minus infinity
                 */
                m--;
            }
            m += 0x3FFF; /* biased exponent of 2^(M) */

        expcont1:
            fp2 = fp0; /* N */
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0xBC317218), status),
                               status); /* N * L1, L1 = lead(-log2/64) */
            l2 = packFloatx80(0, 0x3FDC, LIT64(0x82E308654361C4C6));
            fp2 = floatx80_mul(fp2, l2, status); /* N * L2, L1+L2 = -log2/64 */
            fp0 = floatx80_add(fp0, fp1, status); /* X + N*L1 */
            fp0 = floatx80_add(fp0, fp2, status); /* R */

            fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
            fp2 = float32_to_floatx80(make_float32(0x3AB60B70),
                                      status); /* A5 */
            fp2 = floatx80_mul(fp2, fp1, status); /* fp2 is S*A5 */
            fp3 = floatx80_mul(float32_to_floatx80(make_float32(0x3C088895),
                               status), fp1,
                               status); /* fp3 is S*A4 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(make_float64(
                               0x3FA5555555554431), status),
                               status); /* fp2 is A3+S*A5 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(make_float64(
                               0x3FC5555555554018), status),
                               status); /* fp3 is A2+S*A4 */
            fp2 = floatx80_mul(fp2, fp1, status); /* fp2 is S*(A3+S*A5) */
            fp3 = floatx80_mul(fp3, fp1, status); /* fp3 is S*(A2+S*A4) */
            fp2 = floatx80_add(fp2, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status); /* fp2 is A1+S*(A3+S*A5) */
            fp3 = floatx80_mul(fp3, fp0, status); /* fp3 IS R*S*(A2+S*A4) */
            fp2 = floatx80_mul(fp2, fp1,
                               status); /* fp2 IS S*(A1+S*(A3+S*A5)) */
            fp0 = floatx80_add(fp0, fp3, status); /* fp0 IS R+R*S*(A2+S*A4) */
            fp0 = floatx80_add(fp0, fp2, status); /* fp0 IS EXP(R) - 1 */

            fp1 = exp_tbl[j];
            fp0 = floatx80_mul(fp0, fp1, status); /* 2^(J/64)*(Exp(R)-1) */
            fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j], status),
                               status); /* accurate 2^(J/64) */
            fp0 = floatx80_add(fp0, fp1,
                               status); /* 2^(J/64) + 2^(J/64)*(Exp(R)-1) */

            scale = packFloatx80(0, m, one_sig);
            if (adjflag) {
                adjscale = packFloatx80(0, m1, one_sig);
                fp0 = floatx80_mul(fp0, adjscale, status);
            }

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, scale, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else { /* |X| >= 16380 log2 */
            if (compact > 0x400CB27C) { /* |X| >= 16480 log2 */
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;
                if (aSign) {
                    a = roundAndPackFloatx80(
                                           status->floatx80_rounding_precision,
                                           0, -0x1000, aSig, 0, status);
                } else {
                    a = roundAndPackFloatx80(
                                           status->floatx80_rounding_precision,
                                           0, 0x8000, aSig, 0, status);
                }
                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                fp1 = a;
                fp0 = floatx80_mul(fp0, float32_to_floatx80(
                                   make_float32(0x42B8AA3B), status),
                                   status); /* 64/log2 * X */
                adjflag = 1;
                n = floatx80_to_int32(fp0, status); /* int(64/log2*X) */
                fp0 = int32_to_floatx80(n, status);

                j = n & 0x3F; /* J = N mod 64 */
                /* NOTE: this is really arithmetic right shift by 6 */
                k = n / 64;
                if (n < 0 && j) {
                    /* arithmetic right shift is division and
                     * round towards minus infinity
                     */
                    k--;
                }
                /* NOTE: this is really arithmetic right shift by 1 */
                m1 = k / 2;
                if (k < 0 && (k & 1)) {
                    /* arithmetic right shift is division and
                     * round towards minus infinity
                     */
                    m1--;
                }
                m = k - m1;
                m1 += 0x3FFF; /* biased exponent of 2^(M1) */
                m += 0x3FFF; /* biased exponent of 2^(M) */

                goto expcont1;
            }
        }
    } else { /* |X| < 2^(-65) */
        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(a, float32_to_floatx80(make_float32(0x3F800000),
                         status), status); /* 1 + X */

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | 2 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_twotox(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, l, m, m1;
    floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return packFloatx80(0, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    fp0 = a;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FB98000 || compact > 0x400D80C0) {
        /* |X| > 16480 or |X| < 2^(-70) */
        if (compact > 0x3FFF8000) { /* |X| > 16480 */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (aSign) {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, -0x1000, aSig, 0, status);
            } else {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, 0x8000, aSig, 0, status);
            }
        } else { /* |X| < 2^(-70) */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(
                             make_float32(0x3F800000), status),
                             status); /* 1 + X */

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else { /* 2^(-70) <= |X| <= 16480 */
        fp1 = fp0; /* X */
        fp1 = floatx80_mul(fp1, float32_to_floatx80(
                           make_float32(0x42800000), status),
                           status); /* X * 64 */
        n = floatx80_to_int32(fp1, status);
        fp1 = int32_to_floatx80(n, status);
        j = n & 0x3F;
        l = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
        if (n < 0 && j) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            l--;
        }
        m = l / 2; /* NOTE: this is really arithmetic right shift by 1 */
        if (l < 0 && (l & 1)) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            m--;
        }
        m1 = l - m;
        m1 += 0x3FFF; /* ADJFACT IS 2^(M') */

        adjfact = packFloatx80(0, m1, one_sig);
        fact1 = exp2_tbl[j];
        fact1.high += m;
        fact2.high = exp2_tbl2[j] >> 16;
        fact2.high += m;
        fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
        fact2.low <<= 48;

        fp1 = floatx80_mul(fp1, float32_to_floatx80(
                           make_float32(0x3C800000), status),
                           status); /* (1/64)*N */
        fp0 = floatx80_sub(fp0, fp1, status); /* X - (1/64)*INT(64 X) */
        fp2 = packFloatx80(0, 0x3FFE, LIT64(0xB17217F7D1CF79AC)); /* LOG2 */
        fp0 = floatx80_mul(fp0, fp2, status); /* R */

        /* EXPR */
        fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
        fp2 = float64_to_floatx80(make_float64(0x3F56C16D6F7BD0B2),
                                  status); /* A5 */
        fp3 = float64_to_floatx80(make_float64(0x3F811112302C712C),
                                  status); /* A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*A5 */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*A4 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FA5555555554CC1), status),
                           status); /* A3+S*A5 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3FC5555555554A54), status),
                           status); /* A2+S*A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*(A2+S*A4) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FE0000000000000), status),
                           status); /* A1+S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp0, status); /* R*S*(A2+S*A4) */

        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A1+S*(A3+S*A5)) */
        fp0 = floatx80_add(fp0, fp3, status); /* R+R*S*(A2+S*A4) */
        fp0 = floatx80_add(fp0, fp2, status); /* EXP(R) - 1 */

        fp0 = floatx80_mul(fp0, fact1, status);
        fp0 = floatx80_add(fp0, fact2, status);
        fp0 = floatx80_add(fp0, fact1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, adjfact, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | 10 to x
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tentox(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, l, m, m1;
    floatx80 fp0, fp1, fp2, fp3, adjfact, fact1, fact2;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(0, 0, 0);
        }
        return packFloatx80(0, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    fp0 = a;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FB98000 || compact > 0x400B9B07) {
        /* |X| > 16480 LOG2/LOG10 or |X| < 2^(-70) */
        if (compact > 0x3FFF8000) { /* |X| > 16480 */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (aSign) {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, -0x1000, aSig, 0, status);
            } else {
                return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                            0, 0x8000, aSig, 0, status);
            }
        } else { /* |X| < 2^(-70) */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(
                             make_float32(0x3F800000), status),
                             status); /* 1 + X */

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else { /* 2^(-70) <= |X| <= 16480 LOG 2 / LOG 10 */
        fp1 = fp0; /* X */
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x406A934F0979A371),
                           status), status); /* X*64*LOG10/LOG2 */
        n = floatx80_to_int32(fp1, status); /* N=INT(X*64*LOG10/LOG2) */
        fp1 = int32_to_floatx80(n, status);

        j = n & 0x3F;
        l = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
        if (n < 0 && j) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            l--;
        }
        m = l / 2; /* NOTE: this is really arithmetic right shift by 1 */
        if (l < 0 && (l & 1)) {
            /* arithmetic right shift is division and
             * round towards minus infinity
             */
            m--;
        }
        m1 = l - m;
        m1 += 0x3FFF; /* ADJFACT IS 2^(M') */

        adjfact = packFloatx80(0, m1, one_sig);
        fact1 = exp2_tbl[j];
        fact1.high += m;
        fact2.high = exp2_tbl2[j] >> 16;
        fact2.high += m;
        fact2.low = (uint64_t)(exp2_tbl2[j] & 0xFFFF);
        fact2.low <<= 48;

        fp2 = fp1; /* N */
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0x3F734413509F8000), status),
                           status); /* N*(LOG2/64LOG10)_LEAD */
        fp3 = packFloatx80(1, 0x3FCD, LIT64(0xC0219DC1DA994FD2));
        fp2 = floatx80_mul(fp2, fp3, status); /* N*(LOG2/64LOG10)_TRAIL */
        fp0 = floatx80_sub(fp0, fp1, status); /* X - N L_LEAD */
        fp0 = floatx80_sub(fp0, fp2, status); /* X - N L_TRAIL */
        fp2 = packFloatx80(0, 0x4000, LIT64(0x935D8DDDAAA8AC17)); /* LOG10 */
        fp0 = floatx80_mul(fp0, fp2, status); /* R */

        /* EXPR */
        fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
        fp2 = float64_to_floatx80(make_float64(0x3F56C16D6F7BD0B2),
                                  status); /* A5 */
        fp3 = float64_to_floatx80(make_float64(0x3F811112302C712C),
                                  status); /* A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*A5 */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*A4 */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FA5555555554CC1), status),
                           status); /* A3+S*A5 */
        fp3 = floatx80_add(fp3, float64_to_floatx80(
                           make_float64(0x3FC5555555554A54), status),
                           status); /* A2+S*A4 */
        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp1, status); /* S*(A2+S*A4) */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x3FE0000000000000), status),
                           status); /* A1+S*(A3+S*A5) */
        fp3 = floatx80_mul(fp3, fp0, status); /* R*S*(A2+S*A4) */

        fp2 = floatx80_mul(fp2, fp1, status); /* S*(A1+S*(A3+S*A5)) */
        fp0 = floatx80_add(fp0, fp3, status); /* R+R*S*(A2+S*A4) */
        fp0 = floatx80_add(fp0, fp2, status); /* EXP(R) - 1 */

        fp0 = floatx80_mul(fp0, fact1, status);
        fp0 = floatx80_add(fp0, fact2, status);
        fp0 = floatx80_add(fp0, fact1, status);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, adjfact, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tan(floatx80 a, float_status *status)
{
    flag aSign, xSign;
    int32_t aExp, xExp;
    uint64_t aSig, xSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, l, n, j;
    floatx80 fp0, fp1, fp2, fp3, fp4, fp5, invtwopi, twopi1, twopi2;
    float32 twoto63;
    flag endflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;

    if (compact < 0x3FD78000 || compact > 0x4004BC7E) {
        /* 2^(-40) > |X| > 15 PI */
        if (compact > 0x3FFF8000) { /* |X| >= 15 PI */
            /* REDUCEX */
            fp1 = packFloatx80(0, 0, 0);
            if (compact == 0x7FFEFFFF) {
                twopi1 = packFloatx80(aSign ^ 1, 0x7FFE,
                                      LIT64(0xC90FDAA200000000));
                twopi2 = packFloatx80(aSign ^ 1, 0x7FDC,
                                      LIT64(0x85A308D300000000));
                fp0 = floatx80_add(fp0, twopi1, status);
                fp1 = fp0;
                fp0 = floatx80_add(fp0, twopi2, status);
                fp1 = floatx80_sub(fp1, fp0, status);
                fp1 = floatx80_add(fp1, twopi2, status);
            }
        loop:
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xExp -= 0x3FFF;
            if (xExp <= 28) {
                l = 0;
                endflag = 1;
            } else {
                l = xExp - 27;
                endflag = 0;
            }
            invtwopi = packFloatx80(0, 0x3FFE - l,
                                    LIT64(0xA2F9836E4E44152A)); /* INVTWOPI */
            twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
            twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));

            /* SIGN(INARG)*2^63 IN SGL */
            twoto63 = packFloat32(xSign, 0xBE, 0);

            fp2 = floatx80_mul(fp0, invtwopi, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status),
                               status); /* THE FRACT PART OF FP2 IS ROUNDED */
            fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status),
                               status); /* FP2 is N */
            fp4 = floatx80_mul(twopi1, fp2, status); /* W = N*P1 */
            fp5 = floatx80_mul(twopi2, fp2, status); /* w = N*P2 */
            fp3 = floatx80_add(fp4, fp5, status); /* FP3 is P */
            fp4 = floatx80_sub(fp4, fp3, status); /* W-P */
            fp0 = floatx80_sub(fp0, fp3, status); /* FP0 is A := R - P */
            fp4 = floatx80_add(fp4, fp5, status); /* FP4 is p = (W-P)+w */
            fp3 = fp0; /* FP3 is A */
            fp1 = floatx80_sub(fp1, fp4, status); /* FP1 is a := r - p */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 is R := A+a */

            if (endflag > 0) {
                n = floatx80_to_int32(fp2, status);
                goto tancont;
            }
            fp3 = floatx80_sub(fp3, fp0, status); /* A-R */
            fp1 = floatx80_add(fp1, fp3, status); /* FP1 is r := (A-R)+a */
            goto loop;
        } else {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_move(a, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = floatx80_mul(fp0, float64_to_floatx80(
                           make_float64(0x3FE45F306DC9C883), status),
                           status); /* X*2/PI */

        n = floatx80_to_int32(fp1, status);
        j = 32 + n;

        fp0 = floatx80_sub(fp0, pi_tbl[j], status); /* X-Y1 */
        fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status),
                           status); /* FP0 IS R = (X-Y1)-Y2 */

    tancont:
        if (n & 1) {
            /* NODD */
            fp1 = fp0; /* R */
            fp0 = floatx80_mul(fp0, fp0, status); /* S = R*R */
            fp3 = float64_to_floatx80(make_float64(0x3EA0B759F50F8688),
                                      status); /* Q4 */
            fp2 = float64_to_floatx80(make_float64(0xBEF2BAA5A8924F04),
                                      status); /* P3 */
            fp3 = floatx80_mul(fp3, fp0, status); /* SQ4 */
            fp2 = floatx80_mul(fp2, fp0, status); /* SP3 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF346F59B39BA65F), status),
                               status); /* Q3+SQ4 */
            fp4 = packFloatx80(0, 0x3FF6, LIT64(0xE073D3FC199C4A00));
            fp2 = floatx80_add(fp2, fp4, status); /* P2+SP3 */
            fp3 = floatx80_mul(fp3, fp0, status); /* S(Q3+SQ4) */
            fp2 = floatx80_mul(fp2, fp0, status); /* S(P2+SP3) */
            fp4 = packFloatx80(0, 0x3FF9, LIT64(0xD23CD68415D95FA1));
            fp3 = floatx80_add(fp3, fp4, status); /* Q2+S(Q3+SQ4) */
            fp4 = packFloatx80(1, 0x3FFC, LIT64(0x8895A6C5FB423BCA));
            fp2 = floatx80_add(fp2, fp4, status); /* P1+S(P2+SP3) */
            fp3 = floatx80_mul(fp3, fp0, status); /* S(Q2+S(Q3+SQ4)) */
            fp2 = floatx80_mul(fp2, fp0, status); /* S(P1+S(P2+SP3)) */
            fp4 = packFloatx80(1, 0x3FFD, LIT64(0xEEF57E0DA84BC8CE));
            fp3 = floatx80_add(fp3, fp4, status); /* Q1+S(Q2+S(Q3+SQ4)) */
            fp2 = floatx80_mul(fp2, fp1, status); /* RS(P1+S(P2+SP3)) */
            fp0 = floatx80_mul(fp0, fp3, status); /* S(Q1+S(Q2+S(Q3+SQ4))) */
            fp1 = floatx80_add(fp1, fp2, status); /* R+RS(P1+S(P2+SP3)) */
            fp0 = floatx80_add(fp0, float32_to_floatx80(
                               make_float32(0x3F800000), status),
                               status); /* 1+S(Q1+S(Q2+S(Q3+SQ4))) */

            xSign = extractFloatx80Sign(fp1);
            xExp = extractFloatx80Exp(fp1);
            xSig = extractFloatx80Frac(fp1);
            xSign ^= 1;
            fp1 = packFloatx80(xSign, xExp, xSig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_div(fp0, fp1, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
            fp3 = float64_to_floatx80(make_float64(0x3EA0B759F50F8688),
                                      status); /* Q4 */
            fp2 = float64_to_floatx80(make_float64(0xBEF2BAA5A8924F04),
                                      status); /* P3 */
            fp3 = floatx80_mul(fp3, fp1, status); /* SQ4 */
            fp2 = floatx80_mul(fp2, fp1, status); /* SP3 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF346F59B39BA65F), status),
                               status); /* Q3+SQ4 */
            fp4 = packFloatx80(0, 0x3FF6, LIT64(0xE073D3FC199C4A00));
            fp2 = floatx80_add(fp2, fp4, status); /* P2+SP3 */
            fp3 = floatx80_mul(fp3, fp1, status); /* S(Q3+SQ4) */
            fp2 = floatx80_mul(fp2, fp1, status); /* S(P2+SP3) */
            fp4 = packFloatx80(0, 0x3FF9, LIT64(0xD23CD68415D95FA1));
            fp3 = floatx80_add(fp3, fp4, status); /* Q2+S(Q3+SQ4) */
            fp4 = packFloatx80(1, 0x3FFC, LIT64(0x8895A6C5FB423BCA));
            fp2 = floatx80_add(fp2, fp4, status); /* P1+S(P2+SP3) */
            fp3 = floatx80_mul(fp3, fp1, status); /* S(Q2+S(Q3+SQ4)) */
            fp2 = floatx80_mul(fp2, fp1, status); /* S(P1+S(P2+SP3)) */
            fp4 = packFloatx80(1, 0x3FFD, LIT64(0xEEF57E0DA84BC8CE));
            fp3 = floatx80_add(fp3, fp4, status); /* Q1+S(Q2+S(Q3+SQ4)) */
            fp2 = floatx80_mul(fp2, fp0, status); /* RS(P1+S(P2+SP3)) */
            fp1 = floatx80_mul(fp1, fp3, status); /* S(Q1+S(Q2+S(Q3+SQ4))) */
            fp0 = floatx80_add(fp0, fp2, status); /* R+RS(P1+S(P2+SP3)) */
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0x3F800000), status),
                               status); /* 1+S(Q1+S(Q2+S(Q3+SQ4))) */

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_div(fp0, fp1, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

/*----------------------------------------------------------------------------
 | Sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sin(floatx80 a, float_status *status)
{
    flag aSign, xSign;
    int32_t aExp, xExp;
    uint64_t aSig, xSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, l, n, j;
    floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
    float32 posneg1, twoto63;
    flag adjn, endflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    adjn = 0;

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;

    if (compact < 0x3FD78000 || compact > 0x4004BC7E) {
        /* 2^(-40) > |X| > 15 PI */
        if (compact > 0x3FFF8000) { /* |X| >= 15 PI */
            /* REDUCEX */
            fp1 = packFloatx80(0, 0, 0);
            if (compact == 0x7FFEFFFF) {
                twopi1 = packFloatx80(aSign ^ 1, 0x7FFE,
                                      LIT64(0xC90FDAA200000000));
                twopi2 = packFloatx80(aSign ^ 1, 0x7FDC,
                                      LIT64(0x85A308D300000000));
                fp0 = floatx80_add(fp0, twopi1, status);
                fp1 = fp0;
                fp0 = floatx80_add(fp0, twopi2, status);
                fp1 = floatx80_sub(fp1, fp0, status);
                fp1 = floatx80_add(fp1, twopi2, status);
            }
        loop:
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xExp -= 0x3FFF;
            if (xExp <= 28) {
                l = 0;
                endflag = 1;
            } else {
                l = xExp - 27;
                endflag = 0;
            }
            invtwopi = packFloatx80(0, 0x3FFE - l,
                                    LIT64(0xA2F9836E4E44152A)); /* INVTWOPI */
            twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
            twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));

            /* SIGN(INARG)*2^63 IN SGL */
            twoto63 = packFloat32(xSign, 0xBE, 0);

            fp2 = floatx80_mul(fp0, invtwopi, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status),
                               status); /* THE FRACT PART OF FP2 IS ROUNDED */
            fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status),
                               status); /* FP2 is N */
            fp4 = floatx80_mul(twopi1, fp2, status); /* W = N*P1 */
            fp5 = floatx80_mul(twopi2, fp2, status); /* w = N*P2 */
            fp3 = floatx80_add(fp4, fp5, status); /* FP3 is P */
            fp4 = floatx80_sub(fp4, fp3, status); /* W-P */
            fp0 = floatx80_sub(fp0, fp3, status); /* FP0 is A := R - P */
            fp4 = floatx80_add(fp4, fp5, status); /* FP4 is p = (W-P)+w */
            fp3 = fp0; /* FP3 is A */
            fp1 = floatx80_sub(fp1, fp4, status); /* FP1 is a := r - p */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 is R := A+a */

            if (endflag > 0) {
                n = floatx80_to_int32(fp2, status);
                goto sincont;
            }
            fp3 = floatx80_sub(fp3, fp0, status); /* A-R */
            fp1 = floatx80_add(fp1, fp3, status); /* FP1 is r := (A-R)+a */
            goto loop;
        } else {
            /* SINSM */
            fp0 = float32_to_floatx80(make_float32(0x3F800000),
                                      status); /* 1 */

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (adjn) {
                /* COSTINY */
                a = floatx80_sub(fp0, float32_to_floatx80(
                                 make_float32(0x00800000), status), status);
            } else {
                /* SINTINY */
                a = floatx80_move(a, status);
            }
            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = floatx80_mul(fp0, float64_to_floatx80(
                           make_float64(0x3FE45F306DC9C883), status),
                           status); /* X*2/PI */

        n = floatx80_to_int32(fp1, status);
        j = 32 + n;

        fp0 = floatx80_sub(fp0, pi_tbl[j], status); /* X-Y1 */
        fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status),
                           status); /* FP0 IS R = (X-Y1)-Y2 */

    sincont:
        if ((n + adjn) & 1) {
            /* COSPOLY */
            fp0 = floatx80_mul(fp0, fp0, status); /* FP0 IS S */
            fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS T */
            fp2 = float64_to_floatx80(make_float64(0x3D2AC4D0D6011EE3),
                                      status); /* B8 */
            fp3 = float64_to_floatx80(make_float64(0xBDA9396F9F45AC19),
                                      status); /* B7 */

            xSign = extractFloatx80Sign(fp0); /* X IS S */
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            if (((n + adjn) >> 1) & 1) {
                xSign ^= 1;
                posneg1 = make_float32(0xBF800000); /* -1 */
            } else {
                xSign ^= 0;
                posneg1 = make_float32(0x3F800000); /* 1 */
            } /* X IS NOW R'= SGN*R */

            fp2 = floatx80_mul(fp2, fp1, status); /* TB8 */
            fp3 = floatx80_mul(fp3, fp1, status); /* TB7 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3E21EED90612C972), status),
                               status); /* B6+TB8 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE927E4FB79D9FCF), status),
                               status); /* B5+TB7 */
            fp2 = floatx80_mul(fp2, fp1, status); /* T(B6+TB8) */
            fp3 = floatx80_mul(fp3, fp1, status); /* T(B5+TB7) */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EFA01A01A01D423), status),
                               status); /* B4+T(B6+TB8) */
            fp4 = packFloatx80(1, 0x3FF5, LIT64(0xB60B60B60B61D438));
            fp3 = floatx80_add(fp3, fp4, status); /* B3+T(B5+TB7) */
            fp2 = floatx80_mul(fp2, fp1, status); /* T(B4+T(B6+TB8)) */
            fp1 = floatx80_mul(fp1, fp3, status); /* T(B3+T(B5+TB7)) */
            fp4 = packFloatx80(0, 0x3FFA, LIT64(0xAAAAAAAAAAAAAB5E));
            fp2 = floatx80_add(fp2, fp4, status); /* B2+T(B4+T(B6+TB8)) */
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0xBF000000), status),
                               status); /* B1+T(B3+T(B5+TB7)) */
            fp0 = floatx80_mul(fp0, fp2, status); /* S(B2+T(B4+T(B6+TB8))) */
            fp0 = floatx80_add(fp0, fp1, status); /* [B1+T(B3+T(B5+TB7))]+
                                                   * [S(B2+T(B4+T(B6+TB8)))]
                                                   */

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            /* SINPOLY */
            xSign = extractFloatx80Sign(fp0); /* X IS R */
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            xSign ^= ((n + adjn) >> 1) & 1; /* X IS NOW R'= SGN*R */

            fp0 = floatx80_mul(fp0, fp0, status); /* FP0 IS S */
            fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS T */
            fp3 = float64_to_floatx80(make_float64(0xBD6AAA77CCC994F5),
                                      status); /* A7 */
            fp2 = float64_to_floatx80(make_float64(0x3DE612097AAE8DA1),
                                      status); /* A6 */
            fp3 = floatx80_mul(fp3, fp1, status); /* T*A7 */
            fp2 = floatx80_mul(fp2, fp1, status); /* T*A6 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE5AE6452A118AE4), status),
                               status); /* A5+T*A7 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EC71DE3A5341531), status),
                               status); /* A4+T*A6 */
            fp3 = floatx80_mul(fp3, fp1, status); /* T(A5+TA7) */
            fp2 = floatx80_mul(fp2, fp1, status); /* T(A4+TA6) */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF2A01A01A018B59), status),
                               status); /* A3+T(A5+TA7) */
            fp4 = packFloatx80(0, 0x3FF8, LIT64(0x88888888888859AF));
            fp2 = floatx80_add(fp2, fp4, status); /* A2+T(A4+TA6) */
            fp1 = floatx80_mul(fp1, fp3, status); /* T(A3+T(A5+TA7)) */
            fp2 = floatx80_mul(fp2, fp0, status); /* S(A2+T(A4+TA6)) */
            fp4 = packFloatx80(1, 0x3FFC, LIT64(0xAAAAAAAAAAAAAA99));
            fp1 = floatx80_add(fp1, fp4, status); /* A1+T(A3+T(A5+TA7)) */
            fp1 = floatx80_add(fp1, fp2,
                               status); /* [A1+T(A3+T(A5+TA7))]+
                                         * [S(A2+T(A4+TA6))]
                                         */

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status); /* R'*S */
            fp0 = floatx80_mul(fp0, fp1, status); /* SIN(R')-R' */

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, x, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

/*----------------------------------------------------------------------------
 | Cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_cos(floatx80 a, float_status *status)
{
    flag aSign, xSign;
    int32_t aExp, xExp;
    uint64_t aSig, xSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, l, n, j;
    floatx80 fp0, fp1, fp2, fp3, fp4, fp5, x, invtwopi, twopi1, twopi2;
    float32 posneg1, twoto63;
    flag adjn, endflag;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    adjn = 1;

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    fp0 = a;

    if (compact < 0x3FD78000 || compact > 0x4004BC7E) {
        /* 2^(-40) > |X| > 15 PI */
        if (compact > 0x3FFF8000) { /* |X| >= 15 PI */
            /* REDUCEX */
            fp1 = packFloatx80(0, 0, 0);
            if (compact == 0x7FFEFFFF) {
                twopi1 = packFloatx80(aSign ^ 1, 0x7FFE,
                                      LIT64(0xC90FDAA200000000));
                twopi2 = packFloatx80(aSign ^ 1, 0x7FDC,
                                      LIT64(0x85A308D300000000));
                fp0 = floatx80_add(fp0, twopi1, status);
                fp1 = fp0;
                fp0 = floatx80_add(fp0, twopi2, status);
                fp1 = floatx80_sub(fp1, fp0, status);
                fp1 = floatx80_add(fp1, twopi2, status);
            }
        loop:
            xSign = extractFloatx80Sign(fp0);
            xExp = extractFloatx80Exp(fp0);
            xExp -= 0x3FFF;
            if (xExp <= 28) {
                l = 0;
                endflag = 1;
            } else {
                l = xExp - 27;
                endflag = 0;
            }
            invtwopi = packFloatx80(0, 0x3FFE - l,
                                    LIT64(0xA2F9836E4E44152A)); /* INVTWOPI */
            twopi1 = packFloatx80(0, 0x3FFF + l, LIT64(0xC90FDAA200000000));
            twopi2 = packFloatx80(0, 0x3FDD + l, LIT64(0x85A308D300000000));

            /* SIGN(INARG)*2^63 IN SGL */
            twoto63 = packFloat32(xSign, 0xBE, 0);

            fp2 = floatx80_mul(fp0, invtwopi, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(twoto63, status),
                               status); /* THE FRACT PART OF FP2 IS ROUNDED */
            fp2 = floatx80_sub(fp2, float32_to_floatx80(twoto63, status),
                               status); /* FP2 is N */
            fp4 = floatx80_mul(twopi1, fp2, status); /* W = N*P1 */
            fp5 = floatx80_mul(twopi2, fp2, status); /* w = N*P2 */
            fp3 = floatx80_add(fp4, fp5, status); /* FP3 is P */
            fp4 = floatx80_sub(fp4, fp3, status); /* W-P */
            fp0 = floatx80_sub(fp0, fp3, status); /* FP0 is A := R - P */
            fp4 = floatx80_add(fp4, fp5, status); /* FP4 is p = (W-P)+w */
            fp3 = fp0; /* FP3 is A */
            fp1 = floatx80_sub(fp1, fp4, status); /* FP1 is a := r - p */
            fp0 = floatx80_add(fp0, fp1, status); /* FP0 is R := A+a */

            if (endflag > 0) {
                n = floatx80_to_int32(fp2, status);
                goto sincont;
            }
            fp3 = floatx80_sub(fp3, fp0, status); /* A-R */
            fp1 = floatx80_add(fp1, fp3, status); /* FP1 is r := (A-R)+a */
            goto loop;
        } else {
            /* SINSM */
            fp0 = float32_to_floatx80(make_float32(0x3F800000), status); /* 1 */

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            if (adjn) {
                /* COSTINY */
                a = floatx80_sub(fp0, float32_to_floatx80(
                                 make_float32(0x00800000), status),
                                 status);
            } else {
                /* SINTINY */
                a = floatx80_move(a, status);
            }
            float_raise(float_flag_inexact, status);

            return a;
        }
    } else {
        fp1 = floatx80_mul(fp0, float64_to_floatx80(
                           make_float64(0x3FE45F306DC9C883), status),
                           status); /* X*2/PI */

        n = floatx80_to_int32(fp1, status);
        j = 32 + n;

        fp0 = floatx80_sub(fp0, pi_tbl[j], status); /* X-Y1 */
        fp0 = floatx80_sub(fp0, float32_to_floatx80(pi_tbl2[j], status),
                           status); /* FP0 IS R = (X-Y1)-Y2 */

    sincont:
        if ((n + adjn) & 1) {
            /* COSPOLY */
            fp0 = floatx80_mul(fp0, fp0, status); /* FP0 IS S */
            fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS T */
            fp2 = float64_to_floatx80(make_float64(0x3D2AC4D0D6011EE3),
                                      status); /* B8 */
            fp3 = float64_to_floatx80(make_float64(0xBDA9396F9F45AC19),
                                      status); /* B7 */

            xSign = extractFloatx80Sign(fp0); /* X IS S */
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            if (((n + adjn) >> 1) & 1) {
                xSign ^= 1;
                posneg1 = make_float32(0xBF800000); /* -1 */
            } else {
                xSign ^= 0;
                posneg1 = make_float32(0x3F800000); /* 1 */
            } /* X IS NOW R'= SGN*R */

            fp2 = floatx80_mul(fp2, fp1, status); /* TB8 */
            fp3 = floatx80_mul(fp3, fp1, status); /* TB7 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3E21EED90612C972), status),
                               status); /* B6+TB8 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE927E4FB79D9FCF), status),
                               status); /* B5+TB7 */
            fp2 = floatx80_mul(fp2, fp1, status); /* T(B6+TB8) */
            fp3 = floatx80_mul(fp3, fp1, status); /* T(B5+TB7) */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EFA01A01A01D423), status),
                               status); /* B4+T(B6+TB8) */
            fp4 = packFloatx80(1, 0x3FF5, LIT64(0xB60B60B60B61D438));
            fp3 = floatx80_add(fp3, fp4, status); /* B3+T(B5+TB7) */
            fp2 = floatx80_mul(fp2, fp1, status); /* T(B4+T(B6+TB8)) */
            fp1 = floatx80_mul(fp1, fp3, status); /* T(B3+T(B5+TB7)) */
            fp4 = packFloatx80(0, 0x3FFA, LIT64(0xAAAAAAAAAAAAAB5E));
            fp2 = floatx80_add(fp2, fp4, status); /* B2+T(B4+T(B6+TB8)) */
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0xBF000000), status),
                               status); /* B1+T(B3+T(B5+TB7)) */
            fp0 = floatx80_mul(fp0, fp2, status); /* S(B2+T(B4+T(B6+TB8))) */
            fp0 = floatx80_add(fp0, fp1, status);
                              /* [B1+T(B3+T(B5+TB7))]+[S(B2+T(B4+T(B6+TB8)))] */

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, float32_to_floatx80(posneg1, status), status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            /* SINPOLY */
            xSign = extractFloatx80Sign(fp0); /* X IS R */
            xExp = extractFloatx80Exp(fp0);
            xSig = extractFloatx80Frac(fp0);

            xSign ^= ((n + adjn) >> 1) & 1; /* X IS NOW R'= SGN*R */

            fp0 = floatx80_mul(fp0, fp0, status); /* FP0 IS S */
            fp1 = floatx80_mul(fp0, fp0, status); /* FP1 IS T */
            fp3 = float64_to_floatx80(make_float64(0xBD6AAA77CCC994F5),
                                      status); /* A7 */
            fp2 = float64_to_floatx80(make_float64(0x3DE612097AAE8DA1),
                                      status); /* A6 */
            fp3 = floatx80_mul(fp3, fp1, status); /* T*A7 */
            fp2 = floatx80_mul(fp2, fp1, status); /* T*A6 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBE5AE6452A118AE4), status),
                               status); /* A5+T*A7 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EC71DE3A5341531), status),
                               status); /* A4+T*A6 */
            fp3 = floatx80_mul(fp3, fp1, status); /* T(A5+TA7) */
            fp2 = floatx80_mul(fp2, fp1, status); /* T(A4+TA6) */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0xBF2A01A01A018B59), status),
                               status); /* A3+T(A5+TA7) */
            fp4 = packFloatx80(0, 0x3FF8, LIT64(0x88888888888859AF));
            fp2 = floatx80_add(fp2, fp4, status); /* A2+T(A4+TA6) */
            fp1 = floatx80_mul(fp1, fp3, status); /* T(A3+T(A5+TA7)) */
            fp2 = floatx80_mul(fp2, fp0, status); /* S(A2+T(A4+TA6)) */
            fp4 = packFloatx80(1, 0x3FFC, LIT64(0xAAAAAAAAAAAAAA99));
            fp1 = floatx80_add(fp1, fp4, status); /* A1+T(A3+T(A5+TA7)) */
            fp1 = floatx80_add(fp1, fp2, status);
                                    /* [A1+T(A3+T(A5+TA7))]+[S(A2+T(A4+TA6))] */

            x = packFloatx80(xSign, xExp, xSig);
            fp0 = floatx80_mul(fp0, x, status); /* R'*S */
            fp0 = floatx80_mul(fp0, fp1, status); /* SIN(R')-R' */

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, x, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

/*----------------------------------------------------------------------------
 | Arc tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_atan(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, tbl_index;
    floatx80 fp0, fp1, fp2, fp3, xsave;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig<<1)) return propagateFloatx80NaNOneArg(a, status);
        a = packFloatx80(aSign, piby2_exp, pi_sig);
        float_raise(float_flag_inexact, status);
        return floatx80_move(a, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    compact = floatx80_make_compact(aExp, aSig);

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    if (compact < 0x3FFB8000 || compact > 0x4002FFFF) {
        /* |X| >= 16 or |X| < 1/16 */
        if (compact > 0x3FFF8000) { /* |X| >= 16 */
            if (compact > 0x40638000) { /* |X| > 2^(100) */
                fp0 = packFloatx80(aSign, piby2_exp, pi_sig);
                fp1 = packFloatx80(aSign, 0x0001, one_sig);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_sub(fp0, fp1, status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                fp1 = packFloatx80(1, one_exp, one_sig); /* -1 */
                fp1 = floatx80_div(fp1, fp0, status); /* X' = -1/X */
                xsave = fp1;
                fp0 = floatx80_mul(fp1, fp1, status); /* Y = X'*X' */
                fp1 = floatx80_mul(fp0, fp0, status); /* Z = Y*Y */
                fp3 = float64_to_floatx80(make_float64(0xBFB70BF398539E6A),
                                          status); /* C5 */
                fp2 = float64_to_floatx80(make_float64(0x3FBC7187962D1D7D),
                                          status); /* C4 */
                fp3 = floatx80_mul(fp3, fp1, status); /* Z*C5 */
                fp2 = floatx80_mul(fp2, fp1, status); /* Z*C4 */
                fp3 = floatx80_add(fp3, float64_to_floatx80(
                                   make_float64(0xBFC24924827107B8), status),
                                   status); /* C3+Z*C5 */
                fp2 = floatx80_add(fp2, float64_to_floatx80(
                                   make_float64(0x3FC999999996263E), status),
                                   status); /* C2+Z*C4 */
                fp1 = floatx80_mul(fp1, fp3, status); /* Z*(C3+Z*C5) */
                fp2 = floatx80_mul(fp2, fp0, status); /* Y*(C2+Z*C4) */
                fp1 = floatx80_add(fp1, float64_to_floatx80(
                                   make_float64(0xBFD5555555555536), status),
                                   status); /* C1+Z*(C3+Z*C5) */
                fp0 = floatx80_mul(fp0, xsave, status); /* X'*Y */
                /* [Y*(C2+Z*C4)]+[C1+Z*(C3+Z*C5)] */
                fp1 = floatx80_add(fp1, fp2, status);
                /* X'*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]) ?? */
                fp0 = floatx80_mul(fp0, fp1, status);
                fp0 = floatx80_add(fp0, xsave, status);
                fp1 = packFloatx80(aSign, piby2_exp, pi_sig);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, fp1, status);

                float_raise(float_flag_inexact, status);

                return a;
            }
        } else { /* |X| < 1/16 */
            if (compact < 0x3FD78000) { /* |X| < 2^(-40) */
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_move(a, status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = a;
                xsave = a;
                fp0 = floatx80_mul(fp0, fp0, status); /* Y = X*X */
                fp1 = floatx80_mul(fp0, fp0, status); /* Z = Y*Y */
                fp2 = float64_to_floatx80(make_float64(0x3FB344447F876989),
                                          status); /* B6 */
                fp3 = float64_to_floatx80(make_float64(0xBFB744EE7FAF45DB),
                                          status); /* B5 */
                fp2 = floatx80_mul(fp2, fp1, status); /* Z*B6 */
                fp3 = floatx80_mul(fp3, fp1, status); /* Z*B5 */
                fp2 = floatx80_add(fp2, float64_to_floatx80(
                                   make_float64(0x3FBC71C646940220), status),
                                   status); /* B4+Z*B6 */
                fp3 = floatx80_add(fp3, float64_to_floatx80(
                                   make_float64(0xBFC24924921872F9),
                                   status), status); /* B3+Z*B5 */
                fp2 = floatx80_mul(fp2, fp1, status); /* Z*(B4+Z*B6) */
                fp1 = floatx80_mul(fp1, fp3, status); /* Z*(B3+Z*B5) */
                fp2 = floatx80_add(fp2, float64_to_floatx80(
                                   make_float64(0x3FC9999999998FA9), status),
                                   status); /* B2+Z*(B4+Z*B6) */
                fp1 = floatx80_add(fp1, float64_to_floatx80(
                                   make_float64(0xBFD5555555555555), status),
                                   status); /* B1+Z*(B3+Z*B5) */
                fp2 = floatx80_mul(fp2, fp0, status); /* Y*(B2+Z*(B4+Z*B6)) */
                fp0 = floatx80_mul(fp0, xsave, status); /* X*Y */
                /* [B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))] */
                fp1 = floatx80_add(fp1, fp2, status);
                /* X*Y*([B1+Z*(B3+Z*B5)]+[Y*(B2+Z*(B4+Z*B6))]) */
                fp0 = floatx80_mul(fp0, fp1, status);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, xsave, status);

                float_raise(float_flag_inexact, status);

                return a;
            }
        }
    } else {
        aSig &= LIT64(0xF800000000000000);
        aSig |= LIT64(0x0400000000000000);
        xsave = packFloatx80(aSign, aExp, aSig); /* F */
        fp0 = a;
        fp1 = a; /* X */
        fp2 = packFloatx80(0, one_exp, one_sig); /* 1 */
        fp1 = floatx80_mul(fp1, xsave, status); /* X*F */
        fp0 = floatx80_sub(fp0, xsave, status); /* X-F */
        fp1 = floatx80_add(fp1, fp2, status); /* 1 + X*F */
        fp0 = floatx80_div(fp0, fp1, status); /* U = (X-F)/(1+X*F) */

        tbl_index = compact;

        tbl_index &= 0x7FFF0000;
        tbl_index -= 0x3FFB0000;
        tbl_index >>= 1;
        tbl_index += compact&0x00007800;
        tbl_index >>= 11;

        fp3 = atan_tbl[tbl_index];

        fp3.high |= aSign ? 0x8000 : 0; /* ATAN(F) */

        fp1 = floatx80_mul(fp0, fp0, status); /* V = U*U */
        fp2 = float64_to_floatx80(make_float64(0xBFF6687E314987D8),
                                  status); /* A3 */
        fp2 = floatx80_add(fp2, fp1, status); /* A3+V */
        fp2 = floatx80_mul(fp2, fp1, status); /* V*(A3+V) */
        fp1 = floatx80_mul(fp1, fp0, status); /* U*V */
        fp2 = floatx80_add(fp2, float64_to_floatx80(
                           make_float64(0x4002AC6934A26DB3), status),
                           status); /* A2+V*(A3+V) */
        fp1 = floatx80_mul(fp1, float64_to_floatx80(
                           make_float64(0xBFC2476F4E1DA28E), status),
                           status); /* A1+U*V */
        fp1 = floatx80_mul(fp1, fp2, status); /* A1*U*V*(A2+V*(A3+V)) */
        fp0 = floatx80_add(fp0, fp1, status); /* ATAN(U) */

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_add(fp0, fp3, status); /* ATAN(X) */

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Arc sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_asin(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, fp2, one;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF && (uint64_t) (aSig << 1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    compact = floatx80_make_compact(aExp, aSig);

    if (compact >= 0x3FFF8000) { /* |X| >= 1 */
        if (aExp == one_exp && aSig == one_sig) { /* |X| == 1 */
            float_raise(float_flag_inexact, status);
            a = packFloatx80(aSign, piby2_exp, pi_sig);
            return floatx80_move(a, status);
        } else { /* |X| > 1 */
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }

    } /* |X| < 1 */

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    one = packFloatx80(0, one_exp, one_sig);
    fp0 = a;

    fp1 = floatx80_sub(one, fp0, status);   /* 1 - X */
    fp2 = floatx80_add(one, fp0, status);   /* 1 + X */
    fp1 = floatx80_mul(fp2, fp1, status);   /* (1+X)*(1-X) */
    fp1 = floatx80_sqrt(fp1, status);       /* SQRT((1+X)*(1-X)) */
    fp0 = floatx80_div(fp0, fp1, status);   /* X/SQRT((1+X)*(1-X)) */

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_atan(fp0, status);         /* ATAN(X/SQRT((1+X)*(1-X))) */

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | Arc cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_acos(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, one;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF && (uint64_t) (aSig << 1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }
    if (aExp == 0 && aSig == 0) {
        float_raise(float_flag_inexact, status);
        return roundAndPackFloatx80(status->floatx80_rounding_precision, 0,
                                    piby2_exp, pi_sig, 0, status);
    }

    compact = floatx80_make_compact(aExp, aSig);

    if (compact >= 0x3FFF8000) { /* |X| >= 1 */
        if (aExp == one_exp && aSig == one_sig) { /* |X| == 1 */
            if (aSign) { /* X == -1 */
                a = packFloatx80(0, pi_exp, pi_sig);
                float_raise(float_flag_inexact, status);
                return floatx80_move(a, status);
            } else { /* X == +1 */
                return packFloatx80(0, 0, 0);
            }
        } else { /* |X| > 1 */
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
    } /* |X| < 1 */

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    one = packFloatx80(0, one_exp, one_sig);
    fp0 = a;

    fp1 = floatx80_add(one, fp0, status);   /* 1 + X */
    fp0 = floatx80_sub(one, fp0, status);   /* 1 - X */
    fp0 = floatx80_div(fp0, fp1, status);   /* (1-X)/(1+X) */
    fp0 = floatx80_sqrt(fp0, status);       /* SQRT((1-X)/(1+X)) */
    fp0 = floatx80_atan(fp0, status);       /* ATAN(SQRT((1-X)/(1+X))) */

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_add(fp0, fp0, status);     /* 2 * ATAN(SQRT((1-X)/(1+X))) */

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | Hyperbolic arc tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_atanh(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, fp2, one;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF && (uint64_t) (aSig << 1)) {
        return propagateFloatx80NaNOneArg(a, status);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    compact = floatx80_make_compact(aExp, aSig);

    if (compact >= 0x3FFF8000) { /* |X| >= 1 */
        if (aExp == one_exp && aSig == one_sig) { /* |X| == 1 */
            float_raise(float_flag_divbyzero, status);
            return packFloatx80(aSign, floatx80_infinity_high,
                                floatx80_infinity_low);
        } else { /* |X| > 1 */
            float_raise(float_flag_invalid, status);
            return floatx80_default_nan(status);
        }
    } /* |X| < 1 */

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    one = packFloatx80(0, one_exp, one_sig);
    fp2 = packFloatx80(aSign, 0x3FFE, one_sig); /* SIGN(X) * (1/2) */
    fp0 = packFloatx80(0, aExp, aSig); /* Y = |X| */
    fp1 = packFloatx80(1, aExp, aSig); /* -Y */
    fp0 = floatx80_add(fp0, fp0, status); /* 2Y */
    fp1 = floatx80_add(fp1, one, status); /* 1-Y */
    fp0 = floatx80_div(fp0, fp1, status); /* Z = 2Y/(1-Y) */
    fp0 = floatx80_lognp1(fp0, status); /* LOG1P(Z) */

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_mul(fp0, fp2,
                     status); /* ATANH(X) = SIGN(X) * (1/2) * LOG1P(Z) */

    float_raise(float_flag_inexact, status);

    return a;
}

/*----------------------------------------------------------------------------
 | e to x minus 1
 *----------------------------------------------------------------------------*/

floatx80 floatx80_etoxm1(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact, n, j, m, m1;
    floatx80 fp0, fp1, fp2, fp3, l2, sc, onebysc;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        if (aSign) {
            return packFloatx80(aSign, one_exp, one_sig);
        }
        return packFloatx80(0, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    if (aExp >= 0x3FFD) { /* |X| >= 1/4 */
        compact = floatx80_make_compact(aExp, aSig);

        if (compact <= 0x4004C215) { /* |X| <= 70 log2 */
            fp0 = a;
            fp1 = a;
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x42B8AA3B), status),
                               status); /* 64/log2 * X */
            n = floatx80_to_int32(fp0, status); /* int(64/log2*X) */
            fp0 = int32_to_floatx80(n, status);

            j = n & 0x3F; /* J = N mod 64 */
            m = n / 64; /* NOTE: this is really arithmetic right shift by 6 */
            if (n < 0 && j) {
                /* arithmetic right shift is division and
                 * round towards minus infinity
                 */
                m--;
            }
            m1 = -m;
            /*m += 0x3FFF; // biased exponent of 2^(M) */
            /*m1 += 0x3FFF; // biased exponent of -2^(-M) */

            fp2 = fp0; /* N */
            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0xBC317218), status),
                               status); /* N * L1, L1 = lead(-log2/64) */
            l2 = packFloatx80(0, 0x3FDC, LIT64(0x82E308654361C4C6));
            fp2 = floatx80_mul(fp2, l2, status); /* N * L2, L1+L2 = -log2/64 */
            fp0 = floatx80_add(fp0, fp1, status); /* X + N*L1 */
            fp0 = floatx80_add(fp0, fp2, status); /* R */

            fp1 = floatx80_mul(fp0, fp0, status); /* S = R*R */
            fp2 = float32_to_floatx80(make_float32(0x3950097B),
                                      status); /* A6 */
            fp2 = floatx80_mul(fp2, fp1, status); /* fp2 is S*A6 */
            fp3 = floatx80_mul(float32_to_floatx80(make_float32(0x3AB60B6A),
                               status), fp1, status); /* fp3 is S*A5 */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3F81111111174385), status),
                               status); /* fp2 IS A4+S*A6 */
            fp3 = floatx80_add(fp3, float64_to_floatx80(
                               make_float64(0x3FA5555555554F5A), status),
                               status); /* fp3 is A3+S*A5 */
            fp2 = floatx80_mul(fp2, fp1, status); /* fp2 IS S*(A4+S*A6) */
            fp3 = floatx80_mul(fp3, fp1, status); /* fp3 IS S*(A3+S*A5) */
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3FC5555555555555), status),
                               status); /* fp2 IS A2+S*(A4+S*A6) */
            fp3 = floatx80_add(fp3, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status); /* fp3 IS A1+S*(A3+S*A5) */
            fp2 = floatx80_mul(fp2, fp1,
                               status); /* fp2 IS S*(A2+S*(A4+S*A6)) */
            fp1 = floatx80_mul(fp1, fp3,
                               status); /* fp1 IS S*(A1+S*(A3+S*A5)) */
            fp2 = floatx80_mul(fp2, fp0,
                               status); /* fp2 IS R*S*(A2+S*(A4+S*A6)) */
            fp0 = floatx80_add(fp0, fp1,
                               status); /* fp0 IS R+S*(A1+S*(A3+S*A5)) */
            fp0 = floatx80_add(fp0, fp2, status); /* fp0 IS EXP(R) - 1 */

            fp0 = floatx80_mul(fp0, exp_tbl[j],
                               status); /* 2^(J/64)*(Exp(R)-1) */

            if (m >= 64) {
                fp1 = float32_to_floatx80(exp_tbl2[j], status);
                onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); /* -2^(-M) */
                fp1 = floatx80_add(fp1, onebysc, status);
                fp0 = floatx80_add(fp0, fp1, status);
                fp0 = floatx80_add(fp0, exp_tbl[j], status);
            } else if (m < -3) {
                fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j],
                                   status), status);
                fp0 = floatx80_add(fp0, exp_tbl[j], status);
                onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); /* -2^(-M) */
                fp0 = floatx80_add(fp0, onebysc, status);
            } else { /* -3 <= m <= 63 */
                fp1 = exp_tbl[j];
                fp0 = floatx80_add(fp0, float32_to_floatx80(exp_tbl2[j],
                                   status), status);
                onebysc = packFloatx80(1, m1 + 0x3FFF, one_sig); /* -2^(-M) */
                fp1 = floatx80_add(fp1, onebysc, status);
                fp0 = floatx80_add(fp0, fp1, status);
            }

            sc = packFloatx80(0, m + 0x3FFF, one_sig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, sc, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else { /* |X| > 70 log2 */
            if (aSign) {
                fp0 = float32_to_floatx80(make_float32(0xBF800000),
                      status); /* -1 */

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, float32_to_floatx80(
                                 make_float32(0x00800000), status),
                                 status); /* -1 + 2^(-126) */

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                return floatx80_etox(a, status);
            }
        }
    } else { /* |X| < 1/4 */
        if (aExp >= 0x3FBE) {
            fp0 = a;
            fp0 = floatx80_mul(fp0, fp0, status); /* S = X*X */
            fp1 = float32_to_floatx80(make_float32(0x2F30CAA8),
                                      status); /* B12 */
            fp1 = floatx80_mul(fp1, fp0, status); /* S * B12 */
            fp2 = float32_to_floatx80(make_float32(0x310F8290),
                                      status); /* B11 */
            fp1 = floatx80_add(fp1, float32_to_floatx80(
                               make_float32(0x32D73220), status),
                               status); /* B10 */
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float32_to_floatx80(
                               make_float32(0x3493F281), status),
                               status); /* B9 */
            fp1 = floatx80_add(fp1, float64_to_floatx80(
                               make_float64(0x3EC71DE3A5774682), status),
                               status); /* B8 */
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3EFA01A019D7CB68), status),
                               status); /* B7 */
            fp1 = floatx80_add(fp1, float64_to_floatx80(
                               make_float64(0x3F2A01A01A019DF3), status),
                               status); /* B6 */
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3F56C16C16C170E2), status),
                               status); /* B5 */
            fp1 = floatx80_add(fp1, float64_to_floatx80(
                               make_float64(0x3F81111111111111), status),
                               status); /* B4 */
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);
            fp2 = floatx80_add(fp2, float64_to_floatx80(
                               make_float64(0x3FA5555555555555), status),
                               status); /* B3 */
            fp3 = packFloatx80(0, 0x3FFC, LIT64(0xAAAAAAAAAAAAAAAB));
            fp1 = floatx80_add(fp1, fp3, status); /* B2 */
            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, fp0, status);

            fp2 = floatx80_mul(fp2, fp0, status);
            fp1 = floatx80_mul(fp1, a, status);

            fp0 = floatx80_mul(fp0, float32_to_floatx80(
                               make_float32(0x3F000000), status),
                               status); /* S*B1 */
            fp1 = floatx80_add(fp1, fp2, status); /* Q */
            fp0 = floatx80_add(fp0, fp1, status); /* S*B1+Q */

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_add(fp0, a, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else { /* |X| < 2^(-65) */
            sc = packFloatx80(1, 1, one_sig);
            fp0 = a;

            if (aExp < 0x0033) { /* |X| < 2^(-16382) */
                fp0 = floatx80_mul(fp0, float64_to_floatx80(
                                   make_float64(0x48B0000000000000), status),
                                   status);
                fp0 = floatx80_add(fp0, sc, status);

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_mul(fp0, float64_to_floatx80(
                                 make_float64(0x3730000000000000), status),
                                 status);
            } else {
                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, sc, status);
            }

            float_raise(float_flag_inexact, status);

            return a;
        }
    }
}

/*----------------------------------------------------------------------------
 | Hyperbolic tangent
 *----------------------------------------------------------------------------*/

floatx80 floatx80_tanh(floatx80 a, float_status *status)
{
    flag aSign, vSign;
    int32_t aExp, vExp;
    uint64_t aSig, vSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1;
    uint32_t sign;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return packFloatx80(aSign, one_exp, one_sig);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact < 0x3FD78000 || compact > 0x3FFFDDCE) {
        /* TANHBORS */
        if (compact < 0x3FFF8000) {
            /* TANHSM */
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_move(a, status);

            float_raise(float_flag_inexact, status);

            return a;
        } else {
            if (compact > 0x40048AA1) {
                /* TANHHUGE */
                sign = 0x3F800000;
                sign |= aSign ? 0x80000000 : 0x00000000;
                fp0 = float32_to_floatx80(make_float32(sign), status);
                sign &= 0x80000000;
                sign ^= 0x80800000; /* -SIGN(X)*EPS */

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp0, float32_to_floatx80(make_float32(sign),
                                 status), status);

                float_raise(float_flag_inexact, status);

                return a;
            } else {
                fp0 = packFloatx80(0, aExp + 1, aSig); /* Y = 2|X| */
                fp0 = floatx80_etox(fp0, status); /* FP0 IS EXP(Y) */
                fp0 = floatx80_add(fp0, float32_to_floatx80(
                                   make_float32(0x3F800000),
                                   status), status); /* EXP(Y)+1 */
                sign = aSign ? 0x80000000 : 0x00000000;
                fp1 = floatx80_div(float32_to_floatx80(make_float32(
                                   sign ^ 0xC0000000), status), fp0,
                                   status); /* -SIGN(X)*2 / [EXP(Y)+1] */
                fp0 = float32_to_floatx80(make_float32(sign | 0x3F800000),
                                          status); /* SIGN */

                status->float_rounding_mode = user_rnd_mode;
                status->floatx80_rounding_precision = user_rnd_prec;

                a = floatx80_add(fp1, fp0, status);

                float_raise(float_flag_inexact, status);

                return a;
            }
        }
    } else { /* 2**(-40) < |X| < (5/2)LOG2 */
        fp0 = packFloatx80(0, aExp + 1, aSig); /* Y = 2|X| */
        fp0 = floatx80_etoxm1(fp0, status); /* FP0 IS Z = EXPM1(Y) */
        fp1 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x40000000),
                           status),
                           status); /* Z+2 */

        vSign = extractFloatx80Sign(fp1);
        vExp = extractFloatx80Exp(fp1);
        vSig = extractFloatx80Frac(fp1);

        fp1 = packFloatx80(vSign ^ aSign, vExp, vSig);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_div(fp0, fp1, status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Hyperbolic sine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_sinh(floatx80 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1, fp2;
    float32 fact;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);
    aSign = extractFloatx80Sign(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return packFloatx80(aSign, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(aSign, 0, 0);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact > 0x400CB167) {
        /* SINHBIG */
        if (compact > 0x400CB2B3) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            return roundAndPackFloatx80(status->floatx80_rounding_precision,
                                        aSign, 0x8000, aSig, 0, status);
        } else {
            fp0 = floatx80_abs(a); /* Y = |X| */
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x40C62D38D3D64634), status),
                               status); /* (|X|-16381LOG2_LEAD) */
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x3D6F90AEB1E75CC7), status),
                               status); /* |X| - 16381 LOG2, ACCURATE */
            fp0 = floatx80_etox(fp0, status);
            fp2 = packFloatx80(aSign, 0x7FFB, one_sig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, fp2, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    } else { /* |X| < 16380 LOG2 */
        fp0 = floatx80_abs(a); /* Y = |X| */
        fp0 = floatx80_etoxm1(fp0, status); /* FP0 IS Z = EXPM1(Y) */
        fp1 = floatx80_add(fp0, float32_to_floatx80(make_float32(0x3F800000),
                           status), status); /* 1+Z */
        fp2 = fp0;
        fp0 = floatx80_div(fp0, fp1, status); /* Z/(1+Z) */
        fp0 = floatx80_add(fp0, fp2, status);

        fact = packFloat32(aSign, 0x7E, 0);

        status->float_rounding_mode = user_rnd_mode;
        status->floatx80_rounding_precision = user_rnd_prec;

        a = floatx80_mul(fp0, float32_to_floatx80(fact, status), status);

        float_raise(float_flag_inexact, status);

        return a;
    }
}

/*----------------------------------------------------------------------------
 | Hyperbolic cosine
 *----------------------------------------------------------------------------*/

floatx80 floatx80_cosh(floatx80 a, float_status *status)
{
    int32_t aExp;
    uint64_t aSig;

    int8_t user_rnd_mode, user_rnd_prec;

    int32_t compact;
    floatx80 fp0, fp1;

    aSig = extractFloatx80Frac(a);
    aExp = extractFloatx80Exp(a);

    if (aExp == 0x7FFF) {
        if ((uint64_t) (aSig << 1)) {
            return propagateFloatx80NaNOneArg(a, status);
        }
        return packFloatx80(0, floatx80_infinity_high,
                            floatx80_infinity_low);
    }

    if (aExp == 0 && aSig == 0) {
        return packFloatx80(0, one_exp, one_sig);
    }

    user_rnd_mode = status->float_rounding_mode;
    user_rnd_prec = status->floatx80_rounding_precision;
    status->float_rounding_mode = float_round_nearest_even;
    status->floatx80_rounding_precision = 80;

    compact = floatx80_make_compact(aExp, aSig);

    if (compact > 0x400CB167) {
        if (compact > 0x400CB2B3) {
            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;
            return roundAndPackFloatx80(status->floatx80_rounding_precision, 0,
                                        0x8000, one_sig, 0, status);
        } else {
            fp0 = packFloatx80(0, aExp, aSig);
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x40C62D38D3D64634), status),
                               status);
            fp0 = floatx80_sub(fp0, float64_to_floatx80(
                               make_float64(0x3D6F90AEB1E75CC7), status),
                               status);
            fp0 = floatx80_etox(fp0, status);
            fp1 = packFloatx80(0, 0x7FFB, one_sig);

            status->float_rounding_mode = user_rnd_mode;
            status->floatx80_rounding_precision = user_rnd_prec;

            a = floatx80_mul(fp0, fp1, status);

            float_raise(float_flag_inexact, status);

            return a;
        }
    }

    fp0 = packFloatx80(0, aExp, aSig); /* |X| */
    fp0 = floatx80_etox(fp0, status); /* EXP(|X|) */
    fp0 = floatx80_mul(fp0, float32_to_floatx80(make_float32(0x3F000000),
                       status), status); /* (1/2)*EXP(|X|) */
    fp1 = float32_to_floatx80(make_float32(0x3E800000), status); /* 1/4 */
    fp1 = floatx80_div(fp1, fp0, status); /* 1/(2*EXP(|X|)) */

    status->float_rounding_mode = user_rnd_mode;
    status->floatx80_rounding_precision = user_rnd_prec;

    a = floatx80_add(fp0, fp1, status);

    float_raise(float_flag_inexact, status);

    return a;
}
#endif /* TARGET_M68K */

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is equal
| to the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_eq(floatx80 a, floatx80 b, float_status *status)
{

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint16_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is
| less than or equal to the corresponding value `b', and 0 otherwise.  The
| invalid exception is raised if either operand is a NaN.  The comparison is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_le(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is
| less than the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_lt(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point values `a' and `b'
| cannot be compared, and 0 otherwise.  The invalid exception is raised if
| either operand is a NaN.   The comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
int floatx80_unordered(floatx80 a, floatx80 b, float_status *status)
{
    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)
        || (extractFloatx80Exp(a) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(a) << 1))
        || (extractFloatx80Exp(b) == 0x7FFF
            && (uint64_t) (extractFloatx80Frac(b) << 1))
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is
| equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_eq_quiet(floatx80 a, floatx80 b, float_status *status)
{

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint16_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is less
| than or equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs
| do not cause an exception.  Otherwise, the comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_le_quiet(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point value `a' is less
| than the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause
| an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int floatx80_lt_quiet(floatx80 a, floatx80 b, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint16_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the extended double-precision floating-point values `a' and `b'
| cannot be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.
| The comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/
int floatx80_unordered_quiet(floatx80 a, floatx80 b, float_status *status)
{
    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    if (    (    ( extractFloatx80Exp( a ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( a )<<1 ) )
         || (    ( extractFloatx80Exp( b ) == 0x7FFF )
              && (uint64_t) ( extractFloatx80Frac( b )<<1 ) )
       ) {
        if (floatx80_is_signaling_nan(a, status)
         || floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 32-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int32_t float128_to_int32(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( ( aExp == 0x7FFF ) && ( aSig0 | aSig1 ) ) aSign = 0;
    if ( aExp ) aSig0 |= LIT64( 0x0001000000000000 );
    aSig0 |= ( aSig1 != 0 );
    shiftCount = 0x4028 - aExp;
    if ( 0 < shiftCount ) shift64RightJamming( aSig0, shiftCount, &aSig0 );
    return roundAndPackInt32(aSign, aSig0, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 32-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.  If
| `a' is a NaN, the largest positive integer is returned.  Otherwise, if the
| conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int32_t float128_to_int32_round_to_zero(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1, savedASig;
    int32_t z;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    aSig0 |= ( aSig1 != 0 );
    if ( 0x401E < aExp ) {
        if ( ( aExp == 0x7FFF ) && aSig0 ) aSign = 0;
        goto invalid;
    }
    else if ( aExp < 0x3FFF ) {
        if (aExp || aSig0) {
            status->float_exception_flags |= float_flag_inexact;
        }
        return 0;
    }
    aSig0 |= LIT64( 0x0001000000000000 );
    shiftCount = 0x402F - aExp;
    savedASig = aSig0;
    aSig0 >>= shiftCount;
    z = aSig0;
    if ( aSign ) z = - z;
    if ( ( z < 0 ) ^ aSign ) {
 invalid:
        float_raise(float_flag_invalid, status);
        return aSign ? (int32_t) 0x80000000 : 0x7FFFFFFF;
    }
    if ( ( aSig0<<shiftCount ) != savedASig ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 64-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  Otherwise, if the conversion overflows, the
| largest integer with the same sign as `a' is returned.
*----------------------------------------------------------------------------*/

int64_t float128_to_int64(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp ) aSig0 |= LIT64( 0x0001000000000000 );
    shiftCount = 0x402F - aExp;
    if ( shiftCount <= 0 ) {
        if ( 0x403E < aExp ) {
            float_raise(float_flag_invalid, status);
            if (    ! aSign
                 || (    ( aExp == 0x7FFF )
                      && ( aSig1 || ( aSig0 != LIT64( 0x0001000000000000 ) ) )
                    )
               ) {
                return LIT64( 0x7FFFFFFFFFFFFFFF );
            }
            return (int64_t) LIT64( 0x8000000000000000 );
        }
        shortShift128Left( aSig0, aSig1, - shiftCount, &aSig0, &aSig1 );
    }
    else {
        shift64ExtraRightJamming( aSig0, aSig1, shiftCount, &aSig0, &aSig1 );
    }
    return roundAndPackInt64(aSign, aSig0, aSig1, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 64-bit two's complement integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic, except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise, if
| the conversion overflows, the largest integer with the same sign as `a' is
| returned.
*----------------------------------------------------------------------------*/

int64_t float128_to_int64_round_to_zero(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, shiftCount;
    uint64_t aSig0, aSig1;
    int64_t z;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp ) aSig0 |= LIT64( 0x0001000000000000 );
    shiftCount = aExp - 0x402F;
    if ( 0 < shiftCount ) {
        if ( 0x403E <= aExp ) {
            aSig0 &= LIT64( 0x0000FFFFFFFFFFFF );
            if (    ( a.high == LIT64( 0xC03E000000000000 ) )
                 && ( aSig1 < LIT64( 0x0002000000000000 ) ) ) {
                if (aSig1) {
                    status->float_exception_flags |= float_flag_inexact;
                }
            }
            else {
                float_raise(float_flag_invalid, status);
                if ( ! aSign || ( ( aExp == 0x7FFF ) && ( aSig0 | aSig1 ) ) ) {
                    return LIT64( 0x7FFFFFFFFFFFFFFF );
                }
            }
            return (int64_t) LIT64( 0x8000000000000000 );
        }
        z = ( aSig0<<shiftCount ) | ( aSig1>>( ( - shiftCount ) & 63 ) );
        if ( (uint64_t) ( aSig1<<shiftCount ) ) {
            status->float_exception_flags |= float_flag_inexact;
        }
    }
    else {
        if ( aExp < 0x3FFF ) {
            if ( aExp | aSig0 | aSig1 ) {
                status->float_exception_flags |= float_flag_inexact;
            }
            return 0;
        }
        z = aSig0>>( - shiftCount );
        if (    aSig1
             || ( shiftCount && (uint64_t) ( aSig0<<( shiftCount & 63 ) ) ) ) {
            status->float_exception_flags |= float_flag_inexact;
        }
    }
    if ( aSign ) z = - z;
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  If the conversion overflows, the
| largest unsigned integer is returned.  If 'a' is negative, the value is
| rounded and zero is returned; negative values that do not round to zero
| will raise the inexact exception.
*----------------------------------------------------------------------------*/

uint64_t float128_to_uint64(float128 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig0, aSig1;

    aSig0 = extractFloat128Frac0(a);
    aSig1 = extractFloat128Frac1(a);
    aExp = extractFloat128Exp(a);
    aSign = extractFloat128Sign(a);
    if (aSign && (aExp > 0x3FFE)) {
        float_raise(float_flag_invalid, status);
        if (float128_is_any_nan(a)) {
            return LIT64(0xFFFFFFFFFFFFFFFF);
        } else {
            return 0;
        }
    }
    if (aExp) {
        aSig0 |= LIT64(0x0001000000000000);
    }
    shiftCount = 0x402F - aExp;
    if (shiftCount <= 0) {
        if (0x403E < aExp) {
            float_raise(float_flag_invalid, status);
            return LIT64(0xFFFFFFFFFFFFFFFF);
        }
        shortShift128Left(aSig0, aSig1, -shiftCount, &aSig0, &aSig1);
    } else {
        shift64ExtraRightJamming(aSig0, aSig1, shiftCount, &aSig0, &aSig1);
    }
    return roundAndPackUint64(aSign, aSig0, aSig1, status);
}

uint64_t float128_to_uint64_round_to_zero(float128 a, float_status *status)
{
    uint64_t v;
    signed char current_rounding_mode = status->float_rounding_mode;

    set_float_rounding_mode(float_round_to_zero, status);
    v = float128_to_uint64(a, status);
    set_float_rounding_mode(current_rounding_mode, status);

    return v;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the 32-bit unsigned integer format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic except that the conversion is always rounded toward zero.
| If `a' is a NaN, the largest positive integer is returned.  Otherwise,
| if the conversion overflows, the largest unsigned integer is returned.
| If 'a' is negative, the value is rounded and zero is returned; negative
| values that do not round to zero will raise the inexact exception.
*----------------------------------------------------------------------------*/

uint32_t float128_to_uint32_round_to_zero(float128 a, float_status *status)
{
    uint64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float128_to_uint64_round_to_zero(a, status);
    if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the single-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float32 float128_to_float32(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;
    uint32_t zSig;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloat32(float128ToCommonNaN(a, status), status);
        }
        return packFloat32( aSign, 0xFF, 0 );
    }
    aSig0 |= ( aSig1 != 0 );
    shift64RightJamming( aSig0, 18, &aSig0 );
    zSig = aSig0;
    if ( aExp || zSig ) {
        zSig |= 0x40000000;
        aExp -= 0x3F81;
    }
    return roundAndPackFloat32(aSign, aExp, zSig, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the double-precision floating-point format.  The conversion
| is performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic.
*----------------------------------------------------------------------------*/

float64 float128_to_float64(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloat64(float128ToCommonNaN(a, status), status);
        }
        return packFloat64( aSign, 0x7FF, 0 );
    }
    shortShift128Left( aSig0, aSig1, 14, &aSig0, &aSig1 );
    aSig0 |= ( aSig1 != 0 );
    if ( aExp || aSig0 ) {
        aSig0 |= LIT64( 0x4000000000000000 );
        aExp -= 0x3C01;
    }
    return roundAndPackFloat64(aSign, aExp, aSig0, status);

}

/*----------------------------------------------------------------------------
| Returns the result of converting the quadruple-precision floating-point
| value `a' to the extended double-precision floating-point format.  The
| conversion is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

floatx80 float128_to_floatx80(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return commonNaNToFloatx80(float128ToCommonNaN(a, status), status);
        }
        return packFloatx80(aSign, floatx80_infinity_high,
                                   floatx80_infinity_low);
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloatx80( aSign, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    else {
        aSig0 |= LIT64( 0x0001000000000000 );
    }
    shortShift128Left( aSig0, aSig1, 15, &aSig0, &aSig1 );
    return roundAndPackFloatx80(80, aSign, aExp, aSig0, aSig1, status);

}

/*----------------------------------------------------------------------------
| Rounds the quadruple-precision floating-point value `a' to an integer, and
| returns the result as a quadruple-precision floating-point value.  The
| operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_round_to_int(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t lastBitMask, roundBitsMask;
    float128 z;

    aExp = extractFloat128Exp( a );
    if ( 0x402F <= aExp ) {
        if ( 0x406F <= aExp ) {
            if (    ( aExp == 0x7FFF )
                 && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) )
               ) {
                return propagateFloat128NaN(a, a, status);
            }
            return a;
        }
        lastBitMask = 1;
        lastBitMask = ( lastBitMask<<( 0x406E - aExp ) )<<1;
        roundBitsMask = lastBitMask - 1;
        z = a;
        switch (status->float_rounding_mode) {
        case float_round_nearest_even:
            if ( lastBitMask ) {
                add128( z.high, z.low, 0, lastBitMask>>1, &z.high, &z.low );
                if ( ( z.low & roundBitsMask ) == 0 ) z.low &= ~ lastBitMask;
            }
            else {
                if ( (int64_t) z.low < 0 ) {
                    ++z.high;
                    if ( (uint64_t) ( z.low<<1 ) == 0 ) z.high &= ~1;
                }
            }
            break;
        case float_round_ties_away:
            if (lastBitMask) {
                add128(z.high, z.low, 0, lastBitMask >> 1, &z.high, &z.low);
            } else {
                if ((int64_t) z.low < 0) {
                    ++z.high;
                }
            }
            break;
        case float_round_to_zero:
            break;
        case float_round_up:
            if (!extractFloat128Sign(z)) {
                add128(z.high, z.low, 0, roundBitsMask, &z.high, &z.low);
            }
            break;
        case float_round_down:
            if (extractFloat128Sign(z)) {
                add128(z.high, z.low, 0, roundBitsMask, &z.high, &z.low);
            }
            break;
        default:
            abort();
        }
        z.low &= ~ roundBitsMask;
    }
    else {
        if ( aExp < 0x3FFF ) {
            if ( ( ( (uint64_t) ( a.high<<1 ) ) | a.low ) == 0 ) return a;
            status->float_exception_flags |= float_flag_inexact;
            aSign = extractFloat128Sign( a );
            switch (status->float_rounding_mode) {
             case float_round_nearest_even:
                if (    ( aExp == 0x3FFE )
                     && (   extractFloat128Frac0( a )
                          | extractFloat128Frac1( a ) )
                   ) {
                    return packFloat128( aSign, 0x3FFF, 0, 0 );
                }
                break;
            case float_round_ties_away:
                if (aExp == 0x3FFE) {
                    return packFloat128(aSign, 0x3FFF, 0, 0);
                }
                break;
             case float_round_down:
                return
                      aSign ? packFloat128( 1, 0x3FFF, 0, 0 )
                    : packFloat128( 0, 0, 0, 0 );
             case float_round_up:
                return
                      aSign ? packFloat128( 1, 0, 0, 0 )
                    : packFloat128( 0, 0x3FFF, 0, 0 );
            }
            return packFloat128( aSign, 0, 0, 0 );
        }
        lastBitMask = 1;
        lastBitMask <<= 0x402F - aExp;
        roundBitsMask = lastBitMask - 1;
        z.low = 0;
        z.high = a.high;
        switch (status->float_rounding_mode) {
        case float_round_nearest_even:
            z.high += lastBitMask>>1;
            if ( ( ( z.high & roundBitsMask ) | a.low ) == 0 ) {
                z.high &= ~ lastBitMask;
            }
            break;
        case float_round_ties_away:
            z.high += lastBitMask>>1;
            break;
        case float_round_to_zero:
            break;
        case float_round_up:
            if (!extractFloat128Sign(z)) {
                z.high |= ( a.low != 0 );
                z.high += roundBitsMask;
            }
            break;
        case float_round_down:
            if (extractFloat128Sign(z)) {
                z.high |= (a.low != 0);
                z.high += roundBitsMask;
            }
            break;
        default:
            abort();
        }
        z.high &= ~ roundBitsMask;
    }
    if ( ( z.low != a.low ) || ( z.high != a.high ) ) {
        status->float_exception_flags |= float_flag_inexact;
    }
    return z;

}

/*----------------------------------------------------------------------------
| Returns the result of adding the absolute values of the quadruple-precision
| floating-point values `a' and `b'.  If `zSign' is 1, the sum is negated
| before being returned.  `zSign' is ignored if the result is a NaN.
| The addition is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 addFloat128Sigs(float128 a, float128 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    int32_t expDiff;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    expDiff = aExp - bExp;
    if ( 0 < expDiff ) {
        if ( aExp == 0x7FFF ) {
            if (aSig0 | aSig1) {
                return propagateFloat128NaN(a, b, status);
            }
            return a;
        }
        if ( bExp == 0 ) {
            --expDiff;
        }
        else {
            bSig0 |= LIT64( 0x0001000000000000 );
        }
        shift128ExtraRightJamming(
            bSig0, bSig1, 0, expDiff, &bSig0, &bSig1, &zSig2 );
        zExp = aExp;
    }
    else if ( expDiff < 0 ) {
        if ( bExp == 0x7FFF ) {
            if (bSig0 | bSig1) {
                return propagateFloat128NaN(a, b, status);
            }
            return packFloat128( zSign, 0x7FFF, 0, 0 );
        }
        if ( aExp == 0 ) {
            ++expDiff;
        }
        else {
            aSig0 |= LIT64( 0x0001000000000000 );
        }
        shift128ExtraRightJamming(
            aSig0, aSig1, 0, - expDiff, &aSig0, &aSig1, &zSig2 );
        zExp = bExp;
    }
    else {
        if ( aExp == 0x7FFF ) {
            if ( aSig0 | aSig1 | bSig0 | bSig1 ) {
                return propagateFloat128NaN(a, b, status);
            }
            return a;
        }
        add128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
        if ( aExp == 0 ) {
            if (status->flush_to_zero) {
                if (zSig0 | zSig1) {
                    float_raise(float_flag_output_denormal, status);
                }
                return packFloat128(zSign, 0, 0, 0);
            }
            return packFloat128( zSign, 0, zSig0, zSig1 );
        }
        zSig2 = 0;
        zSig0 |= LIT64( 0x0002000000000000 );
        zExp = aExp;
        goto shiftRight1;
    }
    aSig0 |= LIT64( 0x0001000000000000 );
    add128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
    --zExp;
    if ( zSig0 < LIT64( 0x0002000000000000 ) ) goto roundAndPack;
    ++zExp;
 shiftRight1:
    shift128ExtraRightJamming(
        zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2 );
 roundAndPack:
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the absolute values of the quadruple-
| precision floating-point values `a' and `b'.  If `zSign' is 1, the
| difference is negated before being returned.  `zSign' is ignored if the
| result is a NaN.  The subtraction is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

static float128 subFloat128Sigs(float128 a, float128 b, flag zSign,
                                float_status *status)
{
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1;
    int32_t expDiff;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    expDiff = aExp - bExp;
    shortShift128Left( aSig0, aSig1, 14, &aSig0, &aSig1 );
    shortShift128Left( bSig0, bSig1, 14, &bSig0, &bSig1 );
    if ( 0 < expDiff ) goto aExpBigger;
    if ( expDiff < 0 ) goto bExpBigger;
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 | bSig0 | bSig1 ) {
            return propagateFloat128NaN(a, b, status);
        }
        float_raise(float_flag_invalid, status);
        return float128_default_nan(status);
    }
    if ( aExp == 0 ) {
        aExp = 1;
        bExp = 1;
    }
    if ( bSig0 < aSig0 ) goto aBigger;
    if ( aSig0 < bSig0 ) goto bBigger;
    if ( bSig1 < aSig1 ) goto aBigger;
    if ( aSig1 < bSig1 ) goto bBigger;
    return packFloat128(status->float_rounding_mode == float_round_down,
                        0, 0, 0);
 bExpBigger:
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return packFloat128( zSign ^ 1, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        ++expDiff;
    }
    else {
        aSig0 |= LIT64( 0x4000000000000000 );
    }
    shift128RightJamming( aSig0, aSig1, - expDiff, &aSig0, &aSig1 );
    bSig0 |= LIT64( 0x4000000000000000 );
 bBigger:
    sub128( bSig0, bSig1, aSig0, aSig1, &zSig0, &zSig1 );
    zExp = bExp;
    zSign ^= 1;
    goto normalizeRoundAndPack;
 aExpBigger:
    if ( aExp == 0x7FFF ) {
        if (aSig0 | aSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        --expDiff;
    }
    else {
        bSig0 |= LIT64( 0x4000000000000000 );
    }
    shift128RightJamming( bSig0, bSig1, expDiff, &bSig0, &bSig1 );
    aSig0 |= LIT64( 0x4000000000000000 );
 aBigger:
    sub128( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1 );
    zExp = aExp;
 normalizeRoundAndPack:
    --zExp;
    return normalizeRoundAndPackFloat128(zSign, zExp - 14, zSig0, zSig1,
                                         status);

}

/*----------------------------------------------------------------------------
| Returns the result of adding the quadruple-precision floating-point values
| `a' and `b'.  The operation is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_add(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign == bSign ) {
        return addFloat128Sigs(a, b, aSign, status);
    }
    else {
        return subFloat128Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of subtracting the quadruple-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_sub(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign == bSign ) {
        return subFloat128Sigs(a, b, aSign, status);
    }
    else {
        return addFloat128Sigs(a, b, aSign, status);
    }

}

/*----------------------------------------------------------------------------
| Returns the result of multiplying the quadruple-precision floating-point
| values `a' and `b'.  The operation is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_mul(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2, zSig3;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    bSign = extractFloat128Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (    ( aSig0 | aSig1 )
             || ( ( bExp == 0x7FFF ) && ( bSig0 | bSig1 ) ) ) {
            return propagateFloat128NaN(a, b, status);
        }
        if ( ( bExp | bSig0 | bSig1 ) == 0 ) goto invalid;
        return packFloat128( zSign, 0x7FFF, 0, 0 );
    }
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        if ( ( aExp | aSig0 | aSig1 ) == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return float128_default_nan(status);
        }
        return packFloat128( zSign, 0x7FFF, 0, 0 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat128( zSign, 0, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) return packFloat128( zSign, 0, 0, 0 );
        normalizeFloat128Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    zExp = aExp + bExp - 0x4000;
    aSig0 |= LIT64( 0x0001000000000000 );
    shortShift128Left( bSig0, bSig1, 16, &bSig0, &bSig1 );
    mul128To256( aSig0, aSig1, bSig0, bSig1, &zSig0, &zSig1, &zSig2, &zSig3 );
    add128( zSig0, zSig1, aSig0, aSig1, &zSig0, &zSig1 );
    zSig2 |= ( zSig3 != 0 );
    if ( LIT64( 0x0002000000000000 ) <= zSig0 ) {
        shift128ExtraRightJamming(
            zSig0, zSig1, zSig2, 1, &zSig0, &zSig1, &zSig2 );
        ++zExp;
    }
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the result of dividing the quadruple-precision floating-point value
| `a' by the corresponding value `b'.  The operation is performed according to
| the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_div(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign, zSign;
    int32_t aExp, bExp, zExp;
    uint64_t aSig0, aSig1, bSig0, bSig1, zSig0, zSig1, zSig2;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    bSign = extractFloat128Sign( b );
    zSign = aSign ^ bSign;
    if ( aExp == 0x7FFF ) {
        if (aSig0 | aSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        if ( bExp == 0x7FFF ) {
            if (bSig0 | bSig1) {
                return propagateFloat128NaN(a, b, status);
            }
            goto invalid;
        }
        return packFloat128( zSign, 0x7FFF, 0, 0 );
    }
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return packFloat128( zSign, 0, 0, 0 );
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) {
            if ( ( aExp | aSig0 | aSig1 ) == 0 ) {
 invalid:
                float_raise(float_flag_invalid, status);
                return float128_default_nan(status);
            }
            float_raise(float_flag_divbyzero, status);
            return packFloat128( zSign, 0x7FFF, 0, 0 );
        }
        normalizeFloat128Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat128( zSign, 0, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    zExp = aExp - bExp + 0x3FFD;
    shortShift128Left(
        aSig0 | LIT64( 0x0001000000000000 ), aSig1, 15, &aSig0, &aSig1 );
    shortShift128Left(
        bSig0 | LIT64( 0x0001000000000000 ), bSig1, 15, &bSig0, &bSig1 );
    if ( le128( bSig0, bSig1, aSig0, aSig1 ) ) {
        shift128Right( aSig0, aSig1, 1, &aSig0, &aSig1 );
        ++zExp;
    }
    zSig0 = estimateDiv128To64( aSig0, aSig1, bSig0 );
    mul128By64To192( bSig0, bSig1, zSig0, &term0, &term1, &term2 );
    sub192( aSig0, aSig1, 0, term0, term1, term2, &rem0, &rem1, &rem2 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        add192( rem0, rem1, rem2, 0, bSig0, bSig1, &rem0, &rem1, &rem2 );
    }
    zSig1 = estimateDiv128To64( rem1, rem2, bSig0 );
    if ( ( zSig1 & 0x3FFF ) <= 4 ) {
        mul128By64To192( bSig0, bSig1, zSig1, &term1, &term2, &term3 );
        sub192( rem1, rem2, 0, term1, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            add192( rem1, rem2, rem3, 0, bSig0, bSig1, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shift128ExtraRightJamming( zSig0, zSig1, 0, 15, &zSig0, &zSig1, &zSig2 );
    return roundAndPackFloat128(zSign, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns the remainder of the quadruple-precision floating-point value `a'
| with respect to the corresponding value `b'.  The operation is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_rem(float128 a, float128 b, float_status *status)
{
    flag aSign, zSign;
    int32_t aExp, bExp, expDiff;
    uint64_t aSig0, aSig1, bSig0, bSig1, q, term0, term1, term2;
    uint64_t allZero, alternateASig0, alternateASig1, sigMean1;
    int64_t sigMean0;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    bSig1 = extractFloat128Frac1( b );
    bSig0 = extractFloat128Frac0( b );
    bExp = extractFloat128Exp( b );
    if ( aExp == 0x7FFF ) {
        if (    ( aSig0 | aSig1 )
             || ( ( bExp == 0x7FFF ) && ( bSig0 | bSig1 ) ) ) {
            return propagateFloat128NaN(a, b, status);
        }
        goto invalid;
    }
    if ( bExp == 0x7FFF ) {
        if (bSig0 | bSig1) {
            return propagateFloat128NaN(a, b, status);
        }
        return a;
    }
    if ( bExp == 0 ) {
        if ( ( bSig0 | bSig1 ) == 0 ) {
 invalid:
            float_raise(float_flag_invalid, status);
            return float128_default_nan(status);
        }
        normalizeFloat128Subnormal( bSig0, bSig1, &bExp, &bSig0, &bSig1 );
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return a;
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    expDiff = aExp - bExp;
    if ( expDiff < -1 ) return a;
    shortShift128Left(
        aSig0 | LIT64( 0x0001000000000000 ),
        aSig1,
        15 - ( expDiff < 0 ),
        &aSig0,
        &aSig1
    );
    shortShift128Left(
        bSig0 | LIT64( 0x0001000000000000 ), bSig1, 15, &bSig0, &bSig1 );
    q = le128( bSig0, bSig1, aSig0, aSig1 );
    if ( q ) sub128( aSig0, aSig1, bSig0, bSig1, &aSig0, &aSig1 );
    expDiff -= 64;
    while ( 0 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig0 );
        q = ( 4 < q ) ? q - 4 : 0;
        mul128By64To192( bSig0, bSig1, q, &term0, &term1, &term2 );
        shortShift192Left( term0, term1, term2, 61, &term1, &term2, &allZero );
        shortShift128Left( aSig0, aSig1, 61, &aSig0, &allZero );
        sub128( aSig0, 0, term1, term2, &aSig0, &aSig1 );
        expDiff -= 61;
    }
    if ( -64 < expDiff ) {
        q = estimateDiv128To64( aSig0, aSig1, bSig0 );
        q = ( 4 < q ) ? q - 4 : 0;
        q >>= - expDiff;
        shift128Right( bSig0, bSig1, 12, &bSig0, &bSig1 );
        expDiff += 52;
        if ( expDiff < 0 ) {
            shift128Right( aSig0, aSig1, - expDiff, &aSig0, &aSig1 );
        }
        else {
            shortShift128Left( aSig0, aSig1, expDiff, &aSig0, &aSig1 );
        }
        mul128By64To192( bSig0, bSig1, q, &term0, &term1, &term2 );
        sub128( aSig0, aSig1, term1, term2, &aSig0, &aSig1 );
    }
    else {
        shift128Right( aSig0, aSig1, 12, &aSig0, &aSig1 );
        shift128Right( bSig0, bSig1, 12, &bSig0, &bSig1 );
    }
    do {
        alternateASig0 = aSig0;
        alternateASig1 = aSig1;
        ++q;
        sub128( aSig0, aSig1, bSig0, bSig1, &aSig0, &aSig1 );
    } while ( 0 <= (int64_t) aSig0 );
    add128(
        aSig0, aSig1, alternateASig0, alternateASig1, (uint64_t *)&sigMean0, &sigMean1 );
    if (    ( sigMean0 < 0 )
         || ( ( ( sigMean0 | sigMean1 ) == 0 ) && ( q & 1 ) ) ) {
        aSig0 = alternateASig0;
        aSig1 = alternateASig1;
    }
    zSign = ( (int64_t) aSig0 < 0 );
    if ( zSign ) sub128( 0, 0, aSig0, aSig1, &aSig0, &aSig1 );
    return normalizeRoundAndPackFloat128(aSign ^ zSign, bExp - 4, aSig0, aSig1,
                                         status);
}

/*----------------------------------------------------------------------------
| Returns the square root of the quadruple-precision floating-point value `a'.
| The operation is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

float128 float128_sqrt(float128 a, float_status *status)
{
    flag aSign;
    int32_t aExp, zExp;
    uint64_t aSig0, aSig1, zSig0, zSig1, zSig2, doubleZSig0;
    uint64_t rem0, rem1, rem2, rem3, term0, term1, term2, term3;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if (aSig0 | aSig1) {
            return propagateFloat128NaN(a, a, status);
        }
        if ( ! aSign ) return a;
        goto invalid;
    }
    if ( aSign ) {
        if ( ( aExp | aSig0 | aSig1 ) == 0 ) return a;
 invalid:
        float_raise(float_flag_invalid, status);
        return float128_default_nan(status);
    }
    if ( aExp == 0 ) {
        if ( ( aSig0 | aSig1 ) == 0 ) return packFloat128( 0, 0, 0, 0 );
        normalizeFloat128Subnormal( aSig0, aSig1, &aExp, &aSig0, &aSig1 );
    }
    zExp = ( ( aExp - 0x3FFF )>>1 ) + 0x3FFE;
    aSig0 |= LIT64( 0x0001000000000000 );
    zSig0 = estimateSqrt32( aExp, aSig0>>17 );
    shortShift128Left( aSig0, aSig1, 13 - ( aExp & 1 ), &aSig0, &aSig1 );
    zSig0 = estimateDiv128To64( aSig0, aSig1, zSig0<<32 ) + ( zSig0<<30 );
    doubleZSig0 = zSig0<<1;
    mul64To128( zSig0, zSig0, &term0, &term1 );
    sub128( aSig0, aSig1, term0, term1, &rem0, &rem1 );
    while ( (int64_t) rem0 < 0 ) {
        --zSig0;
        doubleZSig0 -= 2;
        add128( rem0, rem1, zSig0>>63, doubleZSig0 | 1, &rem0, &rem1 );
    }
    zSig1 = estimateDiv128To64( rem1, 0, doubleZSig0 );
    if ( ( zSig1 & 0x1FFF ) <= 5 ) {
        if ( zSig1 == 0 ) zSig1 = 1;
        mul64To128( doubleZSig0, zSig1, &term1, &term2 );
        sub128( rem1, 0, term1, term2, &rem1, &rem2 );
        mul64To128( zSig1, zSig1, &term2, &term3 );
        sub192( rem1, rem2, 0, 0, term2, term3, &rem1, &rem2, &rem3 );
        while ( (int64_t) rem1 < 0 ) {
            --zSig1;
            shortShift128Left( 0, zSig1, 1, &term2, &term3 );
            term3 |= 1;
            term2 |= doubleZSig0;
            add192( rem1, rem2, rem3, 0, term2, term3, &rem1, &rem2, &rem3 );
        }
        zSig1 |= ( ( rem1 | rem2 | rem3 ) != 0 );
    }
    shift128ExtraRightJamming( zSig0, zSig1, 0, 14, &zSig0, &zSig1, &zSig2 );
    return roundAndPackFloat128(0, zExp, zSig0, zSig1, zSig2, status);

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  Otherwise, the comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_eq(float128 a, float128 b, float_status *status)
{

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint64_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  The invalid
| exception is raised if either operand is a NaN.  The comparison is performed
| according to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_le(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  The invalid exception is
| raised if either operand is a NaN.  The comparison is performed according
| to the IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_lt(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  The invalid exception is raised if either
| operand is a NaN. The comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_unordered(float128 a, float128 b, float_status *status)
{
    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        float_raise(float_flag_invalid, status);
        return 1;
    }
    return 0;
}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is equal to
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  The comparison is performed according to the IEC/IEEE Standard
| for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_eq_quiet(float128 a, float128 b, float_status *status)
{

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    return
           ( a.low == b.low )
        && (    ( a.high == b.high )
             || (    ( a.low == 0 )
                  && ( (uint64_t) ( ( a.high | b.high )<<1 ) == 0 ) )
           );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| or equal to the corresponding value `b', and 0 otherwise.  Quiet NaNs do not
| cause an exception.  Otherwise, the comparison is performed according to the
| IEC/IEEE Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_le_quiet(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            || (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 == 0 );
    }
    return
          aSign ? le128( b.high, b.low, a.high, a.low )
        : le128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point value `a' is less than
| the corresponding value `b', and 0 otherwise.  Quiet NaNs do not cause an
| exception.  Otherwise, the comparison is performed according to the IEC/IEEE
| Standard for Binary Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_lt_quiet(float128 a, float128 b, float_status *status)
{
    flag aSign, bSign;

    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 0;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        return
               aSign
            && (    ( ( (uint64_t) ( ( a.high | b.high )<<1 ) ) | a.low | b.low )
                 != 0 );
    }
    return
          aSign ? lt128( b.high, b.low, a.high, a.low )
        : lt128( a.high, a.low, b.high, b.low );

}

/*----------------------------------------------------------------------------
| Returns 1 if the quadruple-precision floating-point values `a' and `b' cannot
| be compared, and 0 otherwise.  Quiet NaNs do not cause an exception.  The
| comparison is performed according to the IEC/IEEE Standard for Binary
| Floating-Point Arithmetic.
*----------------------------------------------------------------------------*/

int float128_unordered_quiet(float128 a, float128 b, float_status *status)
{
    if (    (    ( extractFloat128Exp( a ) == 0x7FFF )
              && ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) )
         || (    ( extractFloat128Exp( b ) == 0x7FFF )
              && ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )
       ) {
        if (float128_is_signaling_nan(a, status)
         || float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return 1;
    }
    return 0;
}

/* misc functions */
float32 uint32_to_float32(uint32_t a, float_status *status)
{
    return int64_to_float32(a, status);
}

float64 uint32_to_float64(uint32_t a, float_status *status)
{
    return int64_to_float64(a, status);
}

uint32_t float32_to_uint32(float32 a, float_status *status)
{
    int64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float32_to_int64(a, status);
    if (v < 0) {
        res = 0;
    } else if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint32_t float32_to_uint32_round_to_zero(float32 a, float_status *status)
{
    int64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float32_to_int64_round_to_zero(a, status);
    if (v < 0) {
        res = 0;
    } else if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

int16_t float32_to_int16(float32 a, float_status *status)
{
    int32_t v;
    int16_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float32_to_int32(a, status);
    if (v < -0x8000) {
        res = -0x8000;
    } else if (v > 0x7fff) {
        res = 0x7fff;
    } else {
        return v;
    }

    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint16_t float32_to_uint16(float32 a, float_status *status)
{
    int32_t v;
    uint16_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float32_to_int32(a, status);
    if (v < 0) {
        res = 0;
    } else if (v > 0xffff) {
        res = 0xffff;
    } else {
        return v;
    }

    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint16_t float32_to_uint16_round_to_zero(float32 a, float_status *status)
{
    int64_t v;
    uint16_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float32_to_int64_round_to_zero(a, status);
    if (v < 0) {
        res = 0;
    } else if (v > 0xffff) {
        res = 0xffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint32_t float64_to_uint32(float64 a, float_status *status)
{
    uint64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float64_to_uint64(a, status);
    if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint32_t float64_to_uint32_round_to_zero(float64 a, float_status *status)
{
    uint64_t v;
    uint32_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float64_to_uint64_round_to_zero(a, status);
    if (v > 0xffffffff) {
        res = 0xffffffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

int16_t float64_to_int16(float64 a, float_status *status)
{
    int64_t v;
    int16_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float64_to_int32(a, status);
    if (v < -0x8000) {
        res = -0x8000;
    } else if (v > 0x7fff) {
        res = 0x7fff;
    } else {
        return v;
    }

    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint16_t float64_to_uint16(float64 a, float_status *status)
{
    int64_t v;
    uint16_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float64_to_int32(a, status);
    if (v < 0) {
        res = 0;
    } else if (v > 0xffff) {
        res = 0xffff;
    } else {
        return v;
    }

    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

uint16_t float64_to_uint16_round_to_zero(float64 a, float_status *status)
{
    int64_t v;
    uint16_t res;
    int old_exc_flags = get_float_exception_flags(status);

    v = float64_to_int64_round_to_zero(a, status);
    if (v < 0) {
        res = 0;
    } else if (v > 0xffff) {
        res = 0xffff;
    } else {
        return v;
    }
    set_float_exception_flags(old_exc_flags, status);
    float_raise(float_flag_invalid, status);
    return res;
}

/*----------------------------------------------------------------------------
| Returns the result of converting the double-precision floating-point value
| `a' to the 64-bit unsigned integer format.  The conversion is
| performed according to the IEC/IEEE Standard for Binary Floating-Point
| Arithmetic---which means in particular that the conversion is rounded
| according to the current rounding mode.  If `a' is a NaN, the largest
| positive integer is returned.  If the conversion overflows, the
| largest unsigned integer is returned.  If 'a' is negative, the value is
| rounded and zero is returned; negative values that do not round to zero
| will raise the inexact exception.
*----------------------------------------------------------------------------*/

uint64_t float64_to_uint64(float64 a, float_status *status)
{
    flag aSign;
    int aExp;
    int shiftCount;
    uint64_t aSig, aSigExtra;
    a = float64_squash_input_denormal(a, status);

    aSig = extractFloat64Frac(a);
    aExp = extractFloat64Exp(a);
    aSign = extractFloat64Sign(a);
    if (aSign && (aExp > 1022)) {
        float_raise(float_flag_invalid, status);
        if (float64_is_any_nan(a)) {
            return LIT64(0xFFFFFFFFFFFFFFFF);
        } else {
            return 0;
        }
    }
    if (aExp) {
        aSig |= LIT64(0x0010000000000000);
    }
    shiftCount = 0x433 - aExp;
    if (shiftCount <= 0) {
        if (0x43E < aExp) {
            float_raise(float_flag_invalid, status);
            return LIT64(0xFFFFFFFFFFFFFFFF);
        }
        aSigExtra = 0;
        aSig <<= -shiftCount;
    } else {
        shift64ExtraRightJamming(aSig, 0, shiftCount, &aSig, &aSigExtra);
    }
    return roundAndPackUint64(aSign, aSig, aSigExtra, status);
}

uint64_t float64_to_uint64_round_to_zero(float64 a, float_status *status)
{
    signed char current_rounding_mode = status->float_rounding_mode;
    set_float_rounding_mode(float_round_to_zero, status);
    uint64_t v = float64_to_uint64(a, status);
    set_float_rounding_mode(current_rounding_mode, status);
    return v;
}

#define COMPARE(s, nan_exp)                                                  \
static inline int float ## s ## _compare_internal(float ## s a, float ## s b,\
                                      int is_quiet, float_status *status)    \
{                                                                            \
    flag aSign, bSign;                                                       \
    uint ## s ## _t av, bv;                                                  \
    a = float ## s ## _squash_input_denormal(a, status);                     \
    b = float ## s ## _squash_input_denormal(b, status);                     \
                                                                             \
    if (( ( extractFloat ## s ## Exp( a ) == nan_exp ) &&                    \
         extractFloat ## s ## Frac( a ) ) ||                                 \
        ( ( extractFloat ## s ## Exp( b ) == nan_exp ) &&                    \
          extractFloat ## s ## Frac( b ) )) {                                \
        if (!is_quiet ||                                                     \
            float ## s ## _is_signaling_nan(a, status) ||                  \
            float ## s ## _is_signaling_nan(b, status)) {                 \
            float_raise(float_flag_invalid, status);                         \
        }                                                                    \
        return float_relation_unordered;                                     \
    }                                                                        \
    aSign = extractFloat ## s ## Sign( a );                                  \
    bSign = extractFloat ## s ## Sign( b );                                  \
    av = float ## s ## _val(a);                                              \
    bv = float ## s ## _val(b);                                              \
    if ( aSign != bSign ) {                                                  \
        if ( (uint ## s ## _t) ( ( av | bv )<<1 ) == 0 ) {                   \
            /* zero case */                                                  \
            return float_relation_equal;                                     \
        } else {                                                             \
            return 1 - (2 * aSign);                                          \
        }                                                                    \
    } else {                                                                 \
        if (av == bv) {                                                      \
            return float_relation_equal;                                     \
        } else {                                                             \
            return 1 - 2 * (aSign ^ ( av < bv ));                            \
        }                                                                    \
    }                                                                        \
}                                                                            \
                                                                             \
int float ## s ## _compare(float ## s a, float ## s b, float_status *status) \
{                                                                            \
    return float ## s ## _compare_internal(a, b, 0, status);                 \
}                                                                            \
                                                                             \
int float ## s ## _compare_quiet(float ## s a, float ## s b,                 \
                                 float_status *status)                       \
{                                                                            \
    return float ## s ## _compare_internal(a, b, 1, status);                 \
}

COMPARE(32, 0xff)
COMPARE(64, 0x7ff)

static inline int floatx80_compare_internal(floatx80 a, floatx80 b,
                                            int is_quiet, float_status *status)
{
    flag aSign, bSign;

    if (floatx80_invalid_encoding(a) || floatx80_invalid_encoding(b)) {
        float_raise(float_flag_invalid, status);
        return float_relation_unordered;
    }
    if (( ( extractFloatx80Exp( a ) == 0x7fff ) &&
          ( extractFloatx80Frac( a )<<1 ) ) ||
        ( ( extractFloatx80Exp( b ) == 0x7fff ) &&
          ( extractFloatx80Frac( b )<<1 ) )) {
        if (!is_quiet ||
            floatx80_is_signaling_nan(a, status) ||
            floatx80_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return float_relation_unordered;
    }
    aSign = extractFloatx80Sign( a );
    bSign = extractFloatx80Sign( b );
    if ( aSign != bSign ) {

        if ( ( ( (uint16_t) ( ( a.high | b.high ) << 1 ) ) == 0) &&
             ( ( a.low | b.low ) == 0 ) ) {
            /* zero case */
            return float_relation_equal;
        } else {
            return 1 - (2 * aSign);
        }
    } else {
        if (a.low == b.low && a.high == b.high) {
            return float_relation_equal;
        } else {
            return 1 - 2 * (aSign ^ ( lt128( a.high, a.low, b.high, b.low ) ));
        }
    }
}

int floatx80_compare(floatx80 a, floatx80 b, float_status *status)
{
    return floatx80_compare_internal(a, b, 0, status);
}

int floatx80_compare_quiet(floatx80 a, floatx80 b, float_status *status)
{
    return floatx80_compare_internal(a, b, 1, status);
}

static inline int float128_compare_internal(float128 a, float128 b,
                                            int is_quiet, float_status *status)
{
    flag aSign, bSign;

    if (( ( extractFloat128Exp( a ) == 0x7fff ) &&
          ( extractFloat128Frac0( a ) | extractFloat128Frac1( a ) ) ) ||
        ( ( extractFloat128Exp( b ) == 0x7fff ) &&
          ( extractFloat128Frac0( b ) | extractFloat128Frac1( b ) ) )) {
        if (!is_quiet ||
            float128_is_signaling_nan(a, status) ||
            float128_is_signaling_nan(b, status)) {
            float_raise(float_flag_invalid, status);
        }
        return float_relation_unordered;
    }
    aSign = extractFloat128Sign( a );
    bSign = extractFloat128Sign( b );
    if ( aSign != bSign ) {
        if ( ( ( ( a.high | b.high )<<1 ) | a.low | b.low ) == 0 ) {
            /* zero case */
            return float_relation_equal;
        } else {
            return 1 - (2 * aSign);
        }
    } else {
        if (a.low == b.low && a.high == b.high) {
            return float_relation_equal;
        } else {
            return 1 - 2 * (aSign ^ ( lt128( a.high, a.low, b.high, b.low ) ));
        }
    }
}

int float128_compare(float128 a, float128 b, float_status *status)
{
    return float128_compare_internal(a, b, 0, status);
}

int float128_compare_quiet(float128 a, float128 b, float_status *status)
{
    return float128_compare_internal(a, b, 1, status);
}

/* min() and max() functions. These can't be implemented as
 * 'compare and pick one input' because that would mishandle
 * NaNs and +0 vs -0.
 *
 * minnum() and maxnum() functions. These are similar to the min()
 * and max() functions but if one of the arguments is a QNaN and
 * the other is numerical then the numerical argument is returned.
 * minnum() and maxnum correspond to the IEEE 754-2008 minNum()
 * and maxNum() operations. min() and max() are the typical min/max
 * semantics provided by many CPUs which predate that specification.
 *
 * minnummag() and maxnummag() functions correspond to minNumMag()
 * and minNumMag() from the IEEE-754 2008.
 */
#define MINMAX(s)                                                       \
static inline float ## s float ## s ## _minmax(float ## s a, float ## s b,     \
                                               int ismin, int isieee,   \
                                               int ismag,               \
                                               float_status *status)    \
{                                                                       \
    flag aSign, bSign;                                                  \
    uint ## s ## _t av, bv, aav, abv;                                   \
    a = float ## s ## _squash_input_denormal(a, status);                \
    b = float ## s ## _squash_input_denormal(b, status);                \
    if (float ## s ## _is_any_nan(a) ||                                 \
        float ## s ## _is_any_nan(b)) {                                 \
        if (isieee) {                                                   \
            if (float ## s ## _is_quiet_nan(a, status) &&               \
                !float ## s ##_is_any_nan(b)) {                         \
                return b;                                               \
            } else if (float ## s ## _is_quiet_nan(b, status) &&        \
                       !float ## s ## _is_any_nan(a)) {                \
                return a;                                               \
            }                                                           \
        }                                                               \
        return propagateFloat ## s ## NaN(a, b, status);                \
    }                                                                   \
    aSign = extractFloat ## s ## Sign(a);                               \
    bSign = extractFloat ## s ## Sign(b);                               \
    av = float ## s ## _val(a);                                         \
    bv = float ## s ## _val(b);                                         \
    if (ismag) {                                                        \
        aav = float ## s ## _abs(av);                                   \
        abv = float ## s ## _abs(bv);                                   \
        if (aav != abv) {                                               \
            if (ismin) {                                                \
                return (aav < abv) ? a : b;                             \
            } else {                                                    \
                return (aav < abv) ? b : a;                             \
            }                                                           \
        }                                                               \
    }                                                                   \
    if (aSign != bSign) {                                               \
        if (ismin) {                                                    \
            return aSign ? a : b;                                       \
        } else {                                                        \
            return aSign ? b : a;                                       \
        }                                                               \
    } else {                                                            \
        if (ismin) {                                                    \
            return (aSign ^ (av < bv)) ? a : b;                         \
        } else {                                                        \
            return (aSign ^ (av < bv)) ? b : a;                         \
        }                                                               \
    }                                                                   \
}                                                                       \
                                                                        \
float ## s float ## s ## _min(float ## s a, float ## s b,               \
                              float_status *status)                     \
{                                                                       \
    return float ## s ## _minmax(a, b, 1, 0, 0, status);                \
}                                                                       \
                                                                        \
float ## s float ## s ## _max(float ## s a, float ## s b,               \
                              float_status *status)                     \
{                                                                       \
    return float ## s ## _minmax(a, b, 0, 0, 0, status);                \
}                                                                       \
                                                                        \
float ## s float ## s ## _minnum(float ## s a, float ## s b,            \
                                 float_status *status)                  \
{                                                                       \
    return float ## s ## _minmax(a, b, 1, 1, 0, status);                \
}                                                                       \
                                                                        \
float ## s float ## s ## _maxnum(float ## s a, float ## s b,            \
                                 float_status *status)                  \
{                                                                       \
    return float ## s ## _minmax(a, b, 0, 1, 0, status);                \
}                                                                       \
                                                                        \
float ## s float ## s ## _minnummag(float ## s a, float ## s b,         \
                                    float_status *status)               \
{                                                                       \
    return float ## s ## _minmax(a, b, 1, 1, 1, status);                \
}                                                                       \
                                                                        \
float ## s float ## s ## _maxnummag(float ## s a, float ## s b,         \
                                    float_status *status)               \
{                                                                       \
    return float ## s ## _minmax(a, b, 0, 1, 1, status);                \
}

MINMAX(32)
MINMAX(64)


/* Multiply A by 2 raised to the power N.  */
float32 float32_scalbn(float32 a, int n, float_status *status)
{
    flag aSign;
    int16_t aExp;
    uint32_t aSig;

    a = float32_squash_input_denormal(a, status);
    aSig = extractFloat32Frac( a );
    aExp = extractFloat32Exp( a );
    aSign = extractFloat32Sign( a );

    if ( aExp == 0xFF ) {
        if ( aSig ) {
            return propagateFloat32NaN(a, a, status);
        }
        return a;
    }
    if (aExp != 0) {
        aSig |= 0x00800000;
    } else if (aSig == 0) {
        return a;
    } else {
        aExp++;
    }

    if (n > 0x200) {
        n = 0x200;
    } else if (n < -0x200) {
        n = -0x200;
    }

    aExp += n - 1;
    aSig <<= 7;
    return normalizeRoundAndPackFloat32(aSign, aExp, aSig, status);
}

float64 float64_scalbn(float64 a, int n, float_status *status)
{
    flag aSign;
    int16_t aExp;
    uint64_t aSig;

    a = float64_squash_input_denormal(a, status);
    aSig = extractFloat64Frac( a );
    aExp = extractFloat64Exp( a );
    aSign = extractFloat64Sign( a );

    if ( aExp == 0x7FF ) {
        if ( aSig ) {
            return propagateFloat64NaN(a, a, status);
        }
        return a;
    }
    if (aExp != 0) {
        aSig |= LIT64( 0x0010000000000000 );
    } else if (aSig == 0) {
        return a;
    } else {
        aExp++;
    }

    if (n > 0x1000) {
        n = 0x1000;
    } else if (n < -0x1000) {
        n = -0x1000;
    }

    aExp += n - 1;
    aSig <<= 10;
    return normalizeRoundAndPackFloat64(aSign, aExp, aSig, status);
}

floatx80 floatx80_scalbn(floatx80 a, int n, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig;

    if (floatx80_invalid_encoding(a)) {
        float_raise(float_flag_invalid, status);
        return floatx80_default_nan(status);
    }
    aSig = extractFloatx80Frac( a );
    aExp = extractFloatx80Exp( a );
    aSign = extractFloatx80Sign( a );

    if ( aExp == 0x7FFF ) {
        if ( aSig<<1 ) {
            return propagateFloatx80NaN(a, a, status);
        }
        return a;
    }

    if (aExp == 0) {
        if (aSig == 0) {
            return a;
        }
        aExp++;
    }

    if (n > 0x10000) {
        n = 0x10000;
    } else if (n < -0x10000) {
        n = -0x10000;
    }

    aExp += n;
    return normalizeRoundAndPackFloatx80(status->floatx80_rounding_precision,
                                         aSign, aExp, aSig, 0, status);
}

float128 float128_scalbn(float128 a, int n, float_status *status)
{
    flag aSign;
    int32_t aExp;
    uint64_t aSig0, aSig1;

    aSig1 = extractFloat128Frac1( a );
    aSig0 = extractFloat128Frac0( a );
    aExp = extractFloat128Exp( a );
    aSign = extractFloat128Sign( a );
    if ( aExp == 0x7FFF ) {
        if ( aSig0 | aSig1 ) {
            return propagateFloat128NaN(a, a, status);
        }
        return a;
    }
    if (aExp != 0) {
        aSig0 |= LIT64( 0x0001000000000000 );
    } else if (aSig0 == 0 && aSig1 == 0) {
        return a;
    } else {
        aExp++;
    }

    if (n > 0x10000) {
        n = 0x10000;
    } else if (n < -0x10000) {
        n = -0x10000;
    }

    aExp += n - 1;
    return normalizeRoundAndPackFloat128( aSign, aExp, aSig0, aSig1
                                         , status);

}
