/*
 *  M68K helper routines
 *
 *  Copyright (c) 2007 CodeSourcery
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "cpu.h"
#include "dyngen-exec.h"
#include "helpers.h"

#if defined(CONFIG_USER_ONLY)

void do_interrupt(CPUState *env1)
{
    env1->exception_index = -1;
}

void do_interrupt_m68k_hardirq(CPUState *env1)
{
}

#define ldub_data ldub
#define lduw_data lduw
#define ldl_data ldl
#define stb_data stb
#define stw_data stw
#define stl_data stl
#else

extern int semihosting_enabled;

#include "softmmu_exec.h"

#define MMUSUFFIX _mmu

#define SHIFT 0
#include "softmmu_template.h"

#define SHIFT 1
#include "softmmu_template.h"

#define SHIFT 2
#include "softmmu_template.h"

#define SHIFT 3
#include "softmmu_template.h"

/* Try to fill the TLB and return an exception if error. If retaddr is
   NULL, it means that the function was called in C code (i.e. not
   from generated code or from helper.c) */
/* XXX: fix it to restore all registers */
void tlb_fill(CPUState *env1, target_ulong addr, int is_write, int mmu_idx,
              void *retaddr)
{
    TranslationBlock *tb;
    CPUState *saved_env;
    unsigned long pc;
    int ret;

    saved_env = env;
    env = env1;
    ret = cpu_m68k_handle_mmu_fault(env, addr, is_write, mmu_idx);
    if (unlikely(ret)) {
        if (retaddr) {
            /* now we have a real cpu fault */
            pc = (unsigned long)retaddr;
            tb = tb_find_pc(pc);
            if (tb) {
                /* the PC is inside the translated code. It means that we have
                   a virtual CPU fault */
                cpu_restore_state(tb, env, pc);
            }
        }
        cpu_loop_exit(env);
    }
    env = saved_env;
}

static void do_rte(void)
{
    uint32_t sp;
    uint16_t fmt;

    sp = env->aregs[7];
    if (m68k_feature(env, M68K_FEATURE_M68000)) {
throwaway:
        env->sr = lduw_kernel(sp);
        sp += 2;
        env->pc = ldl_kernel(sp);
        sp += 4;
        if (m68k_feature(env, M68K_FEATURE_QUAD_MULDIV)) {
            /*  all excepte 68000 */
            fmt = lduw_kernel(sp);
            sp += 2;
            switch (fmt >> 12) {
            case 0:
                break;
            case 1:
                env->aregs[7] = sp;
                m68k_switch_sp(env);
                goto throwaway;
            case 2:
            case 3:
                sp += 4;
                break;
            case 4:
                sp += 8;
                break;
            case 7:
                sp += 52;
                break;
            }
            env->aregs[7] = sp;
            m68k_switch_sp(env);
        }
    } else {
        fmt = ldl_kernel(sp);
        env->pc = ldl_kernel(sp + 4);
        sp |= (fmt >> 28) & 3;
        env->sr = fmt & 0xffff;
        sp += 8;
        env->aregs[7] = sp;
        m68k_switch_sp(env);
    }
}

static inline void do_stack_frame(uint32_t *sp, uint16_t format,
                                  uint16_t sr,
                                  uint32_t addr, uint32_t retaddr)
{
    switch (format) {
    case 4:
        *sp -= 4;
        stl_kernel(*sp, env->pc);
        *sp -= 4;
        stl_kernel(*sp, addr);
        break;
    case 3:
    case 2:
        *sp -= 4;
        stl_kernel(*sp, addr);
        break;
    }
    *sp -= 2;
    stw_kernel(*sp, (format << 12) + (env->exception_index << 2));
    *sp -= 4;
    stl_kernel(*sp, retaddr);
    *sp -= 2;
    stw_kernel(*sp, sr);
}

