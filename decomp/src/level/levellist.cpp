// levellist.cpp - Fellowship.rfl, the machinery behind LevelList.txt.
//
// LevelList.txt sits next to Fellowship.exe, one level path per line, and fills
// the level-selection screen that `runtime/src/plugins/level_select` reopens.
// The reader is at 0x10072c20; these are the two helpers it leans on.
//
// Both carry real relocations, which the earlier maths classes did not: string
// addresses, a call to the CRT, and five imports. `matchtool.py` blanks those
// operands on both sides, so what is compared is purely the code generation.

#include <windows.h>
#include <string.h>


// 0x10072880, 50 bytes, __cdecl.
//
// Why a level whose path contains "backup" never appears in the list, in either
// case - the reader calls this on every line and skips the ones that answer 1.
// Not a filter anybody documented; it is just here, and it explains a level
// going missing from the menu for a reason no amount of staring at
// LevelList.txt would reveal.
//
// The fall-through returns whatever strstr left in EAX, which on that path is
// always NULL. The compiler knows that and never zeroes EAX - `return 0` costs
// nothing.
int IsBackupPath(const char *s)
{
    if (strstr(s, "backup") || strstr(s, "BACKUP")) {
        return 1;
    }
    return 0;
}


// The list itself: a pointer to a block of fixed-size records and the number of
// records it has room for. Each record is 0x200 bytes - the reader zeroes 0x80
// dwords per entry, and the growth below shifts left by 9.
class LevelList
{
public:
    void *data;      // 0x00
    int   capacity;  // 0x04

    void Reserve(int n);   // 0x10072e90, 134 bytes
};


// 0x10072e90, __thiscall. NOT MATCHED, 55 of 134 bytes - but see below,
// because the 55 are not what they look like.
//
// Capacity doubling, starting at 4, on the Win32 global heap rather than the
// CRT's. GlobalHandle is called twice and the compiler caches its import
// thunk in EBX by itself.
//
// The doubling is written twice on purpose: once unconditionally, then again in
// a loop if one doubling was not enough. A single loop would be shorter and
// would not match - the original really does have both.
//
// **This is structurally an exact match.** Every instruction is the same, in
// the same order, with the same operands. The whole difference is which two
// registers the allocator picked:
//
//     orig   this -> EDI, the record count -> ESI
//     ours   this -> ESI, the record count -> EDI
//
// and one consequence of it: the original defers pushing its second register
// until after the early return, ours saves both at entry. Swap the two names
// throughout and the functions are identical, which is why 55 bytes differ
// while nothing is actually wrong.
//
// Tried without moving it: declaring the count local at the top of the
// function, dropping the local and using the member directly (65 - worse), and
// caching `data` in a local as well. VC6 normally takes ESI before EDI, and
// what made it choose the other way round here is not known.
void LevelList::Reserve(int n)
{
    if (capacity > n) {
        return;
    }

    if (capacity == 0) {
        capacity = 4;
    } else {
        capacity = capacity + capacity;
    }

    if (capacity <= n) {
        int grown = capacity;
        do {
            grown = grown + grown;
        } while (grown <= n);
        capacity = grown;
    }

    {
        int count = capacity;
        if (data != 0) {
            GlobalUnlock(GlobalHandle(data));
            data = GlobalLock(GlobalReAlloc(GlobalHandle(data), count << 9, GMEM_MOVEABLE));
        } else {
            data = GlobalLock(GlobalAlloc(0, count << 9));
        }
    }
}
