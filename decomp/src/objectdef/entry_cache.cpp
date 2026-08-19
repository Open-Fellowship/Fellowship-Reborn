// entry_cache.cpp - Fellowship.rfl.
//
// Two small leaf methods, 0x10059490 and 0x10059f00. Neither reads an authored
// property, so neither implies an ObjectDef class and none is named here.
//
// They are declared on two separate classes below because nothing in these
// bytes ties them together: one touches only a ushort at +0x10, the other only
// +0x3c through +0x93. They may well be the same object - the two entries sit
// close together in the image - but that is not established, so they are kept
// apart rather than merged on a hunch.


// ---------------------------------------------------------------------------
// 0x10059f00 - release the handle held at +0x10.
//
//   MOV AX,word ptr [ESI+0x10] / TEST AX,AX
//
// The 66 operand-size prefixes make the field an `unsigned short`, and the
// argument reaches the callee the same way: `MOV AX,..` then `PUSH EAX`, which
// is how VC6 passes a ushort parameter. Zero means "no handle" and the call is
// skipped, matching Handle::Resolve at 0x100051c0 (decomp\src\core\handle.cpp),
// which treats 0 the same way on the same registry.
//
// The global at 0x101326cc is that same handle registry: handle.cpp models it
// flat, with the slot array at +0x14. Here the call is
//
//   MOV ECX,[0x101326cc] / ADD ECX,0x10 / MOV EDX,[ECX] / CALL [EDX+4]
//
// so there is a vtable pointer at registry+0x10 - i.e. a polymorphic subobject
// starts at +0x10, and handle.cpp's slot array at +0x14 is that subobject's
// first data member. The declarations below are a local copy on purpose;
// nothing shared is edited.

// The registry's virtual interface, entered at +0x10 of the registry object.
// Slot 1 is the only one this function uses. It is handed the handle by value
// and its result, if any, is discarded - a release or free, from the fact that
// the caller clears the field straight afterwards. Slot 0 is declared, not
// defined, only to place slot 1 at [vtable+4].
class HandleTable
{
public:
    virtual void Slot0();
    virtual void ReleaseHandle(unsigned short id);   // [vtable+4]
};

struct HandleRegistry
{
    char unknown_00[0x10];
    HandleTable table;               // +0x10 - vptr here, slots follow at +0x14
};

extern HandleRegistry *g_handleRegistry;             // 0x101326cc


class HandleOwner
{
public:
    void ReleaseHandle();

private:
    char unknown_00[0x10];
    unsigned short m_handle;         // +0x10, one-based; 0 = none
};

void HandleOwner::ReleaseHandle()
{
    if (m_handle != 0)
    {
        g_handleRegistry->table.ReleaseHandle(m_handle);
        m_handle = 0;
    }
}


// ---------------------------------------------------------------------------
// 0x10059490 - zero two dwords and stamp a template over two 36-byte slots.
//
// Two REP MOVSDs of nine dwords each, both from the fixed address 0x1013125c,
// into +0x4c and +0x70. Nine dwords is 36 bytes and 0x70 - 0x4c is also 36, so
// the two slots are adjacent and equally sized. 36 bytes is the ObjectDef entry
// stride established at 0x10008f30 (decomp\src\objectdef\entry_list.cpp), but
// nothing here reads a field of either slot, so whether these are ObjectDef
// records or merely records of the same size is NOT established by these bytes.
//
// A REP MOVSD from a fixed source is what a struct assignment from a global
// compiles to, and two of them from one source with no loop is two separate
// assignments rather than an array walk.

// 36 bytes. Nothing here reads any of it, so no member is named.
struct Record36
{
    int m_unknown[9];
};

extern Record36 g_recordTemplate;    // 0x1013125c


class RecordPair
{
public:
    void Reset();

private:
    char unknown_00[0x3c];
    int m_unknown_3c;                // +0x3c, cleared here; purpose unknown
    int m_unknown_40;                // +0x40, cleared here; purpose unknown
    char unknown_44[8];              // +0x44, untouched
    Record36 m_record0;              // +0x4c
    Record36 m_record1;              // +0x70
};

void RecordPair::Reset()
{
    m_unknown_3c = 0;
    m_unknown_40 = 0;
    m_record0 = g_recordTemplate;
    m_record1 = g_recordTemplate;
}