static void do_interrupt_all(int is_hw)
{
    uint32_t sp;
    uint32_t fmt;
    uint32_t retaddr;
    uint32_t vector;
    int oldsr;

    fmt = 0;
    retaddr = env->pc;

    if (!is_hw) {
        switch (env->exception_index) {
        case EXCP_RTE:
            /* Return from an exception.  */
            do_rte();
            return;
        case EXCP_UNSUPPORTED:
            cpu_abort(env, "Illegal instruction: %04x @ %08x",
                      lduw_code(env->pc), env->pc);
            break;
        case EXCP_HALT_INSN:
            if (semihosting_enabled
                    && (env->sr & SR_S) != 0
                    && (env->pc & 3) == 0
                    && lduw_code(env->pc - 4) == 0x4e71
                    && ldl_code(env->pc) == 0x4e7bf000) {
                env->pc += 4;
                do_m68k_semihosting(env, env->dregs[0]);
                return;
            }
            env->halted = 1;
            env->exception_index = EXCP_HLT;
            cpu_loop_exit(env);
            return;
        }
        if (env->exception_index >= EXCP_TRAP0
            && env->exception_index <= EXCP_TRAP15) {
            /* Move the PC after the trap instruction.  */
            retaddr += 2;
        }
    }

    vector = env->exception_index << 2;

    sp = env->aregs[7];

    if (qemu_loglevel_mask(CPU_LOG_INT)) {
        static int count = 0;
        static const char *name;
        name = "Unassigned";
        switch(env->exception_index) {
        case 0: name = "Reset Interrupt SP"; break;
        case 1: name = "Reset PC"; break;
        case 2: name = "Access Fault"; break;
        case 3: name = "Address Error"; break;
        case 4: name = "Illegal Instruction"; break;
        case 5: name = "Divide by Zero"; break;
        case 6: name = "CHK/CHK2"; break;
        case 7: name = "FTRAPcc, TRAPcc, TRAPV"; break;
        case 8: name = "Privilege Violation"; break;
        case 9: name = "Trace"; break;
        case 10: name = "A-Line"; break;
        case 11: name = "F-Line"; break;
        case 13: name = "Copro Protocol Violation"; break; /* 68020/030 only */
        case 14: name = "Format Error"; break;
        case 15: name = "Unitialized Interruot"; break;

        case 24: name = "Spurious Interrupt"; break;
        case 25: name = "Level 1 Interrupt"; break;
        case 26: name = "Level 2 Interrupt"; break;
        case 27: name = "Level 3 Interrupt"; break;
        case 28: name = "Level 4 Interrupt"; break;
        case 29: name = "Level 5 Interrupt"; break;
        case 30: name = "Level 6 Interrupt"; break;
        case 31: name = "Level 7 Interrupt"; break;

        case 32: name = "TRAP #0"; break;
        case 33: name = "TRAP #1"; break;
        case 34: name = "TRAP #2"; break;
        case 35: name = "TRAP #3"; break;
        case 36: name = "TRAP #4"; break;
        case 37: name = "TRAP #5"; break;
        case 38: name = "TRAP #6"; break;
        case 39: name = "TRAP #7"; break;
        case 40: name = "TRAP #8"; break;
        case 41: name = "TRAP #9"; break;
        case 42: name = "TRAP #10"; break;
        case 43: name = "TRAP #11"; break;
        case 44: name = "TRAP #12"; break;
        case 45: name = "TRAP #13"; break;
        case 46: name = "TRAP #14"; break;
        case 47: name = "TRAP #15"; break;

        case 48: name = "FP Branch/Set on unordered condition"; break;
        case 49: name = "FP Inexact Result"; break;
        case 50: name = "FP Divide by Zero"; break;
        case 51: name = "FP Underflow"; break;
        case 52: name = "FP Operand Error"; break;
        case 53: name = "FP Overflow"; break;
        case 54: name = "FP Signaling NAN"; break;
        case 55: name = "FP Unimplemented Data Type"; break;

        case 56: name = "MMU Configuration Error"; break; /* 68030/68851 only */
        case 57: name = "MMU Illegal Operation"; break; /* 68851 only */
        case 58: name = "MMU Access Level Violation"; break; /* 68851 only */

        case 64 ... 255: name = "User Defined Vector"; break;
        }
        qemu_log("INT %6d: %s(%#x) pc=%08x sp=%08x sr=%04x\n",
                 ++count, name, vector, env->pc, sp, env->sr);
    }

    /*
     * MC68040UM/AD,  chapter 9.3.10
     */

    /* "the processor first make an internal copy" */
    oldsr = env->sr;
    /* "set the mode to supervisor" */
    env->sr |= SR_S;
    /* "suppress tracing" */
    env->sr &= ~SR_T;
    /* "sets the processor interrupt mask" */
    if (is_hw) {
        env->sr |= (env->sr & ~SR_I) | (env->pending_level << SR_I_SHIFT);
    }

    m68k_switch_sp(env);
    sp = env->aregs[7];

    if (m68k_feature(env, M68K_FEATURE_M68000)) {
        static int mmu_fault = 0;
        sp &= ~1;
        if (env->exception_index == 2) {
            if (mmu_fault) {
                cpu_abort(env, "DOUBLE MMU FAULT\n");
            }
            mmu_fault = 1;
            sp -= 4;
            stl_kernel(sp, 0); /* push data 3 */
            sp -= 4;
            stl_kernel(sp, 0); /* push data 2 */
            sp -= 4;
            stl_kernel(sp, 0); /* push data 1 */
            sp -= 4;
            stl_kernel(sp, 0); /* write back 1 / push data 0 */
            sp -= 4;
            stl_kernel(sp, 0); /* write back 1 address */
            sp -= 4;
            stl_kernel(sp, 0); /* write back 2 data */
            sp -= 4;
            stl_kernel(sp, 0); /* write back 2 address */
            sp -= 4;
            stl_kernel(sp, env->mmu.wb3_data); /* write back 3 data */
            sp -= 4;
            stl_kernel(sp, env->mmu.ar); /* write back 3 address */
            sp -= 4;
            stl_kernel(sp, env->mmu.ar); /* fault address */
            sp -= 2;
            stw_kernel(sp, 0); /* write back 1 status */
            sp -= 2;
            stw_kernel(sp, 0); /* write back 2 status */
            sp -= 2;
            stw_kernel(sp, env->mmu.wb3_status); /* write back 3 status */
            sp -= 2;
            stw_kernel(sp, env->mmu.ssw); /* special status word */
            sp -= 4;
            stl_kernel(sp, env->mmu.ar); /* effective address */
            do_stack_frame(&sp, 7, oldsr, 0, retaddr);
            mmu_fault = 0;
            if (qemu_loglevel_mask(CPU_LOG_INT)) {
                qemu_log("            wb3d: %08x wb3a: %08x wb3s: %04x\n"
                         "            ssw:  %08x ea:   %08x sfc:  %d    dfc: %d\n",
                         env->mmu.wb3_data, env->mmu.ar, env->mmu.wb3_status,
                         env->mmu.ssw, env->mmu.ar, env->sfc, env->dfc);
            }
        } else if (env->exception_index == 3) {
            do_stack_frame(&sp, 2, oldsr, 0, retaddr);
        } else if (env->exception_index == 5 ||
                   env->exception_index == 6 ||
                   env->exception_index == 7 ||
                   env->exception_index == 9) {
            /* FIXME: addr is not only env->pc */
            do_stack_frame(&sp, 2, oldsr, env->pc, retaddr);
        } else if (is_hw && env->exception_index >= 24 &&
                   env->exception_index < 32) {
            do_stack_frame(&sp, 0, oldsr, 0, retaddr);
            if (env->sr & SR_M) {
                oldsr = env->sr;
                env->sr &= ~SR_M;
                env->aregs[7] = sp;
                m68k_switch_sp(env);
                sp = env->aregs[7] & ~1;
                do_stack_frame(&sp, 1, oldsr, 0, retaddr);
            }
        } else {
            do_stack_frame(&sp, 0, oldsr, 0, retaddr);
        }
    } else {
        fmt |= 0x40000000;
        fmt |= (sp & 3) << 28;
        fmt |= vector << 16;
        fmt |= oldsr;

        sp &= ~3;
        sp -= 4;
        stl_kernel(sp, retaddr);
        sp -= 4;
        stl_kernel(sp, fmt);
    }

    env->aregs[7] = sp;
    /* Jump to vector.  */
    env->pc = ldl_kernel(env->vbr + vector);
}

