#include "common/memory.h"

#include "common/host_image.h"

#include <windows.h>

#include <string.h>

static bool region_is_readable(const MEMORY_BASIC_INFORMATION *info)
{
    DWORD protect;

    if (info->State != MEM_COMMIT) {
        return false;
    }
    protect = info->Protect & 0xFFu;
    if (protect == PAGE_NOACCESS) {
        return false;
    }
    if ((info->Protect & PAGE_GUARD) != 0) {
        return false;
    }
    return true;
}

bool memory_is_readable_range(uintptr_t address, size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    uintptr_t                cursor;
    uintptr_t                last;

    if (size == 0) {
        return false;
    }
    /* Wrap means the caller computed the range wrong, and a wrapped range would otherwise be
     * "checked" one page at a time forever. */
    if (address + size < address) {
        return false;
    }

    cursor = address;
    last   = address + size;

    while (cursor < last) {
        if (VirtualQuery((LPCVOID)cursor, &info, sizeof(info)) != sizeof(info)) {
            return false;
        }
        if (!region_is_readable(&info)) {
            return false;
        }
        cursor = (uintptr_t)info.BaseAddress + info.RegionSize;
    }
    return true;
}

bool memory_is_inside_image(uintptr_t address, size_t size)
{
    uintptr_t base = host_image_base();
    uintptr_t end  = host_image_end();

    if (base == 0 || end == 0 || size == 0) {
        return false;
    }
    if (address + size < address) {
        return false;
    }
    return address >= base && (address + size) <= end;
}

bool memory_make_writable(uintptr_t address, size_t size)
{
    DWORD previous;

    if (size == 0) {
        return false;
    }
    return VirtualProtect((LPVOID)address, size, PAGE_EXECUTE_READWRITE, &previous) != 0;
}

bool memory_read(uintptr_t address, void *destination, size_t size)
{
    if (destination == NULL || !memory_is_readable_range(address, size)) {
        return false;
    }
    memcpy(destination, (const void *)address, size);
    return true;
}

bool memory_read_u8(uintptr_t address, uint8_t *out)
{
    return memory_read(address, out, sizeof(*out));
}

bool memory_read_u32(uintptr_t address, uint32_t *out)
{
    return memory_read(address, out, sizeof(*out));
}
