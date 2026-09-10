/* Run ARM64 guest snippets that common Unicorn detectors use. */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <unicorn/unicorn.h>

#define CODE 0x1000
#define CODE_SIZE 0x4000

static int g_fail;

static void fail(const char *name, const char *why)
{
    printf("  FAIL  %s: %s\n", name, why);
    g_fail++;
}

static void pass(const char *name, const char *detail)
{
    printf("  PASS  %s: %s\n", name, detail);
}

static uc_engine *open_uc(void)
{
    uc_engine *uc;
    uc_err err = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc);
    if (err) {
        printf("uc_open failed: %s\n", uc_strerror(err));
        return NULL;
    }
    if (uc_mem_map(uc, CODE, CODE_SIZE, UC_PROT_ALL) != UC_ERR_OK) {
        uc_close(uc);
        return NULL;
    }
    return uc;
}

static uc_err run_insns(uc_engine *uc, const void *insns, size_t len)
{
    uc_err err = uc_mem_write(uc, CODE, insns, len);
    if (err) {
        return err;
    }
    return uc_emu_start(uc, CODE, CODE + len, 0, 0);
}

static void test_currentel(void)
{
    uc_engine *uc = open_uc();
    uint64_t x0 = 0xdead;
    /* mrs x0, CurrentEL */
    const char code[] = "\x40\x42\x38\xd5";

    if (!uc) {
        fail("CurrentEL", "uc_open");
        return;
    }
    uc_reg_write(uc, UC_ARM64_REG_X0, &x0);
    if (run_insns(uc, code, sizeof(code) - 1) != UC_ERR_OK) {
        fail("CurrentEL", uc_strerror(uc_errno(uc)));
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 == 0) {
        pass("CurrentEL", "MRS returns EL0");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "got EL=%" PRIu64 " (raw 0x%" PRIx64 ")",
                 x0 >> 2, x0);
        fail("CurrentEL", buf);
    }
    uc_close(uc);
}

static void test_cntfrq(void)
{
    uc_engine *uc = open_uc();
    uint64_t x0 = 0;
    /* mrs x0, CNTFRQ_EL0 */
    const char code[] = "\x00\xe0\x3b\xd5";

    if (!uc) {
        fail("CNTFRQ_EL0", "uc_open");
        return;
    }
    if (run_insns(uc, code, sizeof(code) - 1) != UC_ERR_OK) {
        fail("CNTFRQ_EL0", "MRS trapped");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 == 23000000) {
        pass("CNTFRQ_EL0", "23000000 Hz");
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), "got %" PRIu64 " (Unicorn default is 62500000)",
                 x0);
        fail("CNTFRQ_EL0", buf);
    }
    uc_close(uc);
}

static void test_cntpct(void)
{
    uc_engine *uc = open_uc();
    uint64_t x0 = 0, x1 = 0;
    /* mrs x0, CNTPCT_EL0; mrs x1, CNTPCT_EL0 */
    const char code[] = "\x20\xe0\x3b\xd5\x21\xe0\x3b\xd5";

    if (!uc) {
        fail("CNTPCT_EL0", "uc_open");
        return;
    }
    if (run_insns(uc, code, sizeof(code) - 1) != UC_ERR_OK) {
        fail("CNTPCT_EL0", "MRS trapped");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    uc_reg_read(uc, UC_ARM64_REG_X1, &x1);
    if (x0 != 0 && x1 > x0) {
        char buf[80];
        snprintf(buf, sizeof(buf), "0x%" PRIx64 " -> 0x%" PRIx64, x0, x1);
        pass("CNTPCT_EL0", buf);
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), "x0=0x%" PRIx64 " x1=0x%" PRIx64, x0, x1);
        fail("CNTPCT_EL0", buf);
    }
    uc_close(uc);
}

static void test_midr(void)
{
    uc_engine *uc = open_uc();
    uint64_t x0 = 0;
    /* mrs x0, MIDR_EL1 */
    const char code[] = "\x00\x00\x38\xd5";

    if (!uc) {
        fail("MIDR_EL1", "uc_open");
        return;
    }
    if (run_insns(uc, code, sizeof(code) - 1) != UC_ERR_OK) {
        fail("MIDR_EL1", "MRS trapped");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 == 0x511f8041) {
        pass("MIDR_EL1", "Kryo 4xx Gold 0x511f8041");
    } else {
        char buf[80];
        snprintf(buf, sizeof(buf), "got 0x%" PRIx64 " (A72 was 0x410fd083)", x0);
        fail("MIDR_EL1", buf);
    }
    uc_close(uc);
}