void do_interrupt(CPUState *env1)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    do_interrupt_all(0);
    env = saved_env;
}

void do_interrupt_m68k_hardirq(CPUState *env1)
{
    CPUState *saved_env;

    saved_env = env;
    env = env1;
    do_interrupt_all(1);
    env = saved_env;
}
#endif

static void raise_exception(int tt)
{
    env->exception_index = tt;
    cpu_loop_exit(env);
}

/* load from a bitfield */

uint64_t HELPER(bitfield_load)(uint32_t addr, uint32_t offset, uint32_t width)
{
    uint64_t bitfield = 0;
    int size;

    size = (offset + width + 7) >> 3;
    switch(size) {
    case 1:
        bitfield = ldub_data(addr);
        bitfield <<= 56;
        break;
    case 2:
        bitfield = lduw_data(addr);
        bitfield <<= 48;
        break;
    case 3:
        bitfield = lduw_data(addr);
        bitfield <<= 8;
        bitfield |= ldub_data(addr + 2);
        bitfield <<= 40;
        break;
    case 4:
        bitfield = ldl_data(addr);
        bitfield <<= 32;
        break;
    case 5:
        bitfield = ldl_data(addr);
        bitfield <<= 8;
        bitfield |= ldub_data(addr + 4);
        bitfield <<= 24;
        break;
    }

    return bitfield;
}

