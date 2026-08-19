// hit_reactions.cpp - Fellowship.rfl.
//
// 0x10058360, 87 bytes, __thiscall.
//
// Reads authored property ordinal 12 off the owning game object, treats the
// value as an ObjectDef *index*, resolves it to a record through the global
// ObjectDef entry list, and reports whether that record differs from the one
// already cached at +0x168.
//
// The owning ObjectDef class is NOT established. The file name is the one this
// worker was assigned and should be read as an address label, not a claim: the
// ordinal map attributes no class to this site, and nothing in these 87 bytes
// names one. What the bytes do establish is the property's *type* - the value
// is dereferenced as a signed int, range-checked against the entry list's
// count, and scaled by the list's 36-byte record stride, so it is a def index,
// which is how an "object reference" property is stored. A float property is
// ruled out: no FLD appears, and a float bit pattern used as an index would
// fail the bounds check for every plausible authored value.

// The authored-property store, reached through the owner's +0x14 slot. Only
// slot 2 is called here; the two ahead of it are declared and not defined,
// which is all it takes to place the slot.
class PropertyValueBlock
{
public:
    virtual void Slot0();
    virtual void Slot1();

    // PUSH <element> / PUSH <ordinal> / CALL [vtable+8] -> void *.
    // Untyped: the caller knows the width, and here it reads an int.
    virtual void *GetValue(int ordinal, int element);
};


// Whatever sits at gameobject+0x14 and owns the value block. Named for the one
// thing it demonstrably does; its size and any further members are unknown.
// The engine reaches it by address - ADD EAX,0x14 then a load through it, not
// a folded MOV EAX,[EAX+0x14] - so the block is a member of that subobject
// rather than a member of the game object directly.
class PropertyOwner
{
public:
    void *GetProperty(int ordinal, int element)
    {
        return m_values->GetValue(ordinal, element);
    }

private:
    PropertyValueBlock *m_values;   // +0x00
};


// One ObjectDef record. The 36-byte stride is forced by
// LEA ECX,[ESI+ESI*8] / LEA EAX,[EDX+ECX*4]; no field is read here.
// This is the same record as objectdef\entry_list.cpp declares - the
// integrator should factor the two together.
struct ObjectDefEntry
{
    int m_unknown[9];
};


// The list ObjectDefEntryList::GetEntry (0x10008f30) walks. That function is
// not called here: the same guard, the same PrepareEntry call and the same
// stride are open-coded inline, which is why the shape is declared locally.
class ObjectDefEntryList
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();
    virtual void PrepareEntry(int index, int flags);

    ObjectDefEntry *m_entries;   // +0x04
    int m_unknown_08;
    int m_count;                 // +0x0c
};

extern ObjectDefEntryList *g_objectDefEntryList;   // 0x101326e4


class GameObject;


// The object this method belongs to. Which ObjectDef class it implements is
// not established; only these two fields are touched here.
class PropertyObject
{
public:
    int ObjectRefPropertyChanged();

private:
    int m_unknown_00;
    GameObject *m_object;             // +0x04, owner of the property block
    char m_unknown_08[0x168 - 0x08];
    ObjectDefEntry *m_cachedEntry;    // +0x168, last resolved record
};


int PropertyObject::ObjectRefPropertyChanged()
{
    int changed = 0;

    PropertyOwner *owner = (PropertyOwner *)((char *)m_object + 0x14);

    // Element 0, not -1. Every other property read matched so far pushes -1;
    // this is the first site in the project with a different second argument.
    // What the bytes show is only that the value is 0 here. That the argument
    // is an element index at all comes from the documented call shape, not
    // from this function, and nothing here says whether 0 means "element 0 of
    // a list" or is simply how a non-scalar property is spelled.
    int index = *(int *)owner->GetProperty(12, 0);

    ObjectDefEntryList *list = g_objectDefEntryList;
    ObjectDefEntry *entry;

    if (index >= 0 && index < list->m_count)
    {
        list->PrepareEntry(index, 0);
        entry = list->m_entries + index;
    }
    else
    {
        entry = 0;
    }

    if (m_cachedEntry != entry)
        changed = 1;

    return changed;
}
