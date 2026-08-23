#include "level_select.h"

#include "common/engine_types.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/module_watch.h"
#include "common/patch.h"

#include <windows.h>

#include <stdint.h>
#include <string.h>

#define PLUGIN_SECTION "level_select"

#define BRANCH_OFFSET   5u    /* into the pattern, where the two bytes live */

static const uint8_t stock_branch[] = {
    0x8B, 0x45, 0x70,               /* mov  eax,[ebp+0x70]   */
    0x85, 0xC0,                     /* test eax,eax          */
    0x0F, 0x84, 0xD6, 0x00, 0x00, 0x00  /* je +0xD6          */
};

static const uint8_t edited_branch[] = {
    0x8B, 0x45, 0x70,
    0x85, 0xC0,
    0x90, 0xE9, 0xD6, 0x00, 0x00, 0x00  /* nop / jmp +0xD6   */
};

static const uint8_t unconditional[2] = { 0x90, 0xE9 };

/* --------------------------------------------------------------------------------- the search */

/* The rfl is a DLL, so its code section has to be found in the mapped image rather than assumed.
 * Everything is bounds-checked: this runs against whatever module happens to be called
 * Fellowship.rfl, and being wrong about that must not mean reading off the end of it. */
static bool code_section(uintptr_t base, uintptr_t *start, size_t *size)
{
    IMAGE_DOS_HEADER      dos;
    IMAGE_NT_HEADERS32    nt;
    IMAGE_SECTION_HEADER  section;
    uintptr_t             nt_address;
    uintptr_t             section_address;
    unsigned              i;

    if (!memory_read(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    if (dos.e_lfanew <= 0 || (uintptr_t)dos.e_lfanew > 0x10000u) {
        return false;
    }

    nt_address = base + (uintptr_t)dos.e_lfanew;
    if (!memory_read(nt_address, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    if (nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96) {
        return false;
    }

    section_address = nt_address + offsetof(IMAGE_NT_HEADERS32, OptionalHeader) +
                      nt.FileHeader.SizeOfOptionalHeader;

    for (i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        if (!memory_read(section_address + i * sizeof(section), &section, sizeof(section))) {
            return false;
        }
        if ((section.Characteristics & IMAGE_SCN_CNT_CODE) == 0) {
            continue;
        }

        *start = base + section.VirtualAddress;
        *size  = section.Misc.VirtualSize ? section.Misc.VirtualSize : section.SizeOfRawData;

        return memory_is_readable_range(*start, *size);
    }

    return false;
}

/* Every match, not the first one: a pattern that appears twice is a pattern that has not
 * identified anything, and writing to the first of them would be a guess. */
static unsigned find_all(uintptr_t start, size_t size, const uint8_t *pattern, size_t length,
                         uintptr_t *first)
{
    const uint8_t *code = (const uint8_t *)start;
    unsigned       found = 0;
    size_t         i;

    if (size < length) {
        return 0;
    }

    for (i = 0; i + length <= size; ++i) {
        if (code[i] == pattern[0] && memcmp(code + i, pattern, length) == 0) {
            if (found == 0 && first != NULL) {
                *first = start + i;
            }
            ++found;
        }
    }

    return found;
}

/* --------------------------------------------------------------------------------- the install */

static void on_rfl_loaded(uintptr_t rfl_base)
{
    uintptr_t      code = 0;
    size_t         size = 0;
    uintptr_t      site = 0;
    unsigned       matches;
    patch_result_t result;

    if (!code_section(rfl_base, &code, &size)) {
        log_error("could not find the code section of Fellowship.rfl at %08X, not installing",
                  (unsigned)rfl_base);
        return;
    }

    matches = find_all(code, size, stock_branch, sizeof(stock_branch), &site);

    if (matches == 0) {
        /* Second question, asked only because the first came back empty: is the edit already
         * here? Copies that have been through the community patcher, or that came out of a
         * release that shipped an edited rfl, land in this branch. */
        if (find_all(code, size, edited_branch, sizeof(edited_branch), &site) > 0) {
            log_info("rfl+%05X already carries the edit, nothing to do on this copy",
                     (unsigned)(site - rfl_base));
            log_info("  New Game already opens the level list here. If it does not, the missing "
                     "piece is LevelList.txt next to Fellowship.exe, which is what fills it in.");
            return;
        }

        log_error("the New Game branch is not in this Fellowship.rfl, not installing. Looked "
                  "for 8B 45 70 85 C0 0F 84 D6 00 00 00 across %u bytes of code.",
                  (unsigned)size);
        return;
    }

    if (matches > 1) {
        log_error("that byte sequence appears %u times in this build, so it identifies nothing, "
                  "refusing to write", matches);
        return;
    }

    result = patch_write_bytes(site + BRANCH_OFFSET, unconditional, sizeof(unconditional));
    if (result != PATCH_RESULT_OK) {
        log_error("could not take the branch - %s", patch_result_text(result));
        return;
    }

    log_info("rfl+%05X  je -> nop/jmp: New Game now opens the level list",
             (unsigned)(site + BRANCH_OFFSET - rfl_base));
    log_info("  the screen is the game's own and so is the list. LevelList.txt next to "
             "Fellowship.exe is what fills it in, one level path per line; without it the engine "
             "says \"Level List could not be read!\" and you get nothing.");
}

void level_select_install(void)
{
    log_init(PLUGIN_SECTION, false);

    if (!ini_read_bool(PLUGIN_SECTION, "Enabled", true)) {
        log_info("Enabled=0, New Game starts the configured level as the game intends");
        return;
    }
    if (!host_image_resolve()) {
        log_error("the host image could not be resolved; refusing to touch anything");
        return;
    }

    if (!module_watch_when_loaded(fellowship_rfl_module_name(), on_rfl_loaded, 60000)) {
        log_error("could not start the module watch");
    }
}
