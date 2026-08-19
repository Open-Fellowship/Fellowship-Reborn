// 0x10004ea0, 40 bytes - constructor for a small POD-ish record.
//
// __thiscall, RET 4, and it opens with MOV EAX,ECX (this returned in EAX),
// which is exactly how MSVC emits a constructor. One 4-byte argument is
// stored at +0x00; the rest of the initialisers are constants.
//
// Only offsets 0x00, 0x04, 0x08, 0x0c, 0x10 and 0x18 are written. 0x14 and
// 0x1c are left alone by the constructor, so the object is at least 0x20
// bytes and those two members are initialised elsewhere (or not at all).
// Nothing here establishes what any member means, so the names are positional.

class RecordState
{
public:
    void *owner;      // +0x00  the constructor's only argument
    int   field04;    // +0x04  = 5
    int   field08;    // +0x08  = 6
    int   field0c;    // +0x0c  = 0
    int   field10;    // +0x10  = 0
    int   field14;    // +0x14  not touched by the constructor
    int   field18;    // +0x18  = 1
    int   field1c;    // +0x1c  not touched by the constructor

    RecordState(void *o);
};

RecordState::RecordState(void *o)
{
    owner   = o;
    field04 = 5;
    field08 = 6;
    field0c = 0;
    field10 = 0;
    field18 = 1;
}
