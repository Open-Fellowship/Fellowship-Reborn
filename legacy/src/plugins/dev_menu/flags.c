#include "flags.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

/* 0x5449A8 IS the object, in the executable's own data, with names at +0xD4 and values at
 * +0xE0.
 *
 * ALWAYS CALL THE SETTER, never write the values array: the setter carries a jump table of
 * per-flag side effects. Flag 13 sets two others, the screenshot flag takes a screenshot, the
 * cache flags flush caches. Writing the number directly sets the number and does none of the
 * work. See README.md. */
#define FLAG_OBJECT_VA   0x005449A8u
#define FLAG_NAMES_AT    0xD4u
#define FLAG_VALUES_AT   0xE0u
#define FLAG_SETTER_VA   0x00411800u

/* `__fastcall` with a dead EDX parameter, standing in for the engine's `__thiscall`.
 * MSVC does not accept `__thiscall` on a function-pointer typedef in C; the substitution is
 * exact on x86, `this` in ECX either way, the real arguments in the same stack slots, callee
 * cleanup either way, and EDX is caller-saved and never read. See the longer note in
 * cheats.c. */
typedef void (__fastcall *set_flag_fn)(void *self, void *unused_edx, int index, int32_t value);

/* 99, 100 and 101 are X, Y and Z: the destination the Teleport entry (98) reads when it builds
 * "tele %d %d %d". They are the only entries here holding a number that means something in the
 * world rather than a mode, and toggling one between 0 and 1, which is what the dispatcher's
 * default case does to them, throws the coordinate away. They get typed into instead. */
static const int g_numbers[] = { 99, 100, 101 };

/* THE TELEPORT COORDINATES ARE NOT IN THE VALUES ARRAY. Setting values[99..101] does nothing.
 * Teleport reads three fields out of the flag object itself, at +0x114, +0x118 and +0x11C, which
 * are X, Y and Z in that order. See README.md. */
static const uint32_t g_number_field[] = { 0x00544ABCu, 0x00544AC0u, 0x00544AC4u };

static uint32_t number_address(int index)
{
    size_t i;

    for (i = 0; i < sizeof(g_numbers) / sizeof(g_numbers[0]); ++i) {
        if (g_numbers[i] == index) { return g_number_field[i]; }
    }
    return 0;
}

static bool arrays(uintptr_t *names, uintptr_t *values)
{
    uintptr_t object;

    if (!host_image_resolve()) {
        return false;
    }

    object = exe_site(FLAG_OBJECT_VA);
    if (!memory_is_readable_range(object, FLAG_VALUES_AT + 4u)) {
        return false;
    }
    if (!memory_read_u32(object + FLAG_NAMES_AT, (uint32_t *)names) ||
        !memory_read_u32(object + FLAG_VALUES_AT, (uint32_t *)values)) {
        return false;
    }
    if (*names == 0 || *values == 0) {
        return false;                         /* before the registration at 0x40FE30 has run */
    }
    return memory_is_readable_range(*names, FLAG_COUNT * 4u) &&
           memory_is_readable_range(*values, FLAG_COUNT * 4u);
}

bool flags_available(void)
{
    static bool announced;

    uintptr_t names  = 0;
    uintptr_t values = 0;

    if (!arrays(&names, &values)) {
        return false;
    }

    if (!announced) {
        announced = true;
        log_info("engine flags ready: %d entries, names %08X values %08X",
                 FLAG_COUNT, (unsigned)names, (unsigned)values);
    }
    return true;
}

const char *flags_name(int index)
{
    uintptr_t names  = 0;
    uintptr_t values = 0;
    uintptr_t text   = 0;
    uint8_t   first  = 0;

    if (index < 0 || index >= FLAG_COUNT || !arrays(&names, &values)) {
        return NULL;
    }
    if (!memory_read_u32(names + (uintptr_t)index * 4u, (uint32_t *)&text) || text == 0) {
        return NULL;                          /* 71, 95, 96, 99-101: registered, never named */
    }
    /* One readable byte, and it has to look like text. A name array with a hole in it is normal
     * here, and a hole must read as "no name" rather than as a string at address four. */
    if (!memory_read_u8(text, &first) || first < 0x20u || first > 0x7Eu) {
        return NULL;
    }
    return (const char *)text;
}

bool flags_value(int index, int32_t *out)
{
    uintptr_t names  = 0;
    uintptr_t values = 0;
    uint32_t  field;

    if (index < 0 || index >= FLAG_COUNT || out == NULL || !arrays(&names, &values)) {
        return false;
    }

    /* The teleport coordinates are read from where Teleport actually reads them, so the field
     * shows the number that will be used rather than a parallel one nothing looks at. */
    field = number_address(index);
    if (field != 0) {
        return memory_read_u32(exe_site(field), (uint32_t *)out);
    }

    return memory_read_u32(values + (uintptr_t)index * 4u, (uint32_t *)out);
}

