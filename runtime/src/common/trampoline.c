#include "common/trampoline.h"

#include <windows.h>

void *trampoline_alloc(size_t size)
{
    if (size == 0) {
        return NULL;
    }
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}