/* store to a bitfield */

void HELPER(bitfield_store)(uint32_t addr, uint32_t offset, uint32_t width,
                            uint64_t bitfield)
{
    int size;

    size = (offset + width + 7) >> 3;
    switch(size) {
    case 1:
        stb_data(addr, bitfield >> 56);
        break;
    case 2:
        stw_data(addr, bitfield >> 48);
        break;
    case 3:
        stw_data(addr, bitfield >> 48);
        stb_data(addr + 2, bitfield >> 40);
        break;
    case 4:
        stl_data(addr, bitfield >> 32);
        break;
    case 5:
        stl_data(addr, bitfield >> 32);
        stb_data(addr + 4, bitfield >> 24);
        break;
    }
}

void HELPER(raise_exception)(uint32_t tt)
{
    raise_exception(tt);
}

void HELPER(divu)(CPUState *env, uint32_t word)
{
    uint32_t num;
    uint32_t den;
    uint32_t quot;
    uint32_t rem;
    uint32_t flags;

    num = env->div1;
    den = env->div2;
    /* ??? This needs to make sure the throwing location is accurate.  */
    if (den == 0)
        raise_exception(EXCP_DIV0);
    quot = num / den;
    rem = num % den;
    flags = 0;
    /* Avoid using a PARAM1 of zero.  This breaks dyngen because it uses
       the address of a symbol, and gcc knows symbols can't have address
       zero.  */
    if (word && quot > 0xffff) {
	/* real 68040 keep Z and N on overflow,
         * whereas documentation says "undefined"
         */
        flags |= CCF_V | (env->cc_dest & (CCF_Z|CCF_N));
    } else {
        if (quot == 0)
            flags |= CCF_Z;
        else if ((int16_t)quot < 0)
            flags |= CCF_N;
    }

    env->div1 = quot;
    env->div2 = rem;
    env->cc_dest = flags;
}

void HELPER(divs)(CPUState *env, uint32_t word)
{
    int32_t num;
    int32_t den;
    int32_t quot;
    int32_t rem;
    int32_t flags;

    num = env->div1;
    den = env->div2;
    if (den == 0)
        raise_exception(EXCP_DIV0);
    quot = num / den;
    rem = num % den;
    flags = 0;
    if (word && quot != (int16_t)quot) {
	/* real 68040 keep Z and N on overflow,
         * whereas documentation says "undefined"
         */
        flags |= CCF_V | (env->cc_dest & (CCF_Z|CCF_N));
    } else {
        if (quot == 0)
            flags |= CCF_Z;
        else if ((int16_t)quot < 0)
            flags |= CCF_N;
    }

    env->div1 = quot;
    env->div2 = rem;
    env->cc_dest = flags;
}

