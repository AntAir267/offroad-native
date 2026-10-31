/* Reference build: the original Game.exe machine code run by the Unicorn CPU emulator instead of
   the translated C, on the same runtime (design point 10). Calls into the runtime (imports, COM
   methods) stop the emulator, run the runtime's C code on the shared CPU record, and resume.
   With --test and the same scripted input, build/ht2_ref and build/ht2 must leave the
   same memory every frame (tools/compare.py).

   Unicorn maps the program's memory directly (the image and the arena), so the runtime sees one
   memory either way. Only Unicorn-private pages (the GDT for fs:, the sentinel the outermost
   return lands on) live outside it, so the program-visible memory matches the native build's. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <unicorn/unicorn.h>
#include "runtime.h"
#include "image.h"

#define SENTINEL 0x7efe0000u
#define PRIVATE_BASE 0x7f800000u        /* Unicorn-only page: GDT */

extern const uint32_t x87_special_addr[];
extern const uint8_t x87_special_op[];
extern const int x87_special_count;

typedef struct {
    uc_engine *uc;
    uint32_t fs_base;                   /* what the fs descriptor currently points at */
    void (*pending)(CPU *c);            /* a hooked function reached (HOOK_TRAMP) */
} Engine;

/* Hooked functions (the build config's HOOKS) run as in the translated build: their wrapper (the
   runtime's hook, then the translated original if the hook lets it) instead of the original in
   Unicorn, so both builds do the same there and lockstep compares the rest. Reaching one moves
   the emulation to an unmapped address, where it stops as for a thunk. */
extern const FuncEntry hooked_table[];
extern const int hooked_count;
#define HOOK_TRAMP 0x7efd0000u
static int ref_debug;                   /* HT2_REF_DEBUG: log hooked functions and nested calls */

static __thread Engine *eng;

/* x87 tag words per CPU record (the translated build does not track them) */
#define MAXCPU 64
static CPU *tag_cpu[MAXCPU];
static uint16_t tag_val[MAXCPU];
static pthread_mutex_t tag_lock = PTHREAD_MUTEX_INITIALIZER;

static uint16_t *tag_of(CPU *c)
{
    pthread_mutex_lock(&tag_lock);
    int k;
    for (k = 0; k < MAXCPU && tag_cpu[k] && tag_cpu[k] != c; k++) {}
    if (k == MAXCPU) fatal("reference build: too many threads");
    if (!tag_cpu[k]) {
        tag_cpu[k] = c;
        tag_val[k] = 0xffff;
    }
    pthread_mutex_unlock(&tag_lock);
    return &tag_val[k];
}

/* ------------------------------------------------------------------ x87 transcendentals
   Unicorn's results differ from real hardware in the last bits: run them on the host. */
static void hook_x87(uc_engine *u, uint64_t addr, uint32_t size, void *ud)
{
    uint8_t op = (uint8_t)(uintptr_t)ud;
    uint16_t sw, cw, tag;
    uc_reg_read(u, UC_X86_REG_FPSW, &sw);
    uc_reg_read(u, UC_X86_REG_FPCW, &cw);
    uc_reg_read(u, UC_X86_REG_FPTAG, &tag);
    CPU t;
    memset(&t, 0, sizeof t);
    t.ftop = (sw >> 11) & 7;
    t.fsw = sw & 0x4700;
    for (int i = 0; i < 8; i++) {
        uint8_t v[16] = {0};
        uc_reg_read(u, UC_X86_REG_FP0 + i, v);
        memcpy(&t.st[i], v, 10);
    }
    uint16_t keep_cw = current_cpu() ? current_cpu()->fcw : 0x37f;
    fpu_setcw(&t, cw);
    uint32_t top0 = t.ftop;
    switch (op) {
    case 0xfe: fpu_fsin(&t); break;
    case 0xff: fpu_fcos(&t); break;
    case 0xfb: fpu_fsincos(&t); break;
    case 0xf2: fpu_fptan(&t); break;
    case 0xf3: fpu_fpatan(&t); break;
    case 0xf1: fpu_fyl2x(&t); break;
    case 0xf9: fpu_fyl2xp1(&t); break;
    case 0xf0: fpu_f2xm1(&t); break;
    }
    fpu_setcw(&t, keep_cw);
    for (int i = 0; i < 8; i++) {
        uint8_t v[16] = {0};
        memcpy(v, &t.st[i], 10);
        uc_reg_write(u, UC_X86_REG_FP0 + i, v);
    }
    if (t.ftop != top0) {
        if (((top0 - 1) & 7) == t.ftop) tag &= ~(3u << (2 * t.ftop));
        else tag |= 3u << (2 * top0);
    }
    sw = (uint16_t)((sw & ~0x7f00u) | t.fsw | ((t.ftop & 7) << 11));
    uc_reg_write(u, UC_X86_REG_FPSW, &sw);
    uc_reg_write(u, UC_X86_REG_FPTAG, &tag);
    uint32_t next = (uint32_t)addr + size;
    uc_reg_write(u, UC_X86_REG_EIP, &next);
}

