#include "hud_probe.h"
#include "common/compiler.h"

#include "common/emit.h"
#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/patch.h"
#include "common/trampoline.h"

#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PLUGIN_SECTION "hud_probe"

#define GETTER_VA     0x0044E6E0u
#define GETTER_RETURN 0x0044E6E6u
#define GETTER_SIZE   6u

/* TEMPORARY. The second getter, belonging to the sparse override subclass, which HUD-FINDING
 * names as a known gap in this tool. A class can be initialised through the first getter and
 * then read entirely through this one, which is what a report full of defaults and empty of
 * consumers looks like.
 *
 *     0044EFD0  53              push ebx
 *     0044EFD1  8B 5C 24 08     mov ebx,[esp+8]
 *
 * Five bytes, and its override count is at this+0x10 rather than this+4. */
#define SPARSE_VA     0x0044EFD0u
#define SPARSE_RETURN 0x0044EFD5u
#define SPARSE_SIZE   5u

static const uint8_t sparse_expected[SPARSE_SIZE] = {
    0x53,                          /* push ebx           */
    0x8B, 0x5C, 0x24, 0x08         /* mov ebx,[esp+8]    */
};

static const uint8_t getter_expected[GETTER_SIZE] = {
    0x56,                          /* push esi           */
    0x57,                          /* push edi           */
    0x8B, 0x7C, 0x24, 0x10         /* mov edi,[esp+0x10] */
};

#define TABLE_SIZE 2048

typedef struct entry {
    volatile uint32_t caller;
    uint32_t          index;
    uint32_t          nprops;
    uint32_t          kind;        /* 0 the flat getter, 1 the sparse override getter */
    volatile uint32_t hits;
} entry_t;

static entry_t g_table[TABLE_SIZE];
static volatile LONG g_recording;
static int  g_dump_key = VK_F3;

/* __cdecl, because the stub pushes every argument and cleans up after itself.
 *
 * TEMPORARY: nprops is the object's element count, at this+4. On its own it is a weak
 * discriminator, and HUD-FINDING records that it collides for 26 of 86 float properties. Paired
 * with an index it is strong enough to tell one class from another in a report, which is all it
 * is being asked to do here. */
static void __cdecl record(uint32_t caller, uint32_t index, uint32_t nprops, uint32_t kind)
{
    uint32_t slot;

    if (!InterlockedCompareExchange(&g_recording, 0, 0)) {
        return;
    }

    slot = (caller * 2654435761u + index * 40503u + nprops * 97u + kind * 7919u)
           & (TABLE_SIZE - 1u);

    /* One probe, no chain. A collision means the pair is not recorded, which is acceptable for a
     * report and is the only way to keep this to a handful of instructions. */
    if (g_table[slot].caller == 0) {
        g_table[slot].caller = caller;
        g_table[slot].index  = index;
        g_table[slot].nprops = nprops;
        g_table[slot].kind   = kind;
    }
    if (g_table[slot].caller == caller && g_table[slot].index == index &&
        g_table[slot].nprops == nprops && g_table[slot].kind == kind) {
        g_table[slot].hits++;
    }
}

static void *build_stub(uintptr_t stub_address, uintptr_t return_address,
                        unsigned kind, unsigned count_offset,
                        const uint8_t *relocated, unsigned relocated_len)
{
    uint8_t buffer[64];
    emit_t  emit;

    emit_init(&emit, buffer, sizeof(buffer));

    emit_u8(&emit, 0x60);                                        /* pushad               */
    emit_u8(&emit, 0x9C);                                        /* pushfd               */
    emit_u8(&emit, 0x6A); emit_u8(&emit, (uint8_t)kind);         /* push kind            */
    /* ecx is the object, untouched at the entry, and the getter dereferences it itself. */
    emit_u8(&emit, 0x8B); emit_u8(&emit, 0x41); emit_u8(&emit, (uint8_t)count_offset);
    emit_u8(&emit, 0x50);                                        /* push eax, the count  */
    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x74); emit_u8(&emit, 0x24); emit_u8(&emit, 0x30);
    emit_u8(&emit, 0xFF); emit_u8(&emit, 0x74); emit_u8(&emit, 0x24); emit_u8(&emit, 0x30);
    emit_u8(&emit, 0xE8);
    emit_u32(&emit, (uint32_t)((uintptr_t)record - (stub_address + emit_size(&emit) + 4u)));
    emit_u8(&emit, 0x83); emit_u8(&emit, 0xC4); emit_u8(&emit, 0x10);   /* add esp,0x10   */
    emit_u8(&emit, 0x9D);                                        /* popfd                */
    emit_u8(&emit, 0x61);                                        /* popad                */

    {
        unsigned i;

        for (i = 0; i < relocated_len; ++i) {
            emit_u8(&emit, relocated[i]);
        }
    }

    emit_jump_rel32(&emit, stub_address, return_address);

    if (emit_overflowed(&emit)) {
        return NULL;
    }
    memcpy((void *)stub_address, buffer, emit_size(&emit));
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)stub_address, emit_size(&emit));
    return (void *)stub_address;
}

