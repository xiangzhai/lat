/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Freestanding x86_64 guest for 32-bit SHLD/SHRD operand semantics.
 *
 * In 64-bit mode a 32-bit destination is zero-extended, the shift count is
 * masked to five bits, CF/OF/ZF/SF/PF follow the 32-bit result, and OF is
 * defined for a count of one.  The test covers immediate and CL counts,
 * register and memory destinations, result/flags/zero-extension, and the
 * unchanged flags of a zero count.
 */

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

static long sys_write(long fd, const void *buf, unsigned long n)
{
    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(1L), "D"(fd), "S"(buf),
                     "d"(n) : "rcx", "r11", "memory");
    return ret;
}

static void sys_exit(int code)
{
    __asm__ volatile("syscall" : : "a"(60L), "D"(code) : "rcx", "r11", "memory");
    __builtin_unreachable();
}

static void fail(const char *msg)
{
    while (*msg)
        sys_write(1, msg++, 1);
    sys_write(1, "\n", 1);
}

static u64 prng = 0x243f6a8885a308d3ULL;

static u32 next_random(void)
{
    prng ^= prng << 13;
    prng ^= prng >> 7;
    prng ^= prng << 17;
    return (u32)(prng >> 16);
}

static int check(int shld, u32 d, u32 s, int count, u32 res, u64 flags,
                 u64 flags_before, u64 dest_after, int is_gpr)
{
    u32 expected;
    u64 window;
    u8 low;
    int ones, ok = 1;
    int c = count & 31;

    /*
     * Compute the reference from the 64-bit [high:low] concatenation.  Do not
     * write this as ((d << c) | (s >> (32 - c))): at -O2 the compiler folds
     * that idiom back into SHLD/SHRD, which would make the oracle use the very
     * instruction under test.  The concatenation lowers to plain 64-bit
     * shifts.
     */
    if (c == 0)
        expected = d;
    else if (shld) {
        window = ((u64)d << 32) | s;
        expected = (u32)(window >> (32 - c));
    } else {
        window = ((u64)s << 32) | d;
        expected = (u32)(window >> c);
    }

    if (res != expected)
        ok = 0;

    if (is_gpr && dest_after != (u64)expected)
        ok = 0;

    if (c == 0) {
        /*
         * A masked count of zero performs no shift and leaves every flag
         * untouched, even though a 32-bit destination is still zero-extended.
         */
        if (flags != flags_before)
            ok = 0;
    } else {
        u32 cf = shld ? ((d >> (32 - c)) & 1) : ((d >> (c - 1)) & 1);
        u32 sf = expected >> 31;
        u32 zf = expected == 0;
        u32 got_cf = (flags >> 0) & 1;
        u32 got_pf = (flags >> 2) & 1;
        u32 got_zf = (flags >> 6) & 1;
        u32 got_sf = (flags >> 7) & 1;
        u32 got_of = (flags >> 11) & 1;

        low = (u8)expected;
        ones = 0;
        while (low) {
            ones += low & 1;
            low >>= 1;
        }

        if (got_cf != cf || got_pf != (ones % 2 == 0) ||
            got_zf != zf || got_sf != sf)
            ok = 0;

        if (c == 1 && got_of != ((d >> 31) ^ sf))
            ok = 0;
    }

    return ok;
}

#define DEF_CL_FN(fn, ins)                                                    \
    static u64 fn(u32 d, u32 s, u8 c, u32 *res, u64 *dest_after,              \
                  u64 *flags_before)                                          \
    {                                                                         \
        register u64 rax asm("rax") = 0xdeadbeef00000000ULL | d;              \
        register u64 rbx asm("rbx") = s;                                      \
        register u64 rcx asm("rcx") = c;                                      \
        u64 flags, before;                                                    \
                                                                              \
        __asm__ volatile("pushfq\n\t"                                         \
                         "popq %4\n\t"                                        \
                         ins " %%cl, %%ebx, %%eax\n\t"                        \
                         "pushfq\n\t"                                         \
                         "popq %3"                                            \
                         : "+a"(rax), "+b"(rbx), "+c"(rcx), "=&r"(flags),      \
                           "=&r"(before)                                       \
                         :                                                    \
                         : "cc", "memory");                                   \
        *res = (u32)rax;                                                      \
        *dest_after = rax;                                                    \
        *flags_before = before;                                               \
        return flags;                                                         \
    }

