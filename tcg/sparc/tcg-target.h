/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#define TCG_TARGET_SPARC 1

#define TCG_TARGET_WORDS_BIGENDIAN

#define TCG_TARGET_NB_REGS 32

typedef enum {
    TCG_REG_G0 = 0,
    TCG_REG_G1,
    TCG_REG_G2,
    TCG_REG_G3,
    TCG_REG_G4,
    TCG_REG_G5,
    TCG_REG_G6,
    TCG_REG_G7,
    TCG_REG_O0,
    TCG_REG_O1,
    TCG_REG_O2,
    TCG_REG_O3,
    TCG_REG_O4,
    TCG_REG_O5,
    TCG_REG_O6,
    TCG_REG_O7,
    TCG_REG_L0,
    TCG_REG_L1,
    TCG_REG_L2,
    TCG_REG_L3,
    TCG_REG_L4,
    TCG_REG_L5,
    TCG_REG_L6,
    TCG_REG_L7,
    TCG_REG_I0,
    TCG_REG_I1,
    TCG_REG_I2,
    TCG_REG_I3,
    TCG_REG_I4,
    TCG_REG_I5,
    TCG_REG_I6,
    TCG_REG_I7,
} TCGReg;

#define TCG_CT_CONST_S11  0x100
#define TCG_CT_CONST_S13  0x200
#define TCG_CT_CONST_ZERO 0x400

/* used for function call generation */
#define TCG_REG_CALL_STACK TCG_REG_O6

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_STACK_BIAS           2047
#define TCG_TARGET_STACK_ALIGN          16
#define TCG_TARGET_CALL_STACK_OFFSET    (128 + 6*8 + TCG_TARGET_STACK_BIAS)
#else
#define TCG_TARGET_STACK_BIAS           0
#define TCG_TARGET_STACK_ALIGN          8
#define TCG_TARGET_CALL_STACK_OFFSET    (64 + 4 + 6*4)
#endif

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_EXTEND_ARGS 1
#endif

/* optional instructions */
#define TCG_TARGET_HAS_div_i32		1
#define TCG_TARGET_HAS_rot_i32          0
#define TCG_TARGET_HAS_ext8s_i32        0
#define TCG_TARGET_HAS_ext16s_i32       0
#define TCG_TARGET_HAS_ext8u_i32        0
#define TCG_TARGET_HAS_ext16u_i32       0
#define TCG_TARGET_HAS_bswap16_i32      0
#define TCG_TARGET_HAS_bswap32_i32      0
#define TCG_TARGET_HAS_neg_i32          1
#define TCG_TARGET_HAS_not_i32          1
#define TCG_TARGET_HAS_andc_i32         1
#define TCG_TARGET_HAS_orc_i32          1
#define TCG_TARGET_HAS_eqv_i32          0
#define TCG_TARGET_HAS_nand_i32         0
#define TCG_TARGET_HAS_nor_i32          0
#define TCG_TARGET_HAS_deposit_i32      0
#define TCG_TARGET_HAS_movcond_i32      1

#if TCG_TARGET_REG_BITS == 64
#define TCG_TARGET_HAS_div_i64          1
#define TCG_TARGET_HAS_rot_i64          0
#define TCG_TARGET_HAS_ext8s_i64        0
#define TCG_TARGET_HAS_ext16s_i64       0
#define TCG_TARGET_HAS_ext32s_i64       1
#define TCG_TARGET_HAS_ext8u_i64        0
#define TCG_TARGET_HAS_ext16u_i64       0
#define TCG_TARGET_HAS_ext32u_i64       1
#define TCG_TARGET_HAS_bswap16_i64      0
#define TCG_TARGET_HAS_bswap32_i64      0
#define TCG_TARGET_HAS_bswap64_i64      0
#define TCG_TARGET_HAS_neg_i64          1
#define TCG_TARGET_HAS_not_i64          1
#define TCG_TARGET_HAS_andc_i64         1
#define TCG_TARGET_HAS_orc_i64          1
#define TCG_TARGET_HAS_eqv_i64          0
#define TCG_TARGET_HAS_nand_i64         0
#define TCG_TARGET_HAS_nor_i64          0
#define TCG_TARGET_HAS_deposit_i64      0
#define TCG_TARGET_HAS_movcond_i64      1
#endif

#define TCG_AREG0 TCG_REG_I0

static inline void flush_icache_range(tcg_target_ulong start,
                                      tcg_target_ulong stop)
{
    unsigned long p;

    p = start & ~(8UL - 1UL);
    stop = (stop + (8UL - 1UL)) & ~(8UL - 1UL);

    for (; p < stop; p += 8)
        __asm__ __volatile__("flush\t%0" : : "r" (p));
}