static void test_pacia_retab(void)
{
    uc_engine *uc = open_uc();
    uint64_t x0, orig, x30, sp, pc;
    /* pacia x0, sp */
    const char pacia[] = "\xe0\x03\xc1\xda";
    /* pacib x30, sp; retab; nop */
    const char retab[] = "\xfe\x07\xc1\xda\xff\x0b\x5f\xd6\x1f\x20\x03\xd5";

    if (!uc) {
        fail("PACIA", "uc_open");
        return;
    }

    orig = 0x1000;
    sp = 0x3000;
    uc_reg_write(uc, UC_ARM64_REG_X0, &orig);
    uc_reg_write(uc, UC_ARM64_REG_SP, &sp);
    if (run_insns(uc, pacia, sizeof(pacia) - 1) != UC_ERR_OK) {
        fail("PACIA", "UNDEF / trap");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 == orig) {
        fail("PACIA", "pointer unchanged (NOP / no PAC)");
        uc_close(uc);
        return;
    }
    pass("PACIA", "signed pointer");
    uc_close(uc);

    uc = open_uc();
    if (!uc) {
        fail("RETAB", "uc_open");
        return;
    }
    sp = 0x3000;
    x30 = CODE + 8;
    uc_reg_write(uc, UC_ARM64_REG_SP, &sp);
    uc_reg_write(uc, UC_ARM64_REG_X30, &x30);
    if (run_insns(uc, retab, sizeof(retab) - 1) != UC_ERR_OK) {
        fail("RETAB", "UNDEF / trap / bad return");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
    if (pc == CODE + sizeof(retab) - 1) {
        pass("RETAB", "authenticated return");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "pc=0x%" PRIx64, pc);
        fail("RETAB", buf);
    }
    uc_close(uc);
}

static void test_smc_icache(void)
{
    uc_engine *uc = open_uc();
    uint64_t x0, x1, x2;
    /* add w0, w0, #1 */
    const char orig[] = "\x00\x04\x00\x11";
    /* str w1, [x2] */
    const char store[] = "\x41\x00\x00\xb9";
    /* ic ivau, x0; isb */
    const char flush[] = "\x20\x75\x0b\xd5\xdf\x3f\x03\xd5";
    uint32_t patched = 0x11000800; /* add w0, w0, #2 */

    if (!uc) {
        fail("I-cache", "uc_open");
        return;
    }

    uc_mem_write(uc, CODE, orig, sizeof(orig) - 1);
    uc_mem_write(uc, CODE + 0x100, store, sizeof(store) - 1);
    uc_mem_write(uc, CODE + 0x200, flush, sizeof(flush) - 1);

    x0 = 0;
    uc_reg_write(uc, UC_ARM64_REG_X0, &x0);
    if (uc_emu_start(uc, CODE, (uint64_t)-1, 0, 1) != UC_ERR_OK) {
        fail("I-cache", "first execute failed");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 != 1) {
        fail("I-cache", "first ADD #1 did not run");
        uc_close(uc);
        return;
    }

    x1 = patched;
    x2 = CODE;
    uc_reg_write(uc, UC_ARM64_REG_X1, &x1);
    uc_reg_write(uc, UC_ARM64_REG_X2, &x2);
    if (uc_emu_start(uc, CODE + 0x100, (uint64_t)-1, 0, 1) != UC_ERR_OK) {
        fail("I-cache", "guest STR failed");
        uc_close(uc);
        return;
    }

    x0 = 0;
    uc_reg_write(uc, UC_ARM64_REG_X0, &x0);
    if (uc_emu_start(uc, CODE, (uint64_t)-1, 0, 1) != UC_ERR_OK) {
        fail("I-cache", "stale execute failed");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 != 1) {
        fail("I-cache", "guest store was visible without IC IVAU");
        uc_close(uc);
        return;
    }

    x0 = CODE;
    uc_reg_write(uc, UC_ARM64_REG_X0, &x0);
    if (uc_emu_start(uc, CODE + 0x200, (uint64_t)-1, 0, 2) != UC_ERR_OK) {
        fail("I-cache", "IC IVAU failed");
        uc_close(uc);
        return;
    }

    x0 = 0;
    uc_reg_write(uc, UC_ARM64_REG_X0, &x0);
    if (uc_emu_start(uc, CODE, (uint64_t)-1, 0, 1) != UC_ERR_OK) {
        fail("I-cache", "post-flush execute failed");
        uc_close(uc);
        return;
    }
    uc_reg_read(uc, UC_ARM64_REG_X0, &x0);
    if (x0 == 2) {
        pass("I-cache", "stale until IC IVAU, then new insn");
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "after flush x0=%" PRIu64 " want 2", x0);
        fail("I-cache", buf);
    }
    uc_close(uc);
}

int main(void)
{
    printf("ARM64 Unicorn anti-detect checks\n");
    test_currentel();
    test_cntfrq();
    test_cntpct();
    test_midr();
    test_pacia_retab();
    test_smc_icache();
    printf("%s: %d failed\n", g_fail ? "FAILED" : "OK", g_fail);
    return g_fail ? 1 : 0;
}
