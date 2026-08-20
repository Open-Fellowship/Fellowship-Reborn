#include "flags.h"

#include "common/engine_sites.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

/* ==================================================================================== the object
 *
 * Unlike the cheat command object, this one is not a pointer to be chased: 0x5449A8 IS the
 * object, sitting in the executable's own data. Both of its arrays hang off it:
 *
 *     0x5449A8 + 0xD4   ->  const char *names[124]
 *     0x5449A8 + 0xE0   ->  int32_t     values[124]
 *
 * The getter at 0x411BA0 proves the value array's shape:
 *
 *     00411BA0   mov eax,[esp+4]        the index
 *                cmp eax,0x2F
 *                je  0x411BB5           one flag is special-cased to a global
 *                mov ecx,[ecx+0xE0]     the array
 *                mov eax,[ecx+eax*4]    values[index]
 *
 * and the setter at 0x411800 both writes it and does the work each flag implies:
 *
 *     00411800   mov eax,[esp+4]        index
 *                mov esi,ecx            this
 *                mov edi,[esp+0x14]     value
 *                mov ecx,[esi+0xE0]
 *                dec eax
 *                cmp eax,0x6D
 *                mov [ecx+eax*4+4],edi  values[index] = value
 *                ja  <no side effects>
 *                ...                    a jump table, one case per flag
 *
 * That jump table is why this module calls the setter instead of writing the array. Flag 13 sets
 * two others. The screenshot flag takes a screenshot. The cache flags flush caches. Writing the
 * number directly would set the number and do none of the work.
 */
#define FLAG_OBJECT_VA   0x005449A8u
#define FLAG_NAMES_AT    0xD4u
#define FLAG_VALUES_AT   0xE0u
#define FLAG_SETTER_VA   0x00411800u

/* `__fastcall` with a dead EDX parameter, standing in for the engine's `__thiscall`.
 * MSVC does not accept `__thiscall` on a function-pointer typedef in C; the substitution is
 * exact on x86 - `this` in ECX either way, the real arguments in the same stack slots, callee
 * cleanup either way - and EDX is caller-saved and never read. See the longer note in
 * cheats.c. */
typedef void (__fastcall *set_flag_fn)(void *self, void *unused_edx, int index, int32_t value);

/* 99, 100 and 101 are X, Y and Z: the destination the Teleport entry (98) reads when it builds
 * "tele %d %d %d". They are the only entries here holding a number that means something in the
 * world rather than a mode, and toggling one between 0 and 1 - which is what the dispatcher's
 * default case does to them - throws the coordinate away. They get typed into instead. */
static const int g_numbers[] = { 99, 100, 101 };

/* AND THEY DO NOT LIVE IN THE VALUES ARRAY.
 *
 * Setting values[99..101] and pressing Teleport does nothing, which is exactly what happened.
 * The Teleport case reads three fields out of the flag object itself:
 *
 *     00411C7A   mov eax,[edi+0x11C]        edi is the flag object, 0x5449A8
 *                mov ecx,[edi+0x118]
 *                mov edx,[edi+0x114]
 *                push eax / push ecx / push edx
 *                push 0x52F618              "tele %d %d %d"
 *                push <stack buffer>
 *                call sprintf
 *                ...
 *                call [edx+0x68]            the command object, as every other cheat does
 *
 * Arguments are pushed right to left, so the first %d is [+0x114] and the last is [+0x11C]:
 * X, Y, Z in that order. The object is static, so those are three fixed addresses, and the
 * values array never enters into it.
 */
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
     * effect for these - the coordinate is read straight out of the object when Teleport runs. */
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

/* ================================================================== what pressing one actually does
 *
 * Setting the number is not what the game's own menu does, and for most of these entries it is
 * not enough. Pressing an entry goes through the dispatcher:
 *
 *     00411BC0   mov  edi,ecx                    the flag object
 *                push ebp                        the index
 *                call [[edi]]                    getter: esi = current value
 *                cmp  ebp,0x6F
 *                ja   default
 *                movzx ecx, byte [0x4120C0+ebp]  a case number per entry
 *                jmp  [0x41205C + ecx*4]         25 cases
 *     ...
 *     00412046   push esi / push ebp             the common tail:
 *                call 0x411800                   SetFlag(index, newValue)
 *
 * Twenty-five cases for a hundred and twelve entries, and reading them is what turns this list
 * from numbers into controls. What they do:
 *
 *   0x41202A  67 entries  the default: value = !value. A plain switch.
 *   0x411FBD  21 entries  switch, AND sets flag 0x39 with it - the statistics rows, which need
 *                         their master flag on to display at all. This is why stepping one of
 *                         those by hand did nothing.
 *   0x411F2E / 0x411F57 / 0x411F8A   wireframe, strips, render groups: a switch that also sets
 *                         0x0F, 0x39, 0x3D, 0x45 around it
 *   0x411DA5  0 and 51    cycle 0..2          0x411DB6  16   cycle 0..2
 *   0x411BF2  1           cycle 0..6          0x411C03  2    cycle 0..3
 *   0x411FD2  47          cycle 0..3, and writes 0x543434, which is the one value the getter
 *                         special-cases
 *   0x411DC7  108         cycle 0..3 through a second jump table
 *   0x411D28 / 0x411D56 / 0x411D84   the profiler switches, each clearing the other's global
 *   0x411FE2  23          hardware lighting: switches, then talks to the device
 *   0x411E4A  59          take a screenshot. An action; the value is not the point.
 *   0x411C50, 0x411C65, 0x411CB6, 0x411C7A, 0x411CD4, 0x411CE9, 0x411CFE, 0x411D13, 0x411C14
 *                         the cheats, which send a command string - the same eight this menu
 *                         already has as buttons on the other page
 *
 * So one call does the right thing for every entry, and this table is only used to LABEL them.
 * Nothing here decides behaviour; the engine does.
 */
#define FLAG_ACTIVATE_VA 0x00411BC0u

/* `__fastcall` with a dead EDX parameter, standing in for the engine's `__thiscall`.
 * MSVC does not accept `__thiscall` on a function-pointer typedef in C; the substitution is
 * exact on x86 - `this` in ECX either way, the real arguments in the same stack slots, callee
 * cleanup either way - and EDX is caller-saved and never read. See the longer note in
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