static void hook_entry(uc_engine *u, uint64_t addr, uint32_t size, void *ud)
{
    (void)size;
    if (ref_debug)
        LOG(LOG_ALWAYS, "ref: hooked %08x reached", (uint32_t)addr);
    eng->pending = (void (*)(CPU *c))ud;
    uint32_t t = HOOK_TRAMP;
    uc_reg_write(u, UC_X86_REG_EIP, &t);
}

/* CPUID and RDTSC as the translated build answers them (runtime/x86.c) */
static int hook_cpuid(uc_engine *u, void *ud)
{
    (void)ud;
    CPU t;
    memset(&t, 0, sizeof t);
    uc_reg_read(u, UC_X86_REG_EAX, &t.eax);
    uc_reg_read(u, UC_X86_REG_ECX, &t.ecx);
    x86_cpuid(&t);
    uc_reg_write(u, UC_X86_REG_EAX, &t.eax);
    uc_reg_write(u, UC_X86_REG_EBX, &t.ebx);
    uc_reg_write(u, UC_X86_REG_ECX, &t.ecx);
    uc_reg_write(u, UC_X86_REG_EDX, &t.edx);
    return 1;                           /* the instruction itself is skipped */
}

static int hook_rdtsc(uc_engine *u, void *ud)
{
    (void)ud;
    uint64_t v = x86_rdtsc(current_cpu());
    uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
    uc_reg_write(u, UC_X86_REG_EAX, &lo);
    uc_reg_write(u, UC_X86_REG_EDX, &hi);
    return 1;
}

static bool hook_unmapped(uc_engine *u, uc_mem_type type, uint64_t addr, int size, int64_t value, void *ud)
{
    (void)u; (void)type; (void)addr; (void)size; (void)value; (void)ud;
    return false;                       /* stop: a thunk (runtime call) or a real fault */
}

/* ------------------------------------------------------------------ engine per host thread */
static void set_gdt(uc_engine *uc, int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags)
{
    uint64_t d = (limit & 0xffff) | ((uint64_t)(base & 0xffffff) << 16) | ((uint64_t)access << 40) |
                 ((uint64_t)((limit >> 16) & 0xf) << 48) | ((uint64_t)flags << 52) | ((uint64_t)(base >> 24) << 56);
    uc_mem_write(uc, PRIVATE_BASE + 8 * index, &d, 8);
}

