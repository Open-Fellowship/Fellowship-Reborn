// 0x0044e670 (Fellowship.exe), 103 bytes - resize an array of 4-byte elements
// held in a locked block of the Win32 global heap.
//
// ECX holds `this` and the function ends RET 4, so it is __thiscall taking one
// dword. Layout: something unidentified at +0, the element count at +4, the
// locked block pointer at +8. Offset 0 is never read or written here, so it is
// left an unnamed member.
//
// The five CALL dword ptr [0x0056xxxx] sites are import thunks into KERNEL32,
// resolved as GlobalHandle / GlobalUnlock / GlobalFree / GlobalAlloc /
// GlobalLock. They are declared here as __declspec(dllimport) rather than by
// including <windows.h>: dllimport is what makes the compiler treat the thunk
// slot as a variable and emit CALL dword ptr [addr]. Without it VC6 emits
// CALL rel32 to a linker-generated jump stub, which is the wrong instruction
// and the wrong length. It is also dllimport that lets the two GlobalHandle
// calls share one load of the slot - MOV EBX,[0x56773c] then CALL EBX twice -
// because the slot is then an ordinary CSE candidate.
//
// The handle is fetched twice, once for the unlock and once for the free, and
// m_items is re-read from memory for the second fetch; that is the compiler
// refusing to keep the member live across GlobalUnlock, and it comes out of
// simply naming the member twice in the source.

extern "C" __declspec(dllimport) void * __stdcall GlobalHandle(const void *pMem);
extern "C" __declspec(dllimport) int    __stdcall GlobalUnlock(void *hMem);
extern "C" __declspec(dllimport) void * __stdcall GlobalFree(void *hMem);
extern "C" __declspec(dllimport) void * __stdcall GlobalAlloc(unsigned int uFlags, unsigned long dwBytes);
extern "C" __declspec(dllimport) void * __stdcall GlobalLock(void *hMem);

class GlobalArray
{
public:
    int SetCount(int count);

private:
    int m_unknown_00;             // untouched by this function; purpose unknown
    int m_count;
    void *m_items;                // locked block of m_count 4-byte elements
};

int GlobalArray::SetCount(int count)
{
    if (count != m_count)
    {
        if (m_items != 0)
        {
            GlobalUnlock(GlobalHandle(m_items));
            GlobalFree(GlobalHandle(m_items));
        }

        m_count = count;

        if (count != 0)
        {
            m_items = GlobalLock(GlobalAlloc(0, count * 4));
            if (m_items == 0)
                return -1;
        }
    }

    return 0;
}
