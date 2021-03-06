/*
 * Optimizations for Tiny Code Generator for QEMU
 *
 * Copyright (c) 2010 Samsung Electronics.
 * Contributed by Kirill Batuzov <batuzovk@ispras.ru>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include "qemu-common.h"
#include "tcg-op.h"

#define CASE_OP_32_64(x)                        \
        glue(glue(case INDEX_op_, x), _i32):    \
        glue(glue(case INDEX_op_, x), _i64)

typedef enum {
    TCG_TEMP_UNDEF = 0,
    TCG_TEMP_CONST,
    TCG_TEMP_COPY,
} tcg_temp_state;

struct tcg_temp_info {
    tcg_temp_state state;
    uint16_t prev_copy;
    uint16_t next_copy;
    tcg_target_ulong val;
};

static struct tcg_temp_info temps[TCG_MAX_TEMPS];

/* Reset TEMP's state to TCG_TEMP_UNDEF.  If TEMP only had one copy, remove
   the copy flag from the left temp.  */
static void reset_temp(TCGArg temp)
{
    if (temps[temp].state == TCG_TEMP_COPY) {
        if (temps[temp].prev_copy == temps[temp].next_copy) {
            temps[temps[temp].next_copy].state = TCG_TEMP_UNDEF;
        } else {
            temps[temps[temp].next_copy].prev_copy = temps[temp].prev_copy;
            temps[temps[temp].prev_copy].next_copy = temps[temp].next_copy;
        }
    }
    temps[temp].state = TCG_TEMP_UNDEF;
}

static int op_bits(TCGOpcode op)
{
    const TCGOpDef *def = &tcg_op_defs[op];
    return def->flags & TCG_OPF_64BIT ? 64 : 32;
}

