#include "cheats.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdint.h>

/* Every cheat is one call: a command string on the stack, one vtable slot on the object at
 * 0x544070. THE STRING IS NOT RETAINED, which is not an assumption: the teleport site formats
 * into a stack buffer and passes a pointer to it, so a callee that kept the pointer would be
 * reading a dead frame. A string literal from this DLL is therefore safe. See README.md. */
#define COMMAND_OBJECT_PTR_VA  0x00544070u
#define COMMAND_VTABLE_SLOT    0x68u

/* __fastcall with a dead second parameter, NOT __thiscall: the latter is not usable on a
 * function-pointer typedef in C. The substitution is exact on x86. See common/compiler.h. */
typedef void (__fastcall *command_fn)(void *self, void *unused_edx, const char *command);

typedef struct cheat {
    const char *label;
    const char *command;
    bool        is_toggle;
} cheat_t;

/* is_toggle is taken from the engine, not from taste: in the debug menu's handler, `fly` and
 * `tim` are the only two entries that flip a displayed state beside them,
 *
 *     xor ecx,ecx / test esi,esi / sete cl / mov esi,ecx
 *
 * and the other six just fire. */
static const cheat_t g_cheats[CHEAT_COUNT] = {
    { "Fly",            "fly",         true  },
    { "Invincible",     "tim",         true  },
    { "Kill enemies",   "mrclean",     false },
    { "Full health",    "heal",        false },
    { "Drop",           "drop",        false },
    { "Invisible walls","invisowalls", false },
    { "Suicide",        "bye",         false }
};

static bool g_believed[CHEAT_COUNT];

const char *cheat_label(cheat_id_t id)
{
    return (id >= 0 && id < CHEAT_COUNT) ? g_cheats[id].label : "";
}

bool cheat_is_toggle(cheat_id_t id)
{
    return (id >= 0 && id < CHEAT_COUNT) && g_cheats[id].is_toggle;
}

bool cheat_believed_state(cheat_id_t id)
{
    return (id >= 0 && id < CHEAT_COUNT) && g_believed[id];
}

/* The vtable entry lives in Fellowship.rfl, NOT the executable. The first build required the
 * exe and refused every call. Any global the exe only ever reads belongs to the other side.
 * See README.md. */
static bool module_range(const char *name, uintptr_t *base, uintptr_t *end)
{
    HMODULE              module = GetModuleHandleA(name);
    IMAGE_DOS_HEADER     dos;
    IMAGE_NT_HEADERS32   nt;

    if (module == NULL) {
        return false;
    }
    if (!memory_read((uintptr_t)module, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    if (dos.e_lfanew <= 0 || (uintptr_t)dos.e_lfanew > 0x10000u) {
        return false;
    }
    if (!memory_read((uintptr_t)module + (uintptr_t)dos.e_lfanew, &nt, sizeof(nt)) ||
        nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    *base = (uintptr_t)module;
    *end  = (uintptr_t)module + nt.OptionalHeader.SizeOfImage;
    return true;
}

static bool inside_the_game(uintptr_t address)
{
    uintptr_t base;
    uintptr_t end;

    if (memory_is_inside_image(address, 1)) {
        return true;                       /* Fellowship.exe, the engine */
    }
    if (module_range("Fellowship.rfl", &base, &end)) {
        return address >= base && address < end;
    }
    return false;
}

/* Nothing here is believed twice: pointer, vtable, slot and target are each checked on every
 * call, because this runs on a click rather than at a point in the engine's own flow. `why`
 * names the first check that failed, so a button that does nothing can say which nothing. */
static command_fn resolve_command(void **object_out, const char **why)
{
    uintptr_t object = 0;
    uintptr_t vtable = 0;
    uintptr_t entry  = 0;

    *why = "";

    if (!host_image_resolve()) {
        *why = "the host image is not resolved";
        return NULL;
    }
    if (!memory_read_u32(exe_site(COMMAND_OBJECT_PTR_VA), (uint32_t *)&object)) {
        *why = "0x544070 could not be read";
        return NULL;
    }
    if (object == 0) {
        *why = "0x544070 is still NULL; the game has not built the object yet";
        return NULL;
    }
    if (!memory_is_readable_range(object, 4)) {
        *why = "0x544070 does not point at readable memory";
        return NULL;
    }
    if (!memory_read_u32(object, (uint32_t *)&vtable) || vtable == 0) {
        *why = "the object has no vtable pointer";
        return NULL;
    }
    if (!memory_is_readable_range(vtable, COMMAND_VTABLE_SLOT + 4u)) {
        *why = "the vtable is not readable as far as +0x68";
        return NULL;
    }
    if (!memory_read_u32(vtable + COMMAND_VTABLE_SLOT, (uint32_t *)&entry) || entry == 0) {
        *why = "vtable slot +0x68 is empty";
        return NULL;
    }
    if (!inside_the_game(entry)) {
        *why = "vtable slot +0x68 points outside Fellowship.exe and Fellowship.rfl";
        return NULL;
    }

    *object_out = (void *)object;
    return (command_fn)entry;
}

/* Logged once, not once a frame: a dev tool that cannot do the thing has to say why, and a dev
 * tool that says it sixty times a second is a dev tool nobody reads the log of. */
static void explain_once(const char *why)
{
    static const char *last;

    if (why == NULL || *why == '\0' || why == last) {
        return;
    }
    last = why;
    log_info("cheats unavailable: %s", why);
}

bool cheats_available(void)
{
    static bool announced;

    void       *object  = NULL;
    const char *why     = "";
    command_fn  command = resolve_command(&object, &why);

    if (command == NULL) {
        explain_once(why);
        return false;
    }

    /* Once, the first time it works: the three numbers anyone would ask for if a button turned
     * out to do nothing. Cheap to print now, impossible to reconstruct from a bug report later. */
    if (!announced) {
        uint32_t vtable = 0;
        announced = true;
        memory_read_u32((uintptr_t)object, &vtable);
        log_info("cheats ready: object %08X  vtable %08X  command %08X",
                 (unsigned)(uintptr_t)object, (unsigned)vtable, (unsigned)(uintptr_t)command);
    }

    return true;
}

bool cheat_send(cheat_id_t id)
{
    void       *object = NULL;
    const char *why    = "";
    command_fn  command;

    if (id < 0 || id >= CHEAT_COUNT) {
        return false;
    }

    command = resolve_command(&object, &why);
    if (command == NULL) {
        log_warning("%s did nothing: %s", g_cheats[id].label, why);
        return false;
    }

    command(object, NULL, g_cheats[id].command);   /* NULL lands in EDX and is discarded */

    if (g_cheats[id].is_toggle) {
        g_believed[id] = !g_believed[id];
    }

    log_info("%s -> \"%s\"%s", g_cheats[id].label, g_cheats[id].command,
             g_cheats[id].is_toggle ? (g_believed[id] ? "  (now on)" : "  (now off)") : "");

    return true;
}