static Engine *engine(void)
{
    if (eng)
        return eng;
    Engine *e = calloc(1, sizeof *e);
    if (uc_open(UC_ARCH_X86, UC_MODE_32, &e->uc))
        fatal("uc_open");
    uc_engine *uc = e->uc;
    uint32_t isz = (IMAGE_SIZE + 0xffff) & ~0xffffu;
    if (uc_mem_map_ptr(uc, IMAGE_BASE, isz, UC_PROT_ALL, (void *)(uintptr_t)IMAGE_BASE) ||
        uc_mem_map_ptr(uc, ARENA_START, ARENA_END - ARENA_START, UC_PROT_ALL, (void *)(uintptr_t)ARENA_START) ||
        uc_mem_map(uc, SENTINEL, 0x1000, UC_PROT_ALL) ||
        uc_mem_map(uc, PRIVATE_BASE, 0x1000, UC_PROT_READ | UC_PROT_WRITE))
        fatal("reference build: cannot map memory into Unicorn");
    uc_hook h;
    uc_hook_add(uc, &h, UC_HOOK_MEM_FETCH_UNMAPPED | UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED,
                hook_unmapped, NULL, 1, 0);
    uc_hook_add(uc, &h, UC_HOOK_INSN, hook_cpuid, NULL, 1, 0, UC_X86_INS_CPUID);
    uc_hook_add(uc, &h, UC_HOOK_INSN, hook_rdtsc, NULL, 1, 0, UC_X86_INS_RDTSC);
    for (int i = 0; i < x87_special_count; i++)
        uc_hook_add(uc, &h, UC_HOOK_CODE, hook_x87, (void *)(uintptr_t)x87_special_op[i],
                    x87_special_addr[i], x87_special_addr[i]);
    for (int i = 0; i < hooked_count; i++)
        uc_hook_add(uc, &h, UC_HOOK_CODE, hook_entry, (void *)hooked_table[i].fn,
                    hooked_table[i].addr, hooked_table[i].addr);
    /* flat code/data (ring 3), fs -> TEB (set per run), stack (ring 0) */
    set_gdt(uc, 14, 0, 0xfffff, 0xfb, 0xc);
    set_gdt(uc, 15, 0, 0xfffff, 0xf3, 0xc);
    set_gdt(uc, 16, 0, 0xfff, 0xf3, 0x4);
    set_gdt(uc, 17, 0, 0xfffff, 0x93, 0xc);
    uc_x86_mmr gdtr = {0, PRIVATE_BASE, 31 * 8 - 1, 0};
    uc_reg_write(uc, UC_X86_REG_GDTR, &gdtr);
    int sel;
    sel = 0x73; uc_reg_write(uc, UC_X86_REG_CS, &sel);
    sel = 0x7b; uc_reg_write(uc, UC_X86_REG_DS, &sel);
    sel = 0x7b; uc_reg_write(uc, UC_X86_REG_ES, &sel);
    sel = 0x88; uc_reg_write(uc, UC_X86_REG_SS, &sel);
    e->fs_base = 0xffffffffu;
    eng = e;
    return e;
}

static const int regid[8] = {UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_EBX,
                             UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_ESI, UC_X86_REG_EDI};

static void to_uc(Engine *e, CPU *c)
{
    uc_engine *uc = e->uc;
    for (int i = 0; i < 8; i++)
        uc_reg_write(uc, regid[i], &(&c->eax)[i]);
    uint32_t fl;
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &fl);
    fl = (fl & ~0x400u) | (c->df ? 0x400u : 0) | 2;
    uc_reg_write(uc, UC_X86_REG_EFLAGS, &fl);
    if (e->fs_base != c->fs_base) {
        set_gdt(uc, 16, c->fs_base, 0xfff, 0xf3, 0x4);
        int sel = 0x83;
        uc_reg_write(uc, UC_X86_REG_FS, &sel);
        e->fs_base = c->fs_base;
    }
    uint16_t cw = c->fcw, sw = (uint16_t)((c->fsw & 0x4700) | ((c->ftop & 7) << 11)), tag = *tag_of(c);
    uc_reg_write(uc, UC_X86_REG_FPCW, &cw);
    uc_reg_write(uc, UC_X86_REG_FPSW, &sw);
    uc_reg_write(uc, UC_X86_REG_FPTAG, &tag);
    for (int i = 0; i < 8; i++) {
        uint8_t v[16] = {0};
        memcpy(v, &c->st[i], 10);
        uc_reg_write(uc, UC_X86_REG_FP0 + i, v);
    }
}

static void from_uc(Engine *e, CPU *c)
{
    uc_engine *uc = e->uc;
    for (int i = 0; i < 8; i++)
        uc_reg_read(uc, regid[i], &(&c->eax)[i]);
    uint32_t fl;
    uc_reg_read(uc, UC_X86_REG_EFLAGS, &fl);
    c->df = (fl >> 10) & 1;
    uint16_t cw, sw, tag;
    uc_reg_read(uc, UC_X86_REG_FPCW, &cw);
    uc_reg_read(uc, UC_X86_REG_FPSW, &sw);
    uc_reg_read(uc, UC_X86_REG_FPTAG, &tag);
    if (cw != c->fcw)
        fpu_setcw(c, cw);
    c->fsw = sw & 0x4700;
    c->ftop = (sw >> 11) & 7;
    *tag_of(c) = tag;
    for (int i = 0; i < 8; i++) {
        uint8_t v[16] = {0};
        uc_reg_read(uc, UC_X86_REG_FP0 + i, v);
        c->st[i] = 0;
        memcpy(&c->st[i], v, 10);
    }
}