DEF_CL_FN(shld32_cl, "shldl")
DEF_CL_FN(shrd32_cl, "shrdl")

#define DEF_IMM_FN(fn, ins, n)                                                \
    static u64 fn(u32 d, u32 s, u32 *res, u64 *dest_after,                    \
                  u64 *flags_before)                                          \
    {                                                                         \
        register u64 rax asm("rax") = 0xdeadbeef00000000ULL | d;              \
        register u64 rbx asm("rbx") = s;                                      \
        u64 flags, before;                                                    \
                                                                              \
        __asm__ volatile("pushfq\n\t"                                         \
                         "popq %3\n\t"                                        \
                         ins " $" #n ", %%ebx, %%eax\n\t"                     \
                         "pushfq\n\t"                                         \
                         "popq %2"                                            \
                         : "+a"(rax), "+b"(rbx), "=&r"(flags),                 \
                           "=&r"(before)                                       \
                         :                                                    \
                         : "cc", "memory");                                   \
        *res = (u32)rax;                                                      \
        *dest_after = rax;                                                    \
        *flags_before = before;                                               \
        return flags;                                                         \
    }

#define SHLD_IMM(n) DEF_IMM_FN(shld32_imm_##n, "shldl", n)
#define SHRD_IMM(n) DEF_IMM_FN(shrd32_imm_##n, "shrdl", n)

SHLD_IMM(0)  SHLD_IMM(1)  SHLD_IMM(2)  SHLD_IMM(3)
SHLD_IMM(4)  SHLD_IMM(5)  SHLD_IMM(6)  SHLD_IMM(7)
SHLD_IMM(8)  SHLD_IMM(9)  SHLD_IMM(10) SHLD_IMM(11)
SHLD_IMM(12) SHLD_IMM(13) SHLD_IMM(14) SHLD_IMM(15)
SHLD_IMM(16) SHLD_IMM(17) SHLD_IMM(18) SHLD_IMM(19)
SHLD_IMM(20) SHLD_IMM(21) SHLD_IMM(22) SHLD_IMM(23)
SHLD_IMM(24) SHLD_IMM(25) SHLD_IMM(26) SHLD_IMM(27)
SHLD_IMM(28) SHLD_IMM(29) SHLD_IMM(30) SHLD_IMM(31)

SHRD_IMM(0)  SHRD_IMM(1)  SHRD_IMM(2)  SHRD_IMM(3)
SHRD_IMM(4)  SHRD_IMM(5)  SHRD_IMM(6)  SHRD_IMM(7)
SHRD_IMM(8)  SHRD_IMM(9)  SHRD_IMM(10) SHRD_IMM(11)
SHRD_IMM(12) SHRD_IMM(13) SHRD_IMM(14) SHRD_IMM(15)
SHRD_IMM(16) SHRD_IMM(17) SHRD_IMM(18) SHRD_IMM(19)
SHRD_IMM(20) SHRD_IMM(21) SHRD_IMM(22) SHRD_IMM(23)
SHRD_IMM(24) SHRD_IMM(25) SHRD_IMM(26) SHRD_IMM(27)
SHRD_IMM(28) SHRD_IMM(29) SHRD_IMM(30) SHRD_IMM(31)

typedef u64 (*imm_fn)(u32 d, u32 s, u32 *res, u64 *dest_after,
                      u64 *flags_before);

#define ENTRY(n) shld32_imm_##n,
static const imm_fn shld32_imm_fns[32] = {
    ENTRY(0)  ENTRY(1)  ENTRY(2)  ENTRY(3)  ENTRY(4)  ENTRY(5)  ENTRY(6)  ENTRY(7)
    ENTRY(8)  ENTRY(9)  ENTRY(10) ENTRY(11) ENTRY(12) ENTRY(13) ENTRY(14) ENTRY(15)
    ENTRY(16) ENTRY(17) ENTRY(18) ENTRY(19) ENTRY(20) ENTRY(21) ENTRY(22) ENTRY(23)
    ENTRY(24) ENTRY(25) ENTRY(26) ENTRY(27) ENTRY(28) ENTRY(29) ENTRY(30) ENTRY(31)
};
#undef ENTRY

