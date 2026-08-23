#include "common/host_image.h"

#include "common/engine_types.h"

#include <windows.h>

#include <stdbool.h>
#include <string.h>

typedef struct host_image_state {
    bool      resolved;
    bool      valid;
    uintptr_t base;
    uintptr_t end;
    uintptr_t text;
    size_t    text_size;
    char      path[MAX_PATH];
    char      directory[MAX_PATH];
} host_image_state_t;

static host_image_state_t image_state;

static void resolve_paths(void)
{
    size_t index;

    image_state.path[0] = '\0';
    image_state.directory[0] = '\0';

    if (GetModuleFileNameA(NULL, image_state.path, MAX_PATH) == 0) {
        return;
    }
    image_state.path[MAX_PATH - 1] = '\0';

    memcpy(image_state.directory, image_state.path, sizeof(image_state.directory));

    /* Trim back to and including the last separator, so the result always ends in a backslash
     * and every caller can concatenate without deciding whether to add one. */
    for (index = strlen(image_state.directory); index > 0; --index) {
        if (image_state.directory[index - 1] == '\\' || image_state.directory[index - 1] == '/') {
            image_state.directory[index] = '\0';
            return;
        }
    }
    image_state.directory[0] = '\0';
}

bool host_image_resolve(void)
{
    HMODULE               module;
    IMAGE_DOS_HEADER     *dos_header;
    IMAGE_NT_HEADERS32   *nt_headers;
    IMAGE_SECTION_HEADER *section;
    WORD                  index;

    if (image_state.resolved) {
        return image_state.valid;
    }
    image_state.resolved = true;

    resolve_paths();

    module = GetModuleHandleA(NULL);
    if (module == NULL) {
        return false;
    }

    dos_header = (IMAGE_DOS_HEADER *)module;
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    nt_headers = (IMAGE_NT_HEADERS32 *)((BYTE *)module + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    /* A 64-bit host would mean every offset in this project is meaningless. Refusing here is
     * what turns that into "no patch was applied" instead of "something was written somewhere". */
    if (nt_headers->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) {
        return false;
    }

    image_state.base = (uintptr_t)module;
    image_state.end  = image_state.base + nt_headers->OptionalHeader.SizeOfImage;

    section = IMAGE_FIRST_SECTION(nt_headers);
    for (index = 0; index < nt_headers->FileHeader.NumberOfSections; ++index, ++section) {
        if ((section->Characteristics & IMAGE_SCN_CNT_CODE) != 0) {
            image_state.text      = image_state.base + section->VirtualAddress;
            image_state.text_size = section->Misc.VirtualSize;
            break;
        }
    }
    if (image_state.text == 0) {
        return false;
    }

    image_state.valid = true;
    return true;
}

uintptr_t   host_image_base(void)      { return image_state.base; }
uintptr_t   host_image_end(void)       { return image_state.end; }
uintptr_t   host_image_text(void)      { return image_state.text; }
size_t      host_image_text_size(void) { return image_state.text_size; }
const char *host_directory(void)       { return image_state.directory; }
const char *host_path(void)            { return image_state.path; }