static TCGOpcode op_to_movi(TCGOpcode op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_movi_i32;
    case 64:
        return INDEX_op_movi_i64;
    default:
        fprintf(stderr, "op_to_movi: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static TCGArg find_better_copy(TCGContext *s, TCGArg temp)
{
    TCGArg i;

    /* If this is already a global, we can't do better. */
    if (temp < s->nb_globals) {
        return temp;
    }

    /* Search for a global first. */
    for (i = temps[temp].next_copy ; i != temp ; i = temps[i].next_copy) {
        if (i < s->nb_globals) {
            return i;
        }
    }

    /* If it is a temp, search for a temp local. */
    if (!s->temps[temp].temp_local) {
        for (i = temps[temp].next_copy ; i != temp ; i = temps[i].next_copy) {
            if (s->temps[i].temp_local) {
                return i;
            }
        }
    }

    /* Failure to find a better representation, return the same temp. */
    return temp;
}

static bool temps_are_copies(TCGArg arg1, TCGArg arg2)
{
    TCGArg i;

    if (arg1 == arg2) {
        return true;
    }

    if (temps[arg1].state != TCG_TEMP_COPY
        || temps[arg2].state != TCG_TEMP_COPY) {
        return false;
    }

    for (i = temps[arg1].next_copy ; i != arg1 ; i = temps[i].next_copy) {
        if (i == arg2) {
            return true;
        }
    }

    return false;
}

static void tcg_opt_gen_mov(TCGContext *s, TCGArg *gen_args,
                            TCGArg dst, TCGArg src)
{
        reset_temp(dst);
        assert(temps[src].state != TCG_TEMP_CONST);

        if (s->temps[src].type == s->temps[dst].type) {
            if (temps[src].state != TCG_TEMP_COPY) {
                temps[src].state = TCG_TEMP_COPY;
                temps[src].next_copy = src;
                temps[src].prev_copy = src;
            }
            temps[dst].state = TCG_TEMP_COPY;
            temps[dst].next_copy = temps[src].next_copy;
            temps[dst].prev_copy = src;
            temps[temps[dst].next_copy].prev_copy = dst;
            temps[src].next_copy = dst;
        }

        gen_args[0] = dst;
        gen_args[1] = src;
}

static void tcg_opt_gen_movi(TCGArg *gen_args, TCGArg dst, TCGArg val)
{
        reset_temp(dst);
        temps[dst].state = TCG_TEMP_CONST;
        temps[dst].val = val;
        gen_args[0] = dst;
        gen_args[1] = val;
}

static TCGOpcode op_to_mov(TCGOpcode op)
{
    switch (op_bits(op)) {
    case 32:
        return INDEX_op_mov_i32;
    case 64:
        return INDEX_op_mov_i64;
    default:
        fprintf(stderr, "op_to_mov: unexpected return value of "
                "function op_bits.\n");
        tcg_abort();
    }
}

static TCGArg do_constant_folding_2(TCGOpcode op, TCGArg x, TCGArg y)
{
    switch (op) {
    CASE_OP_32_64(add):
        return x + y;

    CASE_OP_32_64(sub):
        return x - y;

    CASE_OP_32_64(mul):
        return x * y;

    CASE_OP_32_64(and):
        return x & y;

    CASE_OP_32_64(or):
        return x | y;

    CASE_OP_32_64(xor):
        return x ^ y;

    case INDEX_op_shl_i32:
        return (uint32_t)x << (uint32_t)y;

    case INDEX_op_shl_i64:
        return (uint64_t)x << (uint64_t)y;

    case INDEX_op_shr_i32:
        return (uint32_t)x >> (uint32_t)y;

    case INDEX_op_shr_i64:
        return (uint64_t)x >> (uint64_t)y;

    case INDEX_op_sar_i32:
        return (int32_t)x >> (int32_t)y;

    case INDEX_op_sar_i64:
        return (int64_t)x >> (int64_t)y;

    case INDEX_op_rotr_i32:
        x = ((uint32_t)x << (32 - y)) | ((uint32_t)x >> y);
        return x;

    case INDEX_op_rotr_i64:
        x = ((uint64_t)x << (64 - y)) | ((uint64_t)x >> y);
        return x;

    case INDEX_op_rotl_i32:
        x = ((uint32_t)x << y) | ((uint32_t)x >> (32 - y));
        return x;

    case INDEX_op_rotl_i64:
        x = ((uint64_t)x << y) | ((uint64_t)x >> (64 - y));
        return x;

    CASE_OP_32_64(not):
        return ~x;

    CASE_OP_32_64(neg):
        return -x;

    CASE_OP_32_64(andc):
        return x & ~y;

    CASE_OP_32_64(orc):
        return x | ~y;

    CASE_OP_32_64(eqv):
        return ~(x ^ y);

    CASE_OP_32_64(nand):
        return ~(x & y);

    CASE_OP_32_64(nor):
        return ~(x | y);

    CASE_OP_32_64(ext8s):
        return (int8_t)x;

    CASE_OP_32_64(ext16s):
        return (int16_t)x;

    CASE_OP_32_64(ext8u):
        return (uint8_t)x;

    CASE_OP_32_64(ext16u):
        return (uint16_t)x;

    case INDEX_op_ext32s_i64:
        return (int32_t)x;

    case INDEX_op_ext32u_i64:
        return (uint32_t)x;

    default:
        fprintf(stderr,
                "Unrecognized operation %d in do_constant_folding.\n", op);
        tcg_abort();
    }
}

static TCGArg do_constant_folding(TCGOpcode op, TCGArg x, TCGArg y)
{
    TCGArg res = do_constant_folding_2(op, x, y);
    if (op_bits(op) == 32) {
        res &= 0xffffffff;
    }
    return res;
}

static bool do_constant_folding_cond_32(uint32_t x, uint32_t y, TCGCond c)
{
    switch (c) {
    case TCG_COND_EQ:
        return x == y;
    case TCG_COND_NE:
        return x != y;
    case TCG_COND_LT:
        return (int32_t)x < (int32_t)y;
    case TCG_COND_GE:
        return (int32_t)x >= (int32_t)y;
    case TCG_COND_LE:
        return (int32_t)x <= (int32_t)y;
    case TCG_COND_GT:
        return (int32_t)x > (int32_t)y;
    case TCG_COND_LTU:
        return x < y;
    case TCG_COND_GEU:
        return x >= y;
    case TCG_COND_LEU:
        return x <= y;
    case TCG_COND_GTU:
        return x > y;
    default:
        tcg_abort();
    }
}

static bool do_constant_folding_cond_64(uint64_t x, uint64_t y, TCGCond c)
{
    switch (c) {
    case TCG_COND_EQ:
        return x == y;
    case TCG_COND_NE:
        return x != y;
    case TCG_COND_LT:
        return (int64_t)x < (int64_t)y;
    case TCG_COND_GE:
        return (int64_t)x >= (int64_t)y;
    case TCG_COND_LE:
        return (int64_t)x <= (int64_t)y;
    case TCG_COND_GT:
        return (int64_t)x > (int64_t)y;
    case TCG_COND_LTU:
        return x < y;
    case TCG_COND_GEU:
        return x >= y;
    case TCG_COND_LEU:
        return x <= y;
    case TCG_COND_GTU:
        return x > y;
    default:
        tcg_abort();
    }
}

static bool do_constant_folding_cond_eq(TCGCond c)
{
    switch (c) {
    case TCG_COND_GT:
    case TCG_COND_LTU:
    case TCG_COND_LT:
    case TCG_COND_GTU:
    case TCG_COND_NE:
        return 0;
    case TCG_COND_GE:
    case TCG_COND_GEU:
    case TCG_COND_LE:
    case TCG_COND_LEU:
    case TCG_COND_EQ:
        return 1;
    default:
        tcg_abort();
    }
}

/* Return 2 if the condition can't be simplified, and the result
   of the condition (0 or 1) if it can */
static TCGArg do_constant_folding_cond(TCGOpcode op, TCGArg x,
                                       TCGArg y, TCGCond c)
{
    if (temps[x].state == TCG_TEMP_CONST && temps[y].state == TCG_TEMP_CONST) {
        switch (op_bits(op)) {
        case 32:
            return do_constant_folding_cond_32(temps[x].val, temps[y].val, c);
        case 64:
            return do_constant_folding_cond_64(temps[x].val, temps[y].val, c);
        default:
            tcg_abort();
        }
    } else if (temps_are_copies(x, y)) {
        return do_constant_folding_cond_eq(c);
    } else if (temps[y].state == TCG_TEMP_CONST && temps[y].val == 0) {
        switch (c) {
        case TCG_COND_LTU:
            return 0;
        case TCG_COND_GEU:
            return 1;
        default:
            return 2;
        }
    } else {
        return 2;
    }
}

/* Return 2 if the condition can't be simplified, and the result
   of the condition (0 or 1) if it can */
static TCGArg do_constant_folding_cond2(TCGArg *p1, TCGArg *p2, TCGCond c)
{
    TCGArg al = p1[0], ah = p1[1];
    TCGArg bl = p2[0], bh = p2[1];

    if (temps[bl].state == TCG_TEMP_CONST
        && temps[bh].state == TCG_TEMP_CONST) {
        uint64_t b = ((uint64_t)temps[bh].val << 32) | (uint32_t)temps[bl].val;

        if (temps[al].state == TCG_TEMP_CONST
            && temps[ah].state == TCG_TEMP_CONST) {
            uint64_t a;
            a = ((uint64_t)temps[ah].val << 32) | (uint32_t)temps[al].val;
            return do_constant_folding_cond_64(a, b, c);
        }
        if (b == 0) {
            switch (c) {
            case TCG_COND_LTU:
                return 0;
            case TCG_COND_GEU:
                return 1;
            default:
                break;
            }
        }
    }
    if (temps_are_copies(al, bl) && temps_are_copies(ah, bh)) {
        return do_constant_folding_cond_eq(c);
    }
    return 2;
}

static bool swap_commutative(TCGArg dest, TCGArg *p1, TCGArg *p2)
{
    TCGArg a1 = *p1, a2 = *p2;
    int sum = 0;
    sum += temps[a1].state == TCG_TEMP_CONST;
    sum -= temps[a2].state == TCG_TEMP_CONST;

    /* Prefer the constant in second argument, and then the form
       op a, a, b, which is better handled on non-RISC hosts. */
    if (sum > 0 || (sum == 0 && dest == a2)) {
        *p1 = a2;
        *p2 = a1;
        return true;
    }
    return false;
}

static bool swap_commutative2(TCGArg *p1, TCGArg *p2)
{
    int sum = 0;
    sum += temps[p1[0]].state == TCG_TEMP_CONST;
    sum += temps[p1[1]].state == TCG_TEMP_CONST;
    sum -= temps[p2[0]].state == TCG_TEMP_CONST;
    sum -= temps[p2[1]].state == TCG_TEMP_CONST;
    if (sum > 0) {
        TCGArg t;
        t = p1[0], p1[0] = p2[0], p2[0] = t;
        t = p1[1], p1[1] = p2[1], p2[1] = t;
        return true;
    }
    return false;
}

/* Propagate constants and copies, fold constant expressions. */
static TCGArg *tcg_constant_folding(TCGContext *s, uint16_t *tcg_opc_ptr,
                                    TCGArg *args, TCGOpDef *tcg_op_defs)
{
    int i, nb_ops, op_index, nb_temps, nb_globals, nb_call_args;
    TCGOpcode op;
    const TCGOpDef *def;
    TCGArg *gen_args;
    TCGArg tmp;

    /* Array VALS has an element for each temp.
       If this temp holds a constant then its value is kept in VALS' element.
       If this temp is a copy of other ones then the other copies are
       available through the doubly linked circular list. */

    nb_temps = s->nb_temps;
    nb_globals = s->nb_globals;
    memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));

    nb_ops = tcg_opc_ptr - gen_opc_buf;
    gen_args = args;
    for (op_index = 0; op_index < nb_ops; op_index++) {
        op = gen_opc_buf[op_index];
        def = &tcg_op_defs[op];
        /* Do copy propagation */
        if (op == INDEX_op_call) {
            int nb_oargs = args[0] >> 16;
            int nb_iargs = args[0] & 0xffff;
            for (i = nb_oargs + 1; i < nb_oargs + nb_iargs + 1; i++) {
                if (temps[args[i]].state == TCG_TEMP_COPY) {
                    args[i] = find_better_copy(s, args[i]);
                }
            }
        } else {
            for (i = def->nb_oargs; i < def->nb_oargs + def->nb_iargs; i++) {
                if (temps[args[i]].state == TCG_TEMP_COPY) {
                    args[i] = find_better_copy(s, args[i]);
                }
            }
        }

        /* For commutative operations make constant second argument */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(and):
        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
            swap_commutative(args[0], &args[1], &args[2]);
            break;
        CASE_OP_32_64(brcond):
            if (swap_commutative(-1, &args[0], &args[1])) {
                args[2] = tcg_swap_cond(args[2]);
            }
            break;
        CASE_OP_32_64(setcond):
            if (swap_commutative(args[0], &args[1], &args[2])) {
                args[3] = tcg_swap_cond(args[3]);
            }
            break;
        CASE_OP_32_64(movcond):
            if (swap_commutative(-1, &args[1], &args[2])) {
                args[5] = tcg_swap_cond(args[5]);
            }
            /* For movcond, we canonicalize the "false" input reg to match
               the destination reg so that the tcg backend can implement
               a "move if true" operation.  */
            if (swap_commutative(args[0], &args[4], &args[3])) {
                args[5] = tcg_invert_cond(args[5]);
            }
            break;
        case INDEX_op_add2_i32:
            swap_commutative(args[0], &args[2], &args[4]);
            swap_commutative(args[1], &args[3], &args[5]);
            break;
        case INDEX_op_mulu2_i32:
            swap_commutative(args[0], &args[2], &args[3]);
            break;
        case INDEX_op_brcond2_i32:
            if (swap_commutative2(&args[0], &args[2])) {
                args[4] = tcg_swap_cond(args[4]);
            }
            break;
        case INDEX_op_setcond2_i32:
            if (swap_commutative2(&args[1], &args[3])) {
                args[5] = tcg_swap_cond(args[5]);
            }
            break;
        default:
            break;
        }

        /* Simplify expressions for "shift/rot r, 0, a => movi r, 0" */
        switch (op) {
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[1]].val == 0) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tcg_opt_gen_movi(gen_args, args[0], 0);
                args += 3;
                gen_args += 2;
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, 0 => mov r, a" cases */
        switch (op) {
        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(or):
        CASE_OP_32_64(xor):
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                /* Proceed with possible constant folding. */
                break;
            }
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0) {
                if (temps_are_copies(args[0], args[1])) {
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(s, gen_args, args[0], args[1]);
                    gen_args += 2;
                }
                args += 3;
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, 0 => movi r, 0" cases */
        switch (op) {
        CASE_OP_32_64(and):
        CASE_OP_32_64(mul):
            if ((temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[2]].val == 0)) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tcg_opt_gen_movi(gen_args, args[0], 0);
                args += 3;
                gen_args += 2;
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, a => mov r, a" cases */
        switch (op) {
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
            if (temps_are_copies(args[1], args[2])) {
                if (temps_are_copies(args[0], args[1])) {
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(s, gen_args, args[0], args[1]);
                    gen_args += 2;
                }
                args += 3;
                continue;
            }
            break;
        default:
            break;
        }

        /* Simplify expression for "op r, a, a => movi r, 0" cases */
        switch (op) {
        CASE_OP_32_64(sub):
        CASE_OP_32_64(xor):
            if (temps_are_copies(args[1], args[2])) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tcg_opt_gen_movi(gen_args, args[0], 0);
                gen_args += 2;
                args += 3;
                continue;
            }
            break;
        default:
            break;
        }

        /* Propagate constants through copy operations and do constant
           folding.  Constants will be substituted to arguments by register
           allocator where needed and possible.  Also detect copies. */
        switch (op) {
        CASE_OP_32_64(mov):
            if (temps_are_copies(args[0], args[1])) {
                args += 2;
                gen_opc_buf[op_index] = INDEX_op_nop;
                break;
            }
            if (temps[args[1]].state != TCG_TEMP_CONST) {
                tcg_opt_gen_mov(s, gen_args, args[0], args[1]);
                gen_args += 2;
                args += 2;
                break;
            }
            /* Source argument is constant.  Rewrite the operation and
               let movi case handle it. */
            op = op_to_movi(op);
            gen_opc_buf[op_index] = op;
            args[1] = temps[args[1]].val;
            /* fallthrough */
        CASE_OP_32_64(movi):
            tcg_opt_gen_movi(gen_args, args[0], args[1]);
            gen_args += 2;
            args += 2;
            break;

        CASE_OP_32_64(not):
        CASE_OP_32_64(neg):
        CASE_OP_32_64(ext8s):
        CASE_OP_32_64(ext8u):
        CASE_OP_32_64(ext16s):
        CASE_OP_32_64(ext16u):
        case INDEX_op_ext32s_i64:
        case INDEX_op_ext32u_i64:
            if (temps[args[1]].state == TCG_TEMP_CONST) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tmp = do_constant_folding(op, temps[args[1]].val, 0);
                tcg_opt_gen_movi(gen_args, args[0], tmp);
                gen_args += 2;
                args += 2;
                break;
            }
            goto do_default;

        CASE_OP_32_64(add):
        CASE_OP_32_64(sub):
        CASE_OP_32_64(mul):
        CASE_OP_32_64(or):
        CASE_OP_32_64(and):
        CASE_OP_32_64(xor):
        CASE_OP_32_64(shl):
        CASE_OP_32_64(shr):
        CASE_OP_32_64(sar):
        CASE_OP_32_64(rotl):
        CASE_OP_32_64(rotr):
        CASE_OP_32_64(andc):
        CASE_OP_32_64(orc):
        CASE_OP_32_64(eqv):
        CASE_OP_32_64(nand):
        CASE_OP_32_64(nor):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tmp = do_constant_folding(op, temps[args[1]].val,
                                          temps[args[2]].val);
                tcg_opt_gen_movi(gen_args, args[0], tmp);
                gen_args += 2;
                args += 3;
                break;
            }
            goto do_default;

        CASE_OP_32_64(deposit):
            if (temps[args[1]].state == TCG_TEMP_CONST
                && temps[args[2]].state == TCG_TEMP_CONST) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tmp = ((1ull << args[4]) - 1);
                tmp = (temps[args[1]].val & ~(tmp << args[3]))
                      | ((temps[args[2]].val & tmp) << args[3]);
                tcg_opt_gen_movi(gen_args, args[0], tmp);
                gen_args += 2;
                args += 5;
                break;
            }
            goto do_default;

        CASE_OP_32_64(setcond):
            tmp = do_constant_folding_cond(op, args[1], args[2], args[3]);
            if (tmp != 2) {
                gen_opc_buf[op_index] = op_to_movi(op);
                tcg_opt_gen_movi(gen_args, args[0], tmp);
                gen_args += 2;
                args += 4;
                break;
            }
            goto do_default;

        CASE_OP_32_64(brcond):
            tmp = do_constant_folding_cond(op, args[0], args[1], args[2]);
            if (tmp != 2) {
                if (tmp) {
                    memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));
                    gen_opc_buf[op_index] = INDEX_op_br;
                    gen_args[0] = args[3];
                    gen_args += 1;
                } else {
                    gen_opc_buf[op_index] = INDEX_op_nop;
                }
                args += 4;
                break;
            }
            goto do_default;

        CASE_OP_32_64(movcond):
            tmp = do_constant_folding_cond(op, args[1], args[2], args[5]);
            if (tmp != 2) {
                if (temps_are_copies(args[0], args[4-tmp])) {
                    gen_opc_buf[op_index] = INDEX_op_nop;
                } else if (temps[args[4-tmp]].state == TCG_TEMP_CONST) {
                    gen_opc_buf[op_index] = op_to_movi(op);
                    tcg_opt_gen_movi(gen_args, args[0], temps[args[4-tmp]].val);
                    gen_args += 2;
                } else {
                    gen_opc_buf[op_index] = op_to_mov(op);
                    tcg_opt_gen_mov(s, gen_args, args[0], args[4-tmp]);
                    gen_args += 2;
                }
                args += 6;
                break;
            }
            goto do_default;

        case INDEX_op_add2_i32:
        case INDEX_op_sub2_i32:
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[3]].state == TCG_TEMP_CONST
                && temps[args[4]].state == TCG_TEMP_CONST
                && temps[args[5]].state == TCG_TEMP_CONST) {
                uint32_t al = temps[args[2]].val;
                uint32_t ah = temps[args[3]].val;
                uint32_t bl = temps[args[4]].val;
                uint32_t bh = temps[args[5]].val;
                uint64_t a = ((uint64_t)ah << 32) | al;
                uint64_t b = ((uint64_t)bh << 32) | bl;
                TCGArg rl, rh;

                if (op == INDEX_op_add2_i32) {
                    a += b;
                } else {
                    a -= b;
                }

                /* We emit the extra nop when we emit the add2/sub2.  */
                assert(gen_opc_buf[op_index + 1] == INDEX_op_nop);

                rl = args[0];
                rh = args[1];
                gen_opc_buf[op_index] = INDEX_op_movi_i32;
                gen_opc_buf[++op_index] = INDEX_op_movi_i32;
                tcg_opt_gen_movi(&gen_args[0], rl, (uint32_t)a);
                tcg_opt_gen_movi(&gen_args[2], rh, (uint32_t)(a >> 32));
                gen_args += 4;
                args += 6;
                break;
            }
            goto do_default;

        case INDEX_op_mulu2_i32:
            if (temps[args[2]].state == TCG_TEMP_CONST
                && temps[args[3]].state == TCG_TEMP_CONST) {
                uint32_t a = temps[args[2]].val;
                uint32_t b = temps[args[3]].val;
                uint64_t r = (uint64_t)a * b;
                TCGArg rl, rh;

                /* We emit the extra nop when we emit the mulu2.  */
                assert(gen_opc_buf[op_index + 1] == INDEX_op_nop);

                rl = args[0];
                rh = args[1];
                gen_opc_buf[op_index] = INDEX_op_movi_i32;
                gen_opc_buf[++op_index] = INDEX_op_movi_i32;
                tcg_opt_gen_movi(&gen_args[0], rl, (uint32_t)r);
                tcg_opt_gen_movi(&gen_args[2], rh, (uint32_t)(r >> 32));
                gen_args += 4;
                args += 4;
                break;
            }
            goto do_default;

        case INDEX_op_brcond2_i32:
            tmp = do_constant_folding_cond2(&args[0], &args[2], args[4]);
            if (tmp != 2) {
                if (tmp) {
                    memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));
                    gen_opc_buf[op_index] = INDEX_op_br;
                    gen_args[0] = args[5];
                    gen_args += 1;
                } else {
                    gen_opc_buf[op_index] = INDEX_op_nop;
                }
            } else if ((args[4] == TCG_COND_LT || args[4] == TCG_COND_GE)
                       && temps[args[2]].state == TCG_TEMP_CONST
                       && temps[args[3]].state == TCG_TEMP_CONST
                       && temps[args[2]].val == 0
                       && temps[args[3]].val == 0) {
                /* Simplify LT/GE comparisons vs zero to a single compare
                   vs the high word of the input.  */
                memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));
                gen_opc_buf[op_index] = INDEX_op_brcond_i32;
                gen_args[0] = args[1];
                gen_args[1] = args[3];
                gen_args[2] = args[4];
                gen_args[3] = args[5];
                gen_args += 4;
            } else {
                goto do_default;
            }
            args += 6;
            break;

        case INDEX_op_setcond2_i32:
            tmp = do_constant_folding_cond2(&args[1], &args[3], args[5]);
            if (tmp != 2) {
                gen_opc_buf[op_index] = INDEX_op_movi_i32;
                tcg_opt_gen_movi(gen_args, args[0], tmp);
                gen_args += 2;
            } else if ((args[5] == TCG_COND_LT || args[5] == TCG_COND_GE)
                       && temps[args[3]].state == TCG_TEMP_CONST
                       && temps[args[4]].state == TCG_TEMP_CONST
                       && temps[args[3]].val == 0
                       && temps[args[4]].val == 0) {
                /* Simplify LT/GE comparisons vs zero to a single compare
                   vs the high word of the input.  */
                gen_opc_buf[op_index] = INDEX_op_setcond_i32;
                gen_args[0] = args[0];
                gen_args[1] = args[2];
                gen_args[2] = args[4];
                gen_args[3] = args[5];
                gen_args += 4;
            } else {
                goto do_default;
            }
            args += 6;
            break;

        case INDEX_op_call:
            nb_call_args = (args[0] >> 16) + (args[0] & 0xffff);
            if (!(args[nb_call_args + 1] & (TCG_CALL_NO_READ_GLOBALS |
                                            TCG_CALL_NO_WRITE_GLOBALS))) {
                for (i = 0; i < nb_globals; i++) {
                    reset_temp(i);
                }
            }
            for (i = 0; i < (args[0] >> 16); i++) {
                reset_temp(args[i + 1]);
            }
            i = nb_call_args + 3;
            while (i) {
                *gen_args = *args;
                args++;
                gen_args++;
                i--;
            }
            break;

        default:
        do_default:
            /* Default case: we know nothing about operation (or were unable
               to compute the operation result) so no propagation is done.
               We trash everything if the operation is the end of a basic
               block, otherwise we only trash the output args.  */
            if (def->flags & TCG_OPF_BB_END) {
                memset(temps, 0, nb_temps * sizeof(struct tcg_temp_info));
            } else {
                for (i = 0; i < def->nb_oargs; i++) {
                    reset_temp(args[i]);
                }
            }
            for (i = 0; i < def->nb_args; i++) {
                gen_args[i] = args[i];
            }
            args += def->nb_args;
            gen_args += def->nb_args;
            break;
        }
    }

    return gen_args;
}

TCGArg *tcg_optimize(TCGContext *s, uint16_t *tcg_opc_ptr,
        TCGArg *args, TCGOpDef *tcg_op_defs)
{
    TCGArg *res;
    res = tcg_constant_folding(s, tcg_opc_ptr, args, tcg_op_defs);
    return res;
}
