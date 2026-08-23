#include "player.h"

#include "common/engine_sites.h"
#include "common/logging.h"
#include "common/memory.h"

#include <windows.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Preferred-base addresses from Fellowship.rfl, converted to RVAs the way the rest of this tree
 * does it. See the note at the top of player.h for where each one comes from. */
#define RFL_OBJECT_MANAGER_RVA (0x101326CCu - 0x10000000u)
#define RFL_ENTRY_LIST_RVA     (0x101326E4u - 0x10000000u)

#define MANAGER_LOCAL_PLAYER   0x0B8u   /* -> the local player's game object */
#define OBJECT_DEF_INDEX       0x00Cu   /* u16, 0xffff when the object has no class */
#define ENTRY_LIST_ARRAY       0x004u
#define ENTRY_LIST_COUNT       0x00Cu
#define ENTRY_STRIDE           36u
#define ENTRY_CLASS_ID         0x004u
/* The ObjectDef id of the class the engine's own class table names `Player`. */
#define CLASS_ID_PLAYER        0x1000Eu

/* The game object's transform, as the position probe found it. Position and rotation were both
 * confirmed by watching them: +0x00EC changes as you walk, +0x00F8 is the yaw that turns when you
 * turn. +0x011C is three floats holding exactly (1, 1, 1) immediately after the nine of the
 * matrix, 0xF8 + 36 is 0x11C, which is where a scale belongs in a position/rotation/scale
 * layout, and it has never been seen holding anything else. */
#define OBJECT_POSITION        0x0ECu
#define OBJECT_TRANSFORM       0x0F8u
#define OBJECT_SCALE           0x11Cu

static uintptr_t g_last_object;

static float dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static bool rfl_base(uintptr_t *base)
{
    HMODULE module = GetModuleHandleA("Fellowship.rfl");

    if (module == NULL) {
        return false;
    }
    *base = (uintptr_t)module;
    return true;
}

uintptr_t player_object(const char **why)
{
    uintptr_t base;
    uint32_t  manager = 0;
    uint32_t  object  = 0;
    uint32_t  list    = 0;
    uint32_t  array   = 0;
    uint32_t  count   = 0;
    uint32_t  classid = 0;
    uint16_t  index   = 0;

    *why = "";

    if (!rfl_base(&base)) {
        *why = "Fellowship.rfl is not loaded";
        return 0;
    }
    if (!memory_read_u32(rfl_site(base, RFL_OBJECT_MANAGER_RVA), &manager) || manager == 0) {
        *why = "the object manager global is still NULL, no level yet";
        return 0;
    }
    if (!memory_is_readable_range((uintptr_t)manager, MANAGER_LOCAL_PLAYER + 4u)) {
        *why = "the object manager is not readable";
        return 0;
    }
    if (!memory_read_u32((uintptr_t)manager + MANAGER_LOCAL_PLAYER, &object) || object == 0) {
        *why = "there is no local player object";
        return 0;
    }
    /* Far enough to cover the fields this resolver reads and the Player subobject pointer at
     * +0xc8. The SEARCH checks its own extent separately, because how big the object really is
     * is not something we know. */
    if (!memory_is_readable_range((uintptr_t)object, 0x100u)) {
        *why = "the player object is not readable";
        return 0;
    }

    {
        uint32_t word = 0;
        if (!memory_read_u32((uintptr_t)object + OBJECT_DEF_INDEX, &word)) {
            *why = "the object's ObjectDef index could not be read";
            return 0;
        }
        index = (uint16_t)(word & 0xFFFFu);
    }
    if (index == 0xFFFFu) {
        *why = "the object has no ObjectDef class";
        return 0;
    }

    if (!memory_read_u32(rfl_site(base, RFL_ENTRY_LIST_RVA), &list) || list == 0) {
        *why = "the ObjectDef entry list global is NULL";
        return 0;
    }
    if (!memory_is_readable_range((uintptr_t)list, ENTRY_LIST_COUNT + 4u)) {
        *why = "the ObjectDef entry list is not readable";
        return 0;
    }
    if (!memory_read_u32((uintptr_t)list + ENTRY_LIST_COUNT, &count)
        || !memory_read_u32((uintptr_t)list + ENTRY_LIST_ARRAY, &array) || array == 0) {
        *why = "the ObjectDef entry list has no array";
        return 0;
    }
    if ((uint32_t)index >= count) {
        *why = "the object's ObjectDef index is past the end of the list";
        return 0;
    }

    {
        uintptr_t entry = (uintptr_t)array + (uintptr_t)index * ENTRY_STRIDE;

        if (!memory_is_readable_range(entry, ENTRY_STRIDE)) {
            *why = "the ObjectDef entry is not readable";
            return 0;
        }
        if (!memory_read_u32(entry + ENTRY_CLASS_ID, &classid)) {
            *why = "the ObjectDef entry has no class id";
            return 0;
        }
    }
    /* The identification, and the reason this is not a hopeful cast. Anything else means the
     * manager's +0xb8 is holding something that is not a Player, and we decline rather than write
     * into it. */
    if (classid != CLASS_ID_PLAYER) {
        *why = "the local player object is not of class Player";
        return 0;
    }

    g_last_object = (uintptr_t)object;
    return (uintptr_t)object;
}

uintptr_t player_last_object(void)
{
    return g_last_object;
}

