// 0x10005190, 38 bytes - a filter object's "does this value pass?" test.
//
// __thiscall (ECX is `this`, RET 0x4, one dword argument). The object has a
// vtable at +0 and a dword at +4; the second vtable slot (+0x4) is called with
// no stack arguments, so it is a __thiscall predicate taking only `this`.
//
// The logic is a short-circuit: if the virtual predicate answers non-zero the
// argument is accepted whatever it is, otherwise it is accepted only when it
// equals the stored dword. That reads as a wildcard/"matches anything" flag
// hanging off the vtable, which is why the names below say so - but only the
// shape is established, not the domain, so slot 0 keeps a placeholder name and
// the stored dword is just "the value we compare against".

class ValueFilter
{
public:
    virtual int Unknown0();     // vtable slot 0 - never called here
    virtual int MatchesAny();   // vtable slot 1, [vtable + 0x4]

    int Accepts(int value);

    int m_value;                // +0x4
};


int ValueFilter::Accepts(int value)
{
    if (MatchesAny() == 0 && value != m_value) {
        return 0;
    }
    return 1;
}
