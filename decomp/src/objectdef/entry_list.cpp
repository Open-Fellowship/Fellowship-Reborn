// entry_list.cpp - Fellowship.rfl.
//
// 0x10008f30, 46 bytes, __thiscall - the engine's index-to-record lookup, and
// the most-called accessor found so far: 58 call sites in 31 functions. It is
// how everything asks "what kind of object is this?", because a very large
// share of those sites follow the call immediately with
// `MOV EDX,[EAX+4] / CMP EDX,<class id>`.

// One record. The lookup proves the size and nothing else: the stride is 36
// bytes (LEA ECX,[EDI+EDI*8] then LEA EAX,[EDX+ECX*4] is index * 9 * 4). The
// class id at +4 is established by the callers, not by this function; the rest
// is unread here and left unnamed.
struct ObjectDefEntry
{
    int m_unknown_00;
    int m_class_id;      // +0x04, tested against ObjectDef class ids by callers
    int m_unknown_08;
    int m_unknown_0c;
    int m_unknown_10;
    int m_unknown_14;
    int m_unknown_18;
    int m_unknown_1c;
    int m_unknown_20;
};


class ObjectDefEntryList
{
public:
    // Four slots ahead of the one this function calls; declared but not
    // defined, which is enough to place them in the vtable. The call is
    // CALL dword ptr [EAX+0x10], so PrepareEntry is slot 4.
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();

    // Called with (index, 0) before the address is computed and its result
    // discarded. Presumably a fault-in or validate step; its purpose is not
    // established, and neither is the second argument.
    virtual void PrepareEntry(int index, int flags);

    ObjectDefEntry *GetEntry(int index);

private:
    ObjectDefEntry *m_entries;   // +0x04
    int m_unknown_08;            // never read here
    int m_count;                 // +0x0c
};


ObjectDefEntry *ObjectDefEntryList::GetEntry(int index)
{
    if (index >= 0 && index < m_count)
    {
        PrepareEntry(index, 0);
        return m_entries + index;
    }

    return 0;
}
