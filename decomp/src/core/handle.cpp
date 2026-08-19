// 0x100051c0, 33 bytes - resolve a one-based ushort handle through a global
// registry into the pointer stored in the handle's slot.
//
// ECX holds `this` and the function ends in a bare RET with no stack
// arguments, so it is __thiscall on a class whose first member is the ushort.
// Zero means "no handle" and yields null without touching the registry.
//
// The slot array lives at +0x14 of the object the global at 0x101326cc points
// to. Slots are six bytes (LEA EAX,[EAX+EAX*2] then a *2 index scale), and the
// -6 displacement folds the one-based bias into the addressing mode, so the
// index is `id - 1`. Only the leading dword of a slot is read here; the
// trailing word's purpose is not established from this function alone.

#pragma pack(push, 2)
struct HandleSlot                 // 6 bytes
{
    void *object;
    unsigned short extra;         // purpose unknown from this function
};
#pragma pack(pop)

struct HandleRegistry
{
    char unknown_00[0x14];
    HandleSlot *slots;
};

extern HandleRegistry *g_handleRegistry;

class Handle
{
public:
    void *Resolve() const;

private:
    unsigned short m_id;          // one-based index into the slot array; 0 = none
};

void *Handle::Resolve() const
{
    if (m_id != 0)
        return g_handleRegistry->slots[m_id - 1].object;

    return 0;
}