#define ENTRY(n) shrd32_imm_##n,
static const imm_fn shrd32_imm_fns[32] = {
    ENTRY(0)  ENTRY(1)  ENTRY(2)  ENTRY(3)  ENTRY(4)  ENTRY(5)  ENTRY(6)  ENTRY(7)
    ENTRY(8)  ENTRY(9)  ENTRY(10) ENTRY(11) ENTRY(12) ENTRY(13) ENTRY(14) ENTRY(15)
    ENTRY(16) ENTRY(17) ENTRY(18) ENTRY(19) ENTRY(20) ENTRY(21) ENTRY(22) ENTRY(23)
    ENTRY(24) ENTRY(25) ENTRY(26) ENTRY(27) ENTRY(28) ENTRY(29) ENTRY(30) ENTRY(31)
};
#undef ENTRY

#define DEF_MEM_FN(fn, ins)                                                   \
    static u64 fn(u32 *mem, u32 s, u8 c, u32 *res, u64 *flags_before)         \
    {                                                                         \
        register u64 rdi asm("rdi") = (u64)mem;                               \
        register u64 rbx asm("rbx") = s;                                      \
        register u64 rcx asm("rcx") = c;                                      \
        u64 flags, before;                                                    \
                                                                              \
        __asm__ volatile("pushfq\n\t"                                         \
                         "popq %4\n\t"                                        \
                         ins " %%cl, %%ebx, (%%rdi)\n\t"                      \
                         "pushfq\n\t"                                         \
                         "popq %3"                                            \
                         : "+D"(rdi), "+b"(rbx), "+c"(rcx), "=&r"(flags),      \
                           "=&r"(before)                                       \
                         :                                                    \
                         : "cc", "memory");                                   \
        *res = *mem;                                                          \
        *flags_before = before;                                               \
        return flags;                                                         \
    }

DEF_MEM_FN(shld32_mem_cl, "shldl")
DEF_MEM_FN(shrd32_mem_cl, "shrdl")

#define DEF_MEM_IMM_FN(fn, ins, n)                                            \
    static u64 fn(u32 *mem, u32 s, u32 *res, u64 *flags_before)               \
    {                                                                         \
        register u64 rdi asm("rdi") = (u64)mem;                               \
        register u64 rbx asm("rbx") = s;                                      \
        u64 flags, before;                                                    \
                                                                              \
        __asm__ volatile("pushfq\n\t"                                         \
                         "popq %3\n\t"                                        \
                         ins " $" #n ", %%ebx, (%%rdi)\n\t"                   \
                         "pushfq\n\t"                                         \
                         "popq %2"                                            \
                         : "+D"(rdi), "+b"(rbx), "=&r"(flags),                 \
                           "=&r"(before)                                       \
                         :                                                    \
                         : "cc", "memory");                                   \
        *res = *mem;                                                          \
        *flags_before = before;                                               \
        return flags;                                                         \
    }

#define SHLD_MEM_IMM(n) DEF_MEM_IMM_FN(shld32_mem_imm_##n, "shldl", n)
#define SHRD_MEM_IMM(n) DEF_MEM_IMM_FN(shrd32_mem_imm_##n, "shrdl", n)

SHLD_MEM_IMM(0)  SHLD_MEM_IMM(1)  SHLD_MEM_IMM(2)  SHLD_MEM_IMM(3)
SHLD_MEM_IMM(4)  SHLD_MEM_IMM(5)  SHLD_MEM_IMM(6)  SHLD_MEM_IMM(7)
SHLD_MEM_IMM(8)  SHLD_MEM_IMM(9)  SHLD_MEM_IMM(10) SHLD_MEM_IMM(11)
SHLD_MEM_IMM(12) SHLD_MEM_IMM(13) SHLD_MEM_IMM(14) SHLD_MEM_IMM(15)
SHLD_MEM_IMM(16) SHLD_MEM_IMM(17) SHLD_MEM_IMM(18) SHLD_MEM_IMM(19)
SHLD_MEM_IMM(20) SHLD_MEM_IMM(21) SHLD_MEM_IMM(22) SHLD_MEM_IMM(23)
SHLD_MEM_IMM(24) SHLD_MEM_IMM(25) SHLD_MEM_IMM(26) SHLD_MEM_IMM(27)
SHLD_MEM_IMM(28) SHLD_MEM_IMM(29) SHLD_MEM_IMM(30) SHLD_MEM_IMM(31)