void HELPER(divu64)(CPUState *env)
{
    uint32_t num;
    uint32_t den;
    uint64_t quot;
    uint32_t rem;
    uint32_t flags;
    uint64_t quad;

    num = env->div1;
    den = env->div2;
    /* ??? This needs to make sure the throwing location is accurate.  */
    if (den == 0)
        raise_exception(EXCP_DIV0);
    quad = num | ((uint64_t)env->quadh << 32);
    quot = quad / den;
    rem = quad % den;
    if (quot > 0xffffffffULL) {
        flags = (env->cc_dest & ~ CCF_C) | CCF_V;
    } else {
        flags = 0;
        if (quot == 0)
            flags |= CCF_Z;
        else if ((int32_t)quot < 0)
            flags |= CCF_N;
        env->div1 = quot;
        env->quadh = rem;
    }
    env->cc_dest = flags;
}

void HELPER(divs64)(CPUState *env)
{
    uint32_t num;
    int32_t den;
    int64_t quot;
    int32_t rem;
    int32_t flags;
    int64_t quad;

    num = env->div1;
    den = env->div2;
    if (den == 0)
        raise_exception(EXCP_DIV0);
    quad = num | ((int64_t)env->quadh << 32);
    quot = quad / (int64_t)den;
    rem = quad % (int64_t)den;

    if ((quot & 0xffffffff80000000ULL) &&
        (quot & 0xffffffff80000000ULL) != 0xffffffff80000000ULL) {
	flags = (env->cc_dest & ~ CCF_C) | CCF_V;
    } else {
        flags = 0;
        if (quot == 0)
	    flags |= CCF_Z;
        else if ((int32_t)quot < 0)
	    flags |= CCF_N;
        env->div1 = quot;
        env->quadh = rem;
    }
    env->cc_dest = flags;
}

uint32_t HELPER(mulu32_cc)(CPUState *env, uint32_t op1, uint32_t op2)
{
    uint64_t res = (uint32_t)op1 * op2;
    uint32_t flags;

    flags = 0;
    if (res >> 32)
       flags |= CCF_V;
    if ((uint32_t)res == 0)
       flags |= CCF_Z;
    if ((int32_t)res < 0)
       flags |= CCF_N;
    env->cc_dest = flags;

    return res;
}

uint32_t HELPER(muls32_cc)(CPUState *env, uint32_t op1, uint32_t op2)
{
    int64_t res = (int32_t)op1 * (int32_t)op2;
    uint32_t flags;

    flags = 0;
    if (res != (int64_t)(int32_t)res)
       flags |= CCF_V;
    if ((uint32_t)res == 0)
       flags |= CCF_Z;
    if ((int32_t)res < 0)
       flags |= CCF_N;
    env->cc_dest = flags;

    return res;
}

uint32_t HELPER(mulu64)(CPUState *env, uint32_t op1, uint32_t op2)
{
    uint64_t res = (uint64_t)op1 * op2;
    uint32_t flags;

    env->quadh = res >> 32;
    flags = 0;
    if (res == 0)
       flags |= CCF_Z;
    if ((int64_t)res < 0)
       flags |= CCF_N;
    env->cc_dest = flags;

    return res;
}

uint32_t HELPER(muls64)(CPUState *env, uint32_t op1, uint32_t op2)
{
    int64_t res = (uint64_t)(int32_t)op1 * (int32_t)op2;
    uint32_t flags;

    env->quadh = res >> 32;
    flags = 0;
    if (res == 0)
       flags |= CCF_Z;
    if (res < 0)
       flags |= CCF_N;
    env->cc_dest = flags;

    return res;
}
