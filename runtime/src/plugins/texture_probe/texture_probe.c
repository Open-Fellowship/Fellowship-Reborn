#include "texture_probe.h"

#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/module_watch.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "texture_probe"

/* FUN_1006C890, eight instructions past the two size clamps. Everything the draw computed is
 * still live here: the control in esi, and the clipped rectangle and the source spans in stack
 * locals. `push 0xff` is exactly five bytes, so the jump fits without splitting an instruction,
 * which is the only reason this address and not one nearby. */
#define PROBE_RVA        0x6CA5Bu
#define PROBE_RETURN_RVA 0x6CA60u
#define PROBE_SIZE       5u

static const uint8_t probe_expected[PROBE_SIZE] = { 0x68, 0xFF, 0x00, 0x00, 0x00 };

/* The blit itself: `call [edx+0x5c]` and the `mov edx,[edi]` after it, five bytes and two whole
 * instructions. Eleven arguments are on the stack at this point, and which of them is the
 * DESTINATION rectangle and which is the SOURCE is the one thing still unmeasured. The engine
 * keeps a single width for both up to here, so the separation, if there is one, happens in this
 * argument list. */
#define BLIT_RVA        0x6CBA2u
#define BLIT_RETURN_RVA 0x6CBA7u
#define BLIT_SIZE       5u

static const uint8_t blit_expected[BLIT_SIZE] = { 0xFF, 0x52, 0x5C, 0x8B, 0x17 };

#define BLIT_ARGS 14
#define BLIT_ROWS 8

typedef struct blit_row {
    uint32_t arg[BLIT_ARGS];
    uint32_t hits;
} blit_row_t;

static blit_row_t g_blit[BLIT_ROWS];
static int        g_blit_count;

/* Control fields, from the constructor and FUN_1006C750. */
#define F_POS_X   0x38u
#define F_POS_Y   0x3Cu
#define F_DST_W   0x40u
#define F_DST_H   0x44u
#define F_PARENT  0x5Cu
#define F_TEXTURE 0x64u
#define F_SRC_X   0x68u
#define F_SRC_Y   0x6Cu
#define F_SRC_W   0x70u
#define F_SRC_H   0x74u

#define STACK_WORDS 24
#define TABLE_SIZE  64

typedef struct entry {
    uintptr_t control;
    uintptr_t texture;
    uint32_t  hits;
    float     pos_x, pos_y, dst_w, dst_h;
    float     src_x, src_y, src_w, src_h;
    int32_t   tex_w, tex_h;
    uint32_t  stack[STACK_WORDS];
} entry_t;

static entry_t       g_table[TABLE_SIZE];
static int           g_count;
static volatile LONG g_recording;
static int           g_dump_key = VK_F3;

