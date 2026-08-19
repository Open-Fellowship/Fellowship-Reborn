// property_ref.cpp - Fellowship.rfl.
//
//   0x1005a480   90 bytes   IsWeaponOrRingDef
//   0x1005ac90  103 bytes   GetRightPropertiesDefByIndex
//   0x1005ad00   69 bytes   GetRightPropertiesDef
//
// Three __stdcall free functions that resolve an ObjectDef record out of the
// one global entry list at 0x101326e4. All three inline the same index-to-record
// walk as ObjectDefEntryList::GetEntry at 0x10008f30
// (decomp\src\objectdef\entry_list.cpp): the >= 0 / < count guard, the discarded
// PrepareEntry call, and the 36-byte stride that shows up as
// LEA ECX,[EDI+EDI*8] / LEA EAX,[EDX+ECX*4].
//
// They do NOT all take the same argument. 0x1005ad00 is handed an *object* and
// reads authored property 34 off it; the other two are handed an *index* into
// the entry list directly - the argument goes straight into the signed bounds
// check with no dereference at all.
//
// The declarations below are local copies on purpose; nothing shared is edited.


class PropertyBlock;


// One ObjectDef entry. 36 bytes, from the stride. Only +0x04 (the ObjectDef
// class id) and +0x08 (the record's authored-property block) are established.
struct ObjectDefEntry
{
    int m_unknown_00;
    int m_class_id;              // +0x04
    PropertyBlock *m_properties; // +0x08
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
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();

    // Slot 4 - CALL dword ptr [EDX+0x10]. Called with (index, 0) and its
    // result discarded, exactly as at 0x10008f30. Purpose not established.
    virtual void PrepareEntry(int index, int flags);

    ObjectDefEntry *GetEntry(int index)
    {
        if (index >= 0 && index < m_count)
        {
            PrepareEntry(index, 0);
            return m_entries + index;
        }

        return 0;
    }

private:
    ObjectDefEntry *m_entries;   // +0x04
    int m_unknown_08;
    int m_count;                 // +0x0c
};


// The authored-property block. Values are addressed by ordinal through slot 2
// of its vtable; the implementation is in Fellowship.exe, not here.
class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);
};


// Any game object: the property block hangs off +0x14.
class PropertyOwner
{
public:
    char reserved[0x14];
    PropertyBlock *properties;   // +0x14
};


// MOV EDI,dword ptr [0x101326e4] - the global holds the list pointer itself,
// not the list, so it is loaded once and reused.
extern ObjectDefEntryList *g_objectDefEntries;


// 0x1005a480. Takes an entry-list index and answers whether the record it names
// is one of three ObjectDef classes: 0x10025 Ranged Weapon, 0x10031 Melee Weapon,
// 0x10107 The One Ring. The class ids are real - they come straight out of the
// rfl's own ObjectDef table - so this predicate's *subject* is established even
// though its caller is not; the name says only which classes it accepts.
//
// The SUB/JZ chain rather than a CMP/JZ chain is VC6's sparse-switch lowering,
// so the source is a switch and not `a || b || c`.
int __stdcall IsWeaponOrRingDef(int index)
{
    ObjectDefEntry *entry = g_objectDefEntries->GetEntry(index);
    if (entry == 0)
        return 0;

    switch (entry->m_class_id)
    {
    case 0x10025:                // Ranged Weapon
    case 0x10031:                // Melee Weapon
    case 0x10107:                // The One Ring
        return 1;
    }

    return 0;
}


// 0x1005ac90. The index form of the function below: resolve the record named by
// `index`, read authored property 34 off *that record's* property block at +0x08,
// and resolve the record that value names in turn. The global is re-read after
// the accessor call because the call could have moved the list.
ObjectDefEntry * __stdcall GetRightPropertiesDefByIndex(int index)
{
    ObjectDefEntry *def = g_objectDefEntries->GetEntry(index);
    if (def != 0)
    {
        int refIndex = *(int *)def->m_properties->GetValue(34, -1);

        ObjectDefEntry *entry = g_objectDefEntries->GetEntry(refIndex);
        if (entry != 0)
            return entry;
    }

    return 0;
}


// 0x1005ad00. Ordinal 34 on both weapon classes is `RightProperties`, an object
// reference accepting `Player Weapon Properties`. That the value is an object
// reference and not a float is proved by the arithmetic - a float bit pattern
// would fail the bounds check - but the owning class is not established, so the
// name records the candidate, not a fact.
ObjectDefEntry * __stdcall GetRightPropertiesDef(PropertyOwner *object)
{
    if (object != 0)
    {
        int index = *(int *)object->properties->GetValue(34, -1);

        ObjectDefEntry *entry = g_objectDefEntries->GetEntry(index);
        if (entry != 0)
            return entry;
    }

    return 0;
}