/* ------------------------------------------------------------------------------- writing back
 *
 * memory.h deliberately offers no write, because almost everything in this tree that writes to
 * the game writes to CODE and goes through patch_write_*, which restores page protection
 * afterwards. This writes to a heap object instead, which is already writable, so the thing worth
 * checking is that the range really is committed and really is writable; a stale pointer that
 * happened to survive the class check would otherwise fault here.
 *
 * Kept local rather than added to common/memory.h: one caller does not justify widening an API
 * that every plugin sees.
 */
static bool writable_range(uintptr_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t                cursor = address;
    uintptr_t                end    = address + size;

    while (cursor < end) {
        if (VirtualQuery((LPCVOID)cursor, &info, sizeof(info)) != sizeof(info)) {
            return false;
        }
        if (info.State != MEM_COMMIT) {
            return false;
        }
        if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
            return false;
        }
        if ((info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY
                             | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
            return false;
        }
        cursor = (uintptr_t)info.BaseAddress + info.RegionSize;
    }
    return true;
}

bool player_apply_size(float girth, float height, const char **why)
{
    uintptr_t object;
    uintptr_t matrix;
    float     m[9];
    float     triple[3];
    int       row;

    *why = "";

    /* Girth is height MULTIPLIED BY build, so the combinations reach further than either row
     * suggests, Huge at 3.0 with Absurd at 8.0 is 24, which the old ceiling of 20 refused. The
     * limit is a sanity guard against a corrupt read reaching this far, not a judgement about how
     * silly a hobbit is allowed to be, so it moves up rather than clamping the buttons. */
    if (!(girth > 0.05f && girth < 40.0f) || !(height > 0.05f && height < 40.0f)) {
        *why = "refusing a scale outside 0.05 .. 40";
        return false;
    }
    object = player_object(why);
    if (object == 0) {
        return false;
    }

    matrix = object + OBJECT_TRANSFORM;
    if (!memory_read(matrix, m, sizeof(m))) {
        *why = "the transform could not be read";
        return false;
    }
    if (!writable_range(matrix, sizeof(m))
        || !writable_range(object + OBJECT_SCALE, sizeof(triple))) {
        *why = "the transform is not writable";
        return false;
    }

    /* A collapsed row means this is no longer an orientation matrix; the object was rebuilt
     * under us, and writing into whatever replaced it is the fault this file exists to avoid. */
    for (row = 0; row < 3; ++row) {
        if (!(dot3(m + row * 3, m + row * 3) > 1.0e-8f)) {
            *why = "the transform is degenerate, not an orientation matrix any more";
            return false;
        }
    }

    /* Renormalise, then scale. Reading back a matrix we already scaled would compound, and the
     * engine rewrites this one from animation every frame anyway, so taking the rows to unit
     * length first is what makes calling this every frame a hold rather than a multiplication. */
    for (row = 0; row < 3; ++row) {
        float *r      = m + row * 3;
        float  length = (float)sqrt((double)dot3(r, r));
        /* Row 1 is the up axis; it reads (0, 1, 0) in every sample, where rows 0 and 2 turn with
         * the player. So row 1 takes the height and the other two take the girth. */
        float  factor = (row == 1) ? height : girth;

        r[0] = r[0] / length * factor;
        r[1] = r[1] / length * factor;
        r[2] = r[2] / length * factor;
    }
    memcpy((void *)matrix, m, sizeof(m));

    /* And the reciprocal into the camera multiplier. Written outright rather than renormalised,
     * because unlike the matrix nothing else touches it: it has held exactly (1, 1, 1) in every
     * reading, so 1.0 restores it precisely and there is nothing to accumulate. */
    /* Per axis, not one value in all three.
     *
     * Keying the whole thing to height was wrong, and wrong in the way it was predicted to be:
     * changing only the build moved the camera. Writing the offset out explains it; the camera
     * sits at `matrix * (0, TrackHeight, -TrackDist)`, so the DISTANCE term rides on row 2, a
     * horizontal row that carries girth, while the HEIGHT term rides on row 1 which carries
     * height. Two different scales, so one reciprocal cannot cancel both.
     *
     * This vector has three components and until now every write put the same number in all of
     * them. Giving each axis the reciprocal of whatever scaled it is the obvious next reading:
     * y cancels the height row, x and z cancel the two horizontal ones. If the vector turns out
     * to be uniform after all, or ordered differently, this will still be visibly wrong when
     * girth and height differ, and right when they are equal, which is the case that already
     * worked. */
    triple[0] = 1.0f / girth;
    triple[1] = 1.0f / height;
    triple[2] = 1.0f / girth;
    memcpy((void *)(object + OBJECT_SCALE), triple, sizeof(triple));
    return true;
}


/* Log the floats where the two positions are most likely to be, so they can be identified by
 * looking at them rather than guessed at.
 *
 * A world coordinate in this engine is large, the schema talks in world units where a tracking
 * distance is 2000, and it changes as you walk. A rotation component never leaves [-1, 1] and a
 * flag or a counter does not look like either. Printing a window around each and reading it is
 * quicker and far more certain than another search.
 *
 * The player's is expected next to its transform at +0x00F8: the three floats before it, or the
 * three after the nine. The camera's is expected near the head of its object. Both windows are
 * printed whole so that "none of these" is as visible an answer as a hit. */