static float as_float(uint32_t bits)
{
    float f;

    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* Every read goes through this. A probe that faults is worse than no probe, and this one walks
 * three pointer hops: the control, the texture it names, and the stack behind it. The first
 * version dereferenced all three unguarded and took the game down on the first draw. */
static uint32_t read_u32(uintptr_t address)
{
    uint32_t value = 0;

    if (!memory_is_readable_range(address, sizeof(value))) {
        return 0;
    }
    memcpy(&value, (const void *)address, sizeof(value));
    return value;
}

/* __cdecl, called from the stub with the control and the stack pointer as it was before the
 * stub touched anything. Reads only. One row per distinct control, so a control drawn every
 * frame does not fill the table. */
static void __cdecl record(uintptr_t control, uintptr_t stack)
{
    entry_t  *e;
    uintptr_t texture;
    int       i;

    if (InterlockedCompareExchange(&g_recording, 1, 1) == 0 || control == 0) {
        return;
    }
    /* The whole control at once, before any field is touched. esi is the object on the path this
     * hook sits on, but nothing guarantees every caller that reaches here agrees. */
    if (!memory_is_readable_range(control, 0x78u)) {
        return;
    }

    for (i = 0; i < g_count; ++i) {
        if (g_table[i].control == control) {
            g_table[i].hits++;
            return;
        }
    }
    if (g_count >= TABLE_SIZE) {
        return;
    }

    e = &g_table[g_count++];
    e->control = control;
    e->hits    = 1;
    e->pos_x   = as_float(read_u32(control + F_POS_X));
    e->pos_y   = as_float(read_u32(control + F_POS_Y));
    e->dst_w   = as_float(read_u32(control + F_DST_W));
    e->dst_h   = as_float(read_u32(control + F_DST_H));
    e->src_x   = as_float(read_u32(control + F_SRC_X));
    e->src_y   = as_float(read_u32(control + F_SRC_Y));
    e->src_w   = as_float(read_u32(control + F_SRC_W));
    e->src_h   = as_float(read_u32(control + F_SRC_H));

    /* The texture object carries its own dimensions at [10] and [11], which is what the two
     * clamps compare against. Guarded, because a control can be drawn before its texture is. */
    texture = (uintptr_t)read_u32(control + F_TEXTURE);
    e->texture = texture;
    e->tex_w   = 0;
    e->tex_h   = 0;
    if (texture != 0 && memory_is_readable_range(texture, 48u)) {
        e->tex_w = (int32_t)read_u32(texture + 40u);
        e->tex_h = (int32_t)read_u32(texture + 44u);
    }

    if (memory_is_readable_range(stack, (size_t)STACK_WORDS * 4u)) {
        for (i = 0; i < STACK_WORDS; ++i) {
            e->stack[i] = read_u32(stack + (uintptr_t)i * 4u);
        }
    }
}

/* Called with the stack pointer as the game had it at the call, so [esp+0] is the first argument
 * pushed last. Reads only. */
static void __cdecl record_blit(uintptr_t args)
{
    blit_row_t *r;
    uint32_t    tmp[BLIT_ARGS];
    int         i;
    int         j;

    if (InterlockedCompareExchange(&g_recording, 1, 1) == 0) {
        return;
    }
    if (!memory_is_readable_range(args, (size_t)BLIT_ARGS * 4u)) {
        return;
    }
    for (i = 0; i < BLIT_ARGS; ++i) {
        tmp[i] = read_u32(args + (uintptr_t)i * 4u);
    }

    for (i = 0; i < g_blit_count; ++i) {
        int same = 1;

        for (j = 0; j < BLIT_ARGS; ++j) {
            if (g_blit[i].arg[j] != tmp[j]) {
                same = 0;
                break;
            }
        }
        if (same) {
            g_blit[i].hits++;
            return;
        }
    }
    if (g_blit_count >= BLIT_ROWS) {
        return;
    }
    r = &g_blit[g_blit_count++];
    memcpy(r->arg, tmp, sizeof(tmp));
    r->hits = 1;
}

/* Every value is printed as BOTH a float and an integer, because which of the two a local holds
 * is the thing being established and guessing it is how the last three attempts went wrong. */
static void dump(void)
{
    int i;
    int w;

    log_info("---- GUI texture draws seen, newest run ----");
    if (g_count == 0) {
        log_info("  nothing recorded. Press the key, then open a screen with art on it.");
        return;
    }

    for (i = 0; i < g_count; ++i) {
        const entry_t *e = &g_table[i];

        log_info("control %08X  texture %08X  hits %u",
                 (unsigned)e->control, (unsigned)e->texture, (unsigned)e->hits);
        log_info("   pos    %.1f, %.1f", (double)e->pos_x, (double)e->pos_y);
        log_info("   dst    %.1f x %.1f      (the +0x40/+0x44 pair)",
                 (double)e->dst_w, (double)e->dst_h);
        log_info("   src    at %.1f, %.1f  size %.1f x %.1f",
                 (double)e->src_x, (double)e->src_y, (double)e->src_w, (double)e->src_h);
        log_info("   texture %d x %d          (what the two clamps compare against)",
                 (int)e->tex_w, (int)e->tex_h);

        for (w = 0; w < STACK_WORDS; w += 4) {
            log_info("   esp+%02X  %12.3f %12.3f %12.3f %12.3f",
                     (unsigned)(w * 4),
                     (double)as_float(e->stack[w]), (double)as_float(e->stack[w + 1]),
                     (double)as_float(e->stack[w + 2]), (double)as_float(e->stack[w + 3]));
            log_info("           %12ld %12ld %12ld %12ld",
                     (long)(int32_t)e->stack[w], (long)(int32_t)e->stack[w + 1],
                     (long)(int32_t)e->stack[w + 2], (long)(int32_t)e->stack[w + 3]);
        }
    }
    log_info("---- %d distinct controls ----", g_count);

    log_info("---- blit argument lists, rfl+%X ----", BLIT_RVA);
    for (i = 0; i < g_blit_count; ++i) {
        const blit_row_t *r = &g_blit[i];

        log_info("blit %d, hits %u", i, (unsigned)r->hits);
        for (w = 0; w < BLIT_ARGS; ++w) {
            log_info("   arg%-2d  esp+%02X  %14.3f   %12ld   %08X",
                     w, (unsigned)(w * 4), (double)as_float(r->arg[w]),
                     (long)(int32_t)r->arg[w], (unsigned)r->arg[w]);
        }
    }
    log_info("---- %d distinct blits ----", g_blit_count);
}

static DWORD WINAPI key_thread(LPVOID parameter)
{
    bool was_down = false;

    (void)parameter;
    for (;;) {
        bool down = (GetAsyncKeyState(g_dump_key) & 0x8000) != 0;

        if (down && !was_down) {
            if (InterlockedCompareExchange(&g_recording, 0, 1) == 1) {
                dump();
                log_info("recording stopped. Press the key again to clear and start over.");
            } else {
                g_count = 0;
                g_blit_count = 0;
                InterlockedExchange(&g_recording, 1);
                log_info("recording started. Open the screen you want to see, then press the "
                         "key again.");
            }
        }
        was_down = down;
        Sleep(30);
    }
}

static void *build_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    /* esp as the game had it, past the 32 bytes of pushad and the 4 of pushfd. */
    emit_u8(&emit, 0x8D); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x24);
    emit_u8(&emit, 0x50);                                    /* push eax  (the stack)        */
    emit_u8(&emit, 0x56);                                    /* push esi  (the control)      */
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&record -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x08);  /* add esp,8  cdecl   */
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */
    emit_bytes(&emit, probe_expected, PROBE_SIZE);           /* the relocated push 0xff      */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