bool flags_set(int index, int32_t value)
{
    uintptr_t   names  = 0;
    uintptr_t   values = 0;
    set_flag_fn setter;

    if (index < 0 || index >= FLAG_COUNT || !arrays(&names, &values)) {
        return false;
    }

    /* Same for writing: into the object's own field, not the values array, or Teleport builds its
     * command from whatever was there before. No setter call, because there is no per-flag side
     * effect for these; the coordinate is read straight out of the object when Teleport runs. */
    {
        uint32_t field = number_address(index);

        if (field != 0) {
            uintptr_t site = exe_site(field);

            if (!memory_make_writable(site, sizeof(int32_t))) {
                return false;
            }
            *(volatile int32_t *)site = value;
            log_info("flag %d %s = %ld (into the object at %08X, where Teleport reads it)",
                     index, flags_name(index) ? flags_name(index) : "(unnamed)", (long)value,
                     (unsigned)field);
            return true;
        }
    }

    setter = (set_flag_fn)exe_site(FLAG_SETTER_VA);
    setter((void *)exe_site(FLAG_OBJECT_VA), NULL, index, value);

    log_info("flag %d %s = %ld", index, flags_name(index) ? flags_name(index) : "(unnamed)",
             (long)value);
    return true;
}

/* Pressing an entry goes through the engine's own dispatcher at 0x411BC0, which has 25 cases
 * for 112 entries. This table only LABELS them; the engine decides what pressing one means.
 * README.md lists the cases and what each does. */
#define FLAG_ACTIVATE_VA 0x00411BC0u

/* `__fastcall` with a dead EDX parameter, standing in for the engine's `__thiscall`.
 * MSVC does not accept `__thiscall` on a function-pointer typedef in C; the substitution is
 * exact on x86, `this` in ECX either way, the real arguments in the same stack slots, callee
 * cleanup either way, and EDX is caller-saved and never read. See the longer note in
 * cheats.c. */
typedef void (__fastcall *activate_fn)(void *self, void *unused_edx, int index);

static const struct { int index; int range; } g_cycles[] = {
    {   0, 3 }, {  51, 3 }, {  16, 3 },
    {   1, 7 },
    {   2, 4 }, {  47, 4 }, { 108, 4 }
};

/* 59 is the screenshot. 95, 96, 98, 103, 104, 106, 107 are the cheat commands; 97 and 111 both
 * send a command AND keep a state, so they read as switches and are left as such. */
static const int g_actions[] = { 59, 95, 96, 98, 103, 104, 106, 107 };

flag_kind_t flags_kind(int index)
{
    size_t i;

    for (i = 0; i < sizeof(g_numbers) / sizeof(g_numbers[0]); ++i) {
        if (g_numbers[i] == index) { return FLAG_NUMBER; }
    }
    for (i = 0; i < sizeof(g_actions) / sizeof(g_actions[0]); ++i) {
        if (g_actions[i] == index) { return FLAG_ACTION; }
    }
    for (i = 0; i < sizeof(g_cycles) / sizeof(g_cycles[0]); ++i) {
        if (g_cycles[i].index == index) { return FLAG_CYCLE; }
    }
    return FLAG_TOGGLE;
}

int flags_cycle_range(int index)
{
    size_t i;

    for (i = 0; i < sizeof(g_cycles) / sizeof(g_cycles[0]); ++i) {
        if (g_cycles[i].index == index) { return g_cycles[i].range; }
    }
    return 2;
}

bool flags_activate(int index)
{
    uintptr_t   names  = 0;
    uintptr_t   values = 0;
    activate_fn activate;
    int32_t     before = 0;
    int32_t     after  = 0;

    /* 0x6F is the dispatcher's own ceiling: above it, it falls out having done nothing. Refusing
     * here rather than calling and hoping keeps the log honest about which entries are reachable
     * at all. */
    if (index < 0 || index > 0x6F || !arrays(&names, &values)) {
        return false;
    }

    flags_value(index, &before);

    activate = (activate_fn)exe_site(FLAG_ACTIVATE_VA);
    activate((void *)exe_site(FLAG_OBJECT_VA), NULL, index);

    flags_value(index, &after);

    log_info("flag %d %s: %ld -> %ld", index,
             flags_name(index) ? flags_name(index) : "(unnamed)", (long)before, (long)after);
    return true;
}