static void dump(void)
{
    uintptr_t exe_low  = host_image_base();
    uintptr_t exe_high = host_image_end();
    HMODULE   rfl      = GetModuleHandleA(FELLOWSHIP_RFL_MODULE);
    int       shown    = 0;
    int       index;

    log_info("---- property reads seen, newest run ----");
    log_info("  %-24s %-6s %-7s %-7s %s", "caller", "index", "count", "getter", "hits");

    for (index = 0; index < TABLE_SIZE; ++index) {
        entry_t  *e = &g_table[index];
        uint32_t  caller = e->caller;
        char      where[64];

        if (caller == 0 || e->hits == 0) {
            continue;
        }
        if (rfl != NULL && caller >= (uintptr_t)rfl && caller < (uintptr_t)rfl + 0x160000u) {
            sprintf(where, "Fellowship.rfl+%X", (unsigned)(caller - (uintptr_t)rfl));
        } else if (caller >= exe_low && caller < exe_high) {
            sprintf(where, "Fellowship.exe+%X", (unsigned)(caller - exe_low));
        } else {
            sprintf(where, "%08X", (unsigned)caller);
        }

        log_info("  %-24s %-6u %-7u %-7s %u", where, (unsigned)e->index,
                 (unsigned)e->nprops, e->kind ? "sparse" : "flat", (unsigned)e->hits);
        ++shown;
    }

    log_info("---- %d distinct (caller, index) pairs ----", shown);
}

OF_NORETURN_THREAD_BEGIN
static DWORD WINAPI key_thread(LPVOID parameter)
{
    bool previous = false;

    (void)parameter;

    for (;;) {
        bool pressed = (GetAsyncKeyState(g_dump_key) & 0x8000) != 0;

        if (pressed && !previous) {
            if (InterlockedCompareExchange(&g_recording, 0, 0)) {
                InterlockedExchange(&g_recording, 0);
                dump();
                log_info("recording stopped. Press the key again to clear and start over.");
            } else {
                memset(g_table, 0, sizeof(g_table));
                InterlockedExchange(&g_recording, 1);
                log_info("recording started, do the thing you want to see, then press the key "
                         "again to write the report");
            }
        }
        previous = pressed;
        Sleep(40);
    }

    return 0;
}
OF_NORETURN_THREAD_END

void hud_probe_install(void)
{
    uintptr_t      site;
    uintptr_t      stub;
    HANDLE         thread;
    patch_result_t result;
    int32_t        configured;

    log_init(PLUGIN_SECTION, false);

    /* OFF by default, on purpose. This is a diagnostic that hooks the busiest function in the
     * engine; nobody should be running it without meaning to. */
    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", false)) {
        log_info("Enabled=0. A diagnostic, not a fix, turn it on only when hunting something.");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    configured = ini_read_int(PLUGIN_SECTION, "DumpKey", VK_F2);
    if (configured > 0 && configured < 256) {
        g_dump_key = configured;
    }

    site = exe_site(GETTER_VA);
    if (!patch_validate_bytes(site, getter_expected, GETTER_SIZE)) {
        log_error("%08X is not the property getter on this build, not installing",
                  (unsigned)GETTER_VA);
        return;
    }

    stub = (uintptr_t)trampoline_alloc(64);
    if (stub == 0 || build_stub(stub, exe_site(GETTER_RETURN), 0u, 0x04u,
                                getter_expected, GETTER_SIZE) == NULL) {
        log_error("could not build the stub");
        return;
    }

    result = patch_write_jump(site, (const void *)stub, GETTER_SIZE);
    if (result != PATCH_RESULT_OK) {
        log_error("could not branch to the stub, %s", patch_result_text(result));
        return;
    }

    /* Best effort, and separate from the getter above: a class can be initialised through one
     * and then read entirely through the other, which looks in a report exactly like a class
     * nothing consumes. Without this hook that reads as a dead end rather than a blind spot. */
    {
        uintptr_t sparse = exe_site(SPARSE_VA);

        if (patch_validate_bytes(sparse, sparse_expected, SPARSE_SIZE)) {
            uintptr_t sparse_stub = (uintptr_t)trampoline_alloc(64);

            if (sparse_stub != 0 &&
                build_stub(sparse_stub, exe_site(SPARSE_RETURN), 1u, 0x10u,
                           sparse_expected, SPARSE_SIZE) != NULL &&
                patch_write_jump(sparse, (const void *)sparse_stub,
                                 SPARSE_SIZE) == PATCH_RESULT_OK) {
                log_info("  and %08X, the sparse override getter", (unsigned)SPARSE_VA);
            } else {
                log_warning("the sparse override getter could not be hooked");
            }
        } else {
            log_warning("%08X is not the sparse override getter on this build",
                        (unsigned)SPARSE_VA);
        }
    }

    thread = CreateThread(NULL, 0, key_thread, NULL, 0, NULL);
    if (thread != NULL) {
        CloseHandle(thread);
    }

    log_info("installed: %08X -> stub at %08X. Recording is OFF; press key %d to start, again "
             "to write the report to this log.", (unsigned)GETTER_VA, (unsigned)stub, g_dump_key);
}