static void ref_run(CPU *c, Engine *e, uint32_t target, uint32_t until, uint32_t until_esp);

/* Run the program from `target` until it returns to the sentinel its caller pushed. */
static void ref_icall(CPU *c, uint32_t target)
{
    Engine *e = engine();
    /* from translated code (a hooked function's original, run as in the translated build): a
       runtime function directly; program code until it comes back to the return address the
       translated code pushed, with the stack as the call left it (the stack is not touched: a
       swapped-in sentinel showed in memory while the call ran) */
    if (thunk_name(target)) {
        if (!call_thunk(c, target))
            fatal("reference build: bad thunk %08x", target);
        return;
    }
    uint32_t ra = R32(c->esp);
    if (ref_debug && ra != SENTINEL)
        LOG(LOG_ALWAYS, "ref: translated calls %08x, returns to %08x, call %lu", target, ra, api_calls);
    if (ra == SENTINEL)
        ref_run(c, e, target, SENTINEL, 0);
    else
        ref_run(c, e, target, ra, c->esp + 4);
    c->ret = ra;
}

/* until_esp 0: the first time `until` is reached; else when it is reached with esp at or above
   until_esp (a callee's "ret n" pops its arguments too; a deeper call returning to the same
   address has esp below it and goes on, one instruction at a time past it) */
static void ref_run1(CPU *c, Engine *e, uint32_t target, uint32_t until, uint32_t until_esp);
static void ref_run(CPU *c, Engine *e, uint32_t target, uint32_t until, uint32_t until_esp)
{
    ref_run1(c, e, target, until, until_esp);
    if (until != SENTINEL)
        uc_ctl_remove_cache(e->uc, until, until + 1);
}

static void ref_run1(CPU *c, Engine *e, uint32_t target, uint32_t until, uint32_t until_esp)
{
    uint32_t pc = target;
    int step = 0;
    for (;;) {
        to_uc(e, c);
        /* Unicorn checks `until` only in code it translates afterwards: code at a return address
           in the middle of the program was usually translated before, so its cached block is
           dropped first (and again when done, so the stop does not stay in it) [verified: the
           GRAPHICS constructor's hook ran on in the original code past its first call] */
        if (until != SENTINEL && !step)
            uc_ctl_remove_cache(e->uc, until, until + 1);
        uc_err err = step ? uc_emu_start(e->uc, pc, SENTINEL, 0, 1) : uc_emu_start(e->uc, pc, until, 0, 0);
        uint32_t eip;
        uc_reg_read(e->uc, UC_X86_REG_EIP, &eip);
        from_uc(e, c);
        int was_step = step;
        step = 0;
        if (eip == until) {
            if (!until_esp || c->esp >= until_esp) {
                c->ret = until;         /* the program's ret already popped it */
                return;
            }
            if (ref_debug)
                LOG(LOG_ALWAYS, "ref: %08x reached deeper (esp %08x, want %08x), call %lu", eip, c->esp, until_esp, api_calls);
            pc = eip;
            step = 1;
            continue;
        }
        if (eip == HOOK_TRAMP && e->pending) {
            void (*fn)(CPU *c) = e->pending;
            e->pending = NULL;
            if (ref_debug)
                LOG(LOG_ALWAYS, "ref: hooked function run, esp %08x ret %08x ecx %08x", c->esp, R32(c->esp), c->ecx);
            fn(c);                      /* returns as the program's ret does: c->ret, esp popped */
        } else if (thunk_name(eip)) {
            /* a call (or jump) into the runtime: the return address is on the stack */
            if (!call_thunk(c, eip))
                fatal("reference build: bad thunk %08x", eip);
        } else if (was_step && err == UC_ERR_OK) {
            pc = eip;
            continue;
        } else {
            fatal("reference build: emulation stopped at %08x: %s", eip, uc_strerror(err));
        }
        pc = c->ret;
        if (pc == until && (!until_esp || c->esp >= until_esp))
            return;
    }
}

__attribute__((constructor)) static void ref_init(void)
{
    icall_hook = ref_icall;
    ref_debug = getenv("HT2_REF_DEBUG") != NULL;
}