/* Saves everything, reads the argument list, restores, then performs the two instructions it
 * displaced and returns. The game's stack is exactly as it was when the call happens. */
static void *build_blit_stub(uintptr_t stub_address, uintptr_t return_address)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                    /* pushad                       */
    emit_u8(&emit, 0x9C);                                    /* pushfd                       */
    emit_u8(&emit, 0x8D); emit_u8(&emit, 0x44); emit_u8(&emit, 0x24); emit_u8(&emit, 0x24);
    emit_u8(&emit, 0x50);                                    /* push eax  (the argument list)*/
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)&record_blit -
                               (stub_address + (uintptr_t)emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x04);
    emit_u8(&emit, 0x9D);                                    /* popfd                        */
    emit_u8(&emit, 0x61);                                    /* popad                        */
    emit_bytes(&emit, blit_expected, BLIT_SIZE);             /* the call, and the mov after  */
    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void on_rfl_loaded(uintptr_t rfl_base)
{
    uintptr_t site = rfl_site(rfl_base, PROBE_RVA);
    uintptr_t blit = rfl_site(rfl_base, BLIT_RVA);
    uintptr_t blit_stub;
    uintptr_t stub_address;
    HANDLE    thread;

    if (!patch_validate_bytes(site, probe_expected, PROBE_SIZE)) {
        log_error("rfl+%X does not hold the expected push, not installing", PROBE_RVA);
        return;
    }
    stub_address = (uintptr_t)trampoline_alloc(64);
    if (stub_address == 0) {
        log_error("could not allocate the stub");
        return;
    }
    if (build_stub(stub_address, rfl_site(rfl_base, PROBE_RETURN_RVA)) == NULL) {
        log_error("the stub did not fit its buffer, not installing");
        return;
    }
    if (patch_write_jump(site, (const void *)stub_address, PROBE_SIZE) != PATCH_RESULT_OK) {
        log_error("could not branch to the stub");
        return;
    }

    /* The blit hook is a bonus: if its site does not match, the first one still works. */
    if (patch_validate_bytes(blit, blit_expected, BLIT_SIZE)) {
        blit_stub = (uintptr_t)trampoline_alloc(64);
        if (blit_stub != 0 &&
            build_blit_stub(blit_stub, rfl_site(rfl_base, BLIT_RETURN_RVA)) != NULL &&
            patch_write_jump(blit, (const void *)blit_stub, BLIT_SIZE) == PATCH_RESULT_OK) {
            log_info("  and rfl+%X -> stub at %08X, the blit argument list",
                     BLIT_RVA, (unsigned)blit_stub);
        } else {
            log_warning("the blit hook could not be installed; the control dump still works");
        }
    } else {
        log_warning("rfl+%X is not the blit this expects; the control dump still works",
                    BLIT_RVA);
    }

    thread = CreateThread(NULL, 0, key_thread, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    }

    log_info("installed: rfl+%X -> stub at %08X. Recording is OFF; press key %d to start, again "
             "to stop and print.", PROBE_RVA, (unsigned)stub_address, g_dump_key);
}

void texture_probe_install(void)
{
    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0. A diagnostic that changes nothing, turn it on only when hunting "
                 "something.");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }
    g_dump_key = (int)ini_read_int(PLUGIN_SECTION, "DumpKey", VK_F3);

    if (!module_watch_when_loaded(FELLOWSHIP_RFL_MODULE, on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}