SHRD_MEM_IMM(0)  SHRD_MEM_IMM(1)  SHRD_MEM_IMM(2)  SHRD_MEM_IMM(3)
SHRD_MEM_IMM(4)  SHRD_MEM_IMM(5)  SHRD_MEM_IMM(6)  SHRD_MEM_IMM(7)
SHRD_MEM_IMM(8)  SHRD_MEM_IMM(9)  SHRD_MEM_IMM(10) SHRD_MEM_IMM(11)
SHRD_MEM_IMM(12) SHRD_MEM_IMM(13) SHRD_MEM_IMM(14) SHRD_MEM_IMM(15)
SHRD_MEM_IMM(16) SHRD_MEM_IMM(17) SHRD_MEM_IMM(18) SHRD_MEM_IMM(19)
SHRD_MEM_IMM(20) SHRD_MEM_IMM(21) SHRD_MEM_IMM(22) SHRD_MEM_IMM(23)
SHRD_MEM_IMM(24) SHRD_MEM_IMM(25) SHRD_MEM_IMM(26) SHRD_MEM_IMM(27)
SHRD_MEM_IMM(28) SHRD_MEM_IMM(29) SHRD_MEM_IMM(30) SHRD_MEM_IMM(31)

typedef u64 (*mem_imm_fn)(u32 *mem, u32 s, u32 *res, u64 *flags_before);

#define ENTRY(n) shld32_mem_imm_##n,
static const mem_imm_fn shld32_mem_imm_fns[32] = {
    ENTRY(0)  ENTRY(1)  ENTRY(2)  ENTRY(3)  ENTRY(4)  ENTRY(5)  ENTRY(6)  ENTRY(7)
    ENTRY(8)  ENTRY(9)  ENTRY(10) ENTRY(11) ENTRY(12) ENTRY(13) ENTRY(14) ENTRY(15)
    ENTRY(16) ENTRY(17) ENTRY(18) ENTRY(19) ENTRY(20) ENTRY(21) ENTRY(22) ENTRY(23)
    ENTRY(24) ENTRY(25) ENTRY(26) ENTRY(27) ENTRY(28) ENTRY(29) ENTRY(30) ENTRY(31)
};
#undef ENTRY

#define ENTRY(n) shrd32_mem_imm_##n,
static const mem_imm_fn shrd32_mem_imm_fns[32] = {
    ENTRY(0)  ENTRY(1)  ENTRY(2)  ENTRY(3)  ENTRY(4)  ENTRY(5)  ENTRY(6)  ENTRY(7)
    ENTRY(8)  ENTRY(9)  ENTRY(10) ENTRY(11) ENTRY(12) ENTRY(13) ENTRY(14) ENTRY(15)
    ENTRY(16) ENTRY(17) ENTRY(18) ENTRY(19) ENTRY(20) ENTRY(21) ENTRY(22) ENTRY(23)
    ENTRY(24) ENTRY(25) ENTRY(26) ENTRY(27) ENTRY(28) ENTRY(29) ENTRY(30) ENTRY(31)
};
#undef ENTRY

static const u32 edge_values[] = {
    0x00000000, 0x00000001, 0x7fffffff, 0x80000000,
    0xffffffff, 0xdeadbeef, 0x12345678,
};

#define EDGE_COUNT (int)(sizeof(edge_values) / sizeof(edge_values[0]))

static int test_register_cl(void)
{
    int c, i, j, failures = 0;

    /* Counts above 31 verify the architectural five-bit count masking. */
    for (c = 0; c < 256; c++) {
        for (i = 0; i < EDGE_COUNT; i++) {
            for (j = 0; j < EDGE_COUNT; j++) {
                u32 d = edge_values[i], s = edge_values[j];
                u32 res;
                u64 flags, before, after;

                flags = shld32_cl(d, s, c, &res, &after, &before);
                failures += !check(1, d, s, c, res, flags, before, after, 1);
                flags = shrd32_cl(d, s, c, &res, &after, &before);
                failures += !check(0, d, s, c, res, flags, before, after, 1);
            }
        }
        for (i = 0; i < 256; i++) {
            u32 d = next_random(), s = next_random();
            u32 res;
            u64 flags, before, after;

            flags = shld32_cl(d, s, c, &res, &after, &before);
            failures += !check(1, d, s, c, res, flags, before, after, 1);
            flags = shrd32_cl(d, s, c, &res, &after, &before);
            failures += !check(0, d, s, c, res, flags, before, after, 1);
        }
    }

    return failures;
}

