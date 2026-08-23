#include "cheats.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <stdint.h>

/* ================================================================================ the primitive
 *
 * Every cheat in this engine is one call. From the debug menu's own handler, eight times, with
 * nothing between them but which string is pushed:
 *
 *     00411C50   mov  ecx,[0x544070]        the command object
 *                push 0x52F630              "fly"
 *                mov  eax,[ecx]             its vtable
 *                call [eax+0x68]            __thiscall (this, const char *command)
 *
 * The strings themselves are in the executable's data, and the game's README documents them as
 * the cheats: fly, drop, tim, mrclean, heal, bye, invisowalls, and "tele %d %d %d".
 *
 * THE STRING IS NOT RETAINED, and that is not an assumption. One of those eight call sites,
 * teleport at 0x411C93, formats into a STACK BUFFER and passes a pointer to it:
 *
 *     lea  eax,[esp+0x20]
 *     push 0x52F618              "tele %d %d %d"
 *     push eax
 *     call 0x504660              sprintf
 *     ...
 *     lea  eax,[esp+0x14]
 *     push eax                   the formatted string, on the stack
 *     call [edx+0x68]
 *
 * A callee that kept that pointer would be reading a dead frame the moment the menu returned.
 * So a string literal from this DLL is safe to pass, which is what makes this module possible
 * without allocating anything inside the game.
 */
#define COMMAND_OBJECT_PTR_VA  0x00544070u
#define COMMAND_VTABLE_SLOT    0x68u

/* Spelled `__fastcall` with a dead second parameter rather than `__thiscall`, which is what the
 * engine actually uses. MSVC accepts `__thiscall` on a function-pointer typedef in C++ but not in
 * C, under `/permissive-` with `C_STANDARD 11` it is not a keyword at all, and the declaration
 * below stops parsing at the `*`. This built once because an older toolset was laxer; 19.50 is
 * not, and no pragma reopens it.
 *
 * The substitution is exact on x86 rather than approximate, which is the only reason it is
 * acceptable here:
 *
 *     __thiscall (this, arg)          this -> ECX, arg on the stack, callee cleans
 *     __fastcall (this, dead, arg)    this -> ECX, dead -> EDX, arg on the stack, callee cleans
 *
 * Same register for `this`, same stack layout for the real argument, same cleanup. EDX is
 * caller-saved and `__thiscall` never reads it, so what we put there is discarded; it is
 * passed as NULL to make that explicit rather than leaving a register uninitialised. */
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

/* WHERE THE CODE LIVES
 *
 * The first build of this refused every call, because it insisted the vtable entry be inside
 * Fellowship.exe. It is not. Nothing in the executable ever WRITES 0x544070, twenty-three
 * instructions read it and none of them assign it, so the object is created by Fellowship.rfl,
 * which is the game half of this engine, and its vtable is in the rfl's code.
 *
 * That is worth stating rather than quietly widening the check: the engine is the exe and the
 * game is the rfl, and any global the exe only ever reads belongs to the other side.
 */
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

/* Nothing here is believed twice. The pointer, the vtable, the slot and the target are each
 * checked on the way through, every time, because this runs on a click at a moment of the
 * player's choosing rather than at a point in the engine's own flow, the same rule the camera
 * work landed on after an unvalidated global crashed a machine that was not this one.
 *
 * `why` gets a word naming the first check that failed, for the one-shot diagnostic below. A
 * button that does nothing has to be able to say which nothing it did.
 */
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