static int test_register_imm(void)
{
    int c, i, j, failures = 0;

    for (c = 0; c < 32; c++) {
        for (i = 0; i < EDGE_COUNT; i++) {
            for (j = 0; j < EDGE_COUNT; j++) {
                u32 d = edge_values[i], s = edge_values[j];
                u32 res;
                u64 flags, before, after;

                flags = shld32_imm_fns[c](d, s, &res, &after, &before);
                failures += !check(1, d, s, c, res, flags, before, after, 1);
                flags = shrd32_imm_fns[c](d, s, &res, &after, &before);
                failures += !check(0, d, s, c, res, flags, before, after, 1);
            }
        }
        for (i = 0; i < 64; i++) {
            u32 d = next_random(), s = next_random();
            u32 res;
            u64 flags, before, after;

            flags = shld32_imm_fns[c](d, s, &res, &after, &before);
            failures += !check(1, d, s, c, res, flags, before, after, 1);
            flags = shrd32_imm_fns[c](d, s, &res, &after, &before);
            failures += !check(0, d, s, c, res, flags, before, after, 1);
        }
    }

    return failures;
}

static int test_memory_cl(void)
{
    int c, i, j, failures = 0;

    /* Counts above 31 verify the architectural five-bit count masking. */
    for (c = 0; c < 256; c++) {
        for (i = 0; i < EDGE_COUNT; i++) {
            for (j = 0; j < EDGE_COUNT; j++) {
                u32 d = edge_values[i], s = edge_values[j];
                volatile u32 mem;
                u32 res;
                u64 flags, before;

                mem = d;
                flags = shld32_mem_cl((u32 *)&mem, s, c, &res, &before);
                failures += !check(1, d, s, c, res, flags, before, 0, 0);
                mem = d;
                flags = shrd32_mem_cl((u32 *)&mem, s, c, &res, &before);
                failures += !check(0, d, s, c, res, flags, before, 0, 0);
            }
        }
        for (i = 0; i < 64; i++) {
            u32 d = next_random(), s = next_random();
            volatile u32 mem;
            u32 res;
            u64 flags, before;

            mem = d;
            flags = shld32_mem_cl((u32 *)&mem, s, c, &res, &before);
            failures += !check(1, d, s, c, res, flags, before, 0, 0);
            mem = d;
            flags = shrd32_mem_cl((u32 *)&mem, s, c, &res, &before);
            failures += !check(0, d, s, c, res, flags, before, 0, 0);
        }
    }

    return failures;
}

static int test_memory_imm(void)
{
    int c, i, j, failures = 0;

    for (c = 0; c < 32; c++) {
        for (i = 0; i < EDGE_COUNT; i++) {
            for (j = 0; j < EDGE_COUNT; j++) {
                u32 d = edge_values[i], s = edge_values[j];
                volatile u32 mem;
                u32 res;
                u64 flags, before;

                mem = d;
                flags = shld32_mem_imm_fns[c]((u32 *)&mem, s, &res, &before);
                failures += !check(1, d, s, c, res, flags, before, 0, 0);
                mem = d;
                flags = shrd32_mem_imm_fns[c]((u32 *)&mem, s, &res, &before);
                failures += !check(0, d, s, c, res, flags, before, 0, 0);
            }
        }
        for (i = 0; i < 64; i++) {
            u32 d = next_random(), s = next_random();
            volatile u32 mem;
            u32 res;
            u64 flags, before;

            mem = d;
            flags = shld32_mem_imm_fns[c]((u32 *)&mem, s, &res, &before);
            failures += !check(1, d, s, c, res, flags, before, 0, 0);
            mem = d;
            flags = shrd32_mem_imm_fns[c]((u32 *)&mem, s, &res, &before);
            failures += !check(0, d, s, c, res, flags, before, 0, 0);
        }
    }

    return failures;
}

void _start(void)
{
    int failures = 0;

    failures += test_register_cl();
    failures += test_register_imm();
    failures += test_memory_cl();
    failures += test_memory_imm();

    if (failures) {
        fail("SHLD/SHRD 32-bit operand test failed");
        sys_exit(1);
    }

    sys_exit(0);
}
