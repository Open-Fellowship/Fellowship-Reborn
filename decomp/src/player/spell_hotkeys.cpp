// spell_hotkeys.cpp - Fellowship.rfl.
//
//   0x1005a1c0  704 bytes  __thiscall  Player::AcquireObject(int objectLink)
//
// The player is handed an object link - an index into the global ObjectDef
// entry list - and dispatches on the *class* of the thing at that index. Six
// classes are named outright by the switch, and all six are real names from the
// ObjectDef table:
//
//   0x10025  Ranged Weapon  |
//   0x10031  Melee Weapon   |  same body: equip unless a global flag is set
//   0x1002f  Item           |  accepted, nothing to do
//   0x10113  Quest Item     |  accepted, nothing to do
//   0x10107  The One Ring   |  already holding one? notify : take it
//   0x10143  Spell          |  remember it, announce it, refresh the hotkey slot
//   default                    spawn the object and make the player its owner
//
// The Spell case is what puts this file under player\ and gives it its name.
// It reads five consecutive ordinals, 161 to 165, and only Player has more than
// 154 properties, so the class attribution is arithmetic rather than inference.
// Those five are the spell hotkey bindings in Player's "PC Controls
// Configuration" group - HKStaffStrikeSpell, HKFireAttackSpell,
// HKLightningSpell, HKHealSpell, HKAttractSpell - each an object reference to a
// Spell. The function compares the acquired link against each binding in turn
// and stores the matching slot index 0..4 at +0x3b4. Ordinal 160 in the same
// run is HKOneRing, which is the same table the 0x10107 case above is about.
//
// Reused unchanged from already-matched sources, because the shapes are
// established and rederiving them would only risk losing them:
//
//   ObjectDefEntryList::GetEntry  decomp\src\objectdef\entry_list.cpp  (inlined
//       here in full: the bounds pair, the PrepareEntry(index, 0) call and the
//       36-byte stride)
//   Handle::Resolve               decomp\src\core\handle.cpp  (inlined here on
//       the field at +0x14c: zero id, then the packed 6-byte slot)
//   ReadProperty                  decomp\src\player\stats.cpp  (the property
//       read through the ADDRESS of the block slot)
//
// If the integrator wants direct evidence of the original file structure: the
// three inlined bodies above must have been visible to the compiler here, so
// whatever headers declared them were included by the Player translation unit.
//
// NOT established, and named for role only: every CALL rel32 target below,
// the two engine globals that are not the ObjectDef list, and every vtable
// slot. See the comments at each.
//
// Two conventions this one confirmed, both new:
//
//   The switch value is UNSIGNED. The class id at entry+0x04 is compared with
//   JA, not JG. Declaring it `int` gives 0F 8F where the original has 0F 87 -
//   one byte, at the very top, and nothing else moves.
//
//   `break` and `return 1` are NOT interchangeable at the end of a case, even
//   where the switch is followed by exactly `return 1`. Written as
//   `m_selectedSpellHotkey = 1; return 1;` VC6 sees one constant serving both
//   the store and the return value and emits MOV EAX,1 / MOV [ESI+0x3b4],EAX,
//   four bytes shorter than the original's MOV dword ptr [ESI+0x3b4],1 -
//   and only in the block whose index is 1, because 0, 2, 3 and 4 do not
//   collide with the return value. Written as `... = 1; break;` the store is
//   emitted with its immediate and the epilogue is tail-duplicated afterwards,
//   which is what the original has in all five blocks.
//
// One conflict worth flagging rather than resolving: the global at 0x101326a8
// is modelled in decomp\src\player\health_regen.cpp as a frame timer with a
// float at +0x04. This function tests bit 0 of a byte at +0xbc of the same
// global. Both readings can be true of one object; neither confirms the other,
// so a local declaration is used here and nothing shared is edited.

class GameObject;


// ---------------------------------------------------------------------------
// The ObjectDef entry list - decomp\src\objectdef\entry_list.cpp, unchanged.

struct ObjectDefEntry            // 36 bytes; the stride is what proves the size
{
    int m_unknown_00;
    unsigned int m_class_id;     // +0x04 - UNSIGNED: the switch below
                                 // compares it with JA, not JG
    int m_unknown[7];
};

class ObjectDefEntryList
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();
    virtual void PrepareEntry(int index, int flags);   // [vtable+0x10]

    ObjectDefEntry *GetEntry(int index)
    {
        if (index >= 0 && index < m_count)
        {
            PrepareEntry(index, 0);
            return m_entries + index;
        }
        return 0;
    }

    ObjectDefEntry *m_entries;   // +0x04
    int m_unknown_08;
    int m_count;                 // +0x0c
};

extern ObjectDefEntryList *g_objectDefEntryList;   // 0x101326e4


// ---------------------------------------------------------------------------
// The object/handle registry at 0x101326cc.
//
// Three separate views of this one global agree on its shape. handle.cpp reads
// the six-byte slot array at +0x14; entry_cache.cpp calls a virtual through a
// vtable pointer at +0x10, which is what makes +0x10 a polymorphic subobject
// with the slot array as its first data member. This function uses both, plus
// an int at +0x1c that bounds the slot array, plus a virtual at the registry's
// OWN vtable that manufactures a game object from an object link.

#pragma pack(push, 2)
struct HandleSlot                // 6 bytes - the -6 displacement and the *2
{                                // index scale are only possible packed
    void *object;
    unsigned short extra;        // purpose unknown
};
#pragma pack(pop)

class HandleTable
{
public:
    virtual void Slot0();
    virtual void ReleaseHandle(unsigned short id);          // [vtable+0x04]
    virtual unsigned short CreateHandle(GameObject *owner); // [vtable+0x08]

    HandleSlot *m_slots;         // +0x04  (registry +0x14)
    int m_unknown_08;            // +0x08  (registry +0x18)
    int m_capacity;              // +0x0c  (registry +0x1c) - warned against
};

class ObjectRegistry
{
public:
    virtual void Slot0();
    virtual void Slot1();

    // [vtable+0x08]. Four arguments: the acquiring object, the address of a
    // field at +0xec of it, zero, and the object link. Returns a game object,
    // or null. "Create" is the role the call site gives it - the result is
    // immediately given an owner handle and a virtual at [vtable+0x14] - and
    // is not otherwise established.
    virtual GameObject *CreateObject(GameObject *owner, void *anchor,
                                     int flags, int objectLink);

    char m_unknown_04[0x10 - 0x04];
    HandleTable m_handles;       // +0x10
};

extern ObjectRegistry *g_objectRegistry;   // 0x101326cc


// A one-based handle into the registry's slot array - handle.cpp, unchanged
// but for the registry spelling.
class Handle
{
public:
    void *Resolve() const
    {
        if (m_id != 0)
            return g_objectRegistry->m_handles.m_slots[m_id - 1].object;
        return 0;
    }

    unsigned short m_id;         // 0 = none
};


// ---------------------------------------------------------------------------
// The property-value block - decomp\src\player\stats.cpp, unchanged.

class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);   // [vtable+0x08]
};

// The read goes through the ADDRESS of the slot, not the slot: that is what
// produces ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX.
inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar = -1;          // element index for a non-list property


// ---------------------------------------------------------------------------
// The object whose logic pointer sits at GameObject+0xc8. For the player's own
// game object that is the Player; for the object created above it is whatever
// that object's logic is. Only the handle at +0x38 and the virtual at
// [vtable+0x68] are touched here, and neither says what it is.

class ObjectLogic
{
public:
    virtual void Slot00();  virtual void Slot01();  virtual void Slot02();
    virtual void Slot03();  virtual void Slot04();  virtual void Slot05();
    virtual void Slot06();  virtual void Slot07();  virtual void Slot08();
    virtual void Slot09();  virtual void Slot10();  virtual void Slot11();
    virtual void Slot12();  virtual void Slot13();  virtual void Slot14();
    virtual void Slot15();  virtual void Slot16();  virtual void Slot17();
    virtual void Slot18();  virtual void Slot19();  virtual void Slot20();
    virtual void Slot21();  virtual void Slot22();  virtual void Slot23();
    virtual void Slot24();  virtual void Slot25();
    virtual void OnOwnerChanged();          // [vtable+0x68], no arguments

    char m_unknown_04[0x38 - 0x04];
    unsigned short m_ownerHandle;           // +0x38
};

class GameObject
{
public:
    virtual void Slot0();  virtual void Slot1();  virtual void Slot2();
    virtual void Slot3();  virtual void Slot4();
    virtual void OnAcquired();              // [vtable+0x14], no arguments

    char m_unknown_04[0x14 - 0x04];
    PropertyBlock *m_properties;            // +0x14
    char m_unknown_18[0xc8 - 0x18];
    ObjectLogic *m_logic;                   // +0xc8
    char m_unknown_cc[0xec - 0xcc];
    int m_anchor_ec;                        // +0xec - passed by address only
};


// ---------------------------------------------------------------------------
// The console. Slot 3 of its vtable is a __cdecl vararg, which is why `this`
// is pushed as the leftmost argument and the caller pops all twelve bytes.

class Console
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void __cdecl Printf(const char *format, ...);   // [vtable+0x0c]
};

struct ConsoleHolder
{
    void *m_unknown_00;
    Console *m_console;          // +0x04
};

extern ConsoleHolder *g_consoleHolder;      // 0x1013269c


// The global at 0x101326a8. Only bit 0 of the byte at +0xbc is read here, as a
// veto on equipping a weapon. See the header comment about health_regen.cpp.
struct EngineGlobals
{
    char m_unknown_00[0xbc];
    unsigned char m_flags_bc;    // +0xbc
};

extern EngineGlobals *g_engineGlobals;      // 0x101326a8


// ---------------------------------------------------------------------------
// ObjectDef class ids, from documentation\generated\class-index.md.

const int kClassRangedWeapon = 0x10025;
const int kClassItem         = 0x1002f;
const int kClassMeleeWeapon  = 0x10031;
const int kClassTheOneRing   = 0x10107;
const int kClassQuestItem    = 0x10113;
const int kClassSpell        = 0x10143;

// Player property ordinals, from the ObjectDef schema for class 0x1000e.
// Group "PC Controls Configuration"; every one an object reference to a Spell.
const int kOrdHKStaffStrikeSpell = 161;   // "Staff Strike Spell Hotkey"
const int kOrdHKFireAttackSpell  = 162;   // "Fire Attack Spell Hotkey"
const int kOrdHKLightningSpell   = 163;   // "Lightning Spell Hotkey"
const int kOrdHKHealSpell        = 164;   // "Heal Spell Hotkey"
const int kOrdHKAttractSpell     = 165;   // "Attract Spell Hotkey"

// The value stored at +0x3b4 is the index of the matching hotkey, in the same
// order the properties are declared in.
const int kHotkeyStaffStrike = 0;
const int kHotkeyFireAttack  = 1;
const int kHotkeyLightning   = 2;
const int kHotkeyHeal        = 3;
const int kHotkeyAttract     = 4;

// Pushed to the notifier below when a spell is acquired. Not identified.
const int kEventSpellAcquired = 0x13;


// The list at Player+0x138. Its one method here takes the object link and a
// flag and answers -1 for "not there", which is an index, so the object is a
// list of object links; nothing else about it is established.
class ObjectLinkList
{
public:
    int Find(int objectLink, int flags);     // 0x1004c3c0, __thiscall

    char m_unknown_00[0x14];
};

// The subobject at Player+0x38a, told about the spell that was picked up.
// Almost certainly the HUD or the spell selector; not established.
class SpellNotifier
{
public:
    void OnSpellAcquired(int objectLink);    // 0x10079f60, __thiscall
};


// ---------------------------------------------------------------------------
// Player. +0x38a is not 4-aligned, so the class is packed; pack(2) is the
// loosest setting that puts it there.

#pragma pack(push, 2)
class Player
{
public:
    int AcquireObject(int objectLink);

    // Not defined here - four __thiscall members of Player reached by
    // CALL rel32. Named for what the call site does with them.
    void EquipWeapon(int objectLink);        // 0x1005a790
    void NotifyRingAlreadyHeld();            // 0x1005b2a0, no arguments
    void TakeOneRing(int objectLink);        // 0x1005b1f0
    void SendEvent(int eventId, int param);  // 0x100625f0

    void       *m_unknown_00;                // +0x00
    GameObject *m_object;                    // +0x04
    char        m_unknown_08[0x138 - 0x08];
    ObjectLinkList m_carried;                // +0x138
    Handle      m_ringHandle;                // +0x14c
    char        m_unknown_14e[0x38a - 0x14e];
    SpellNotifier *m_spellNotifier;          // +0x38a
    char        m_unknown_38e[0x3b0 - 0x38e];
    int         m_pendingSpellLink;          // +0x3b0
    int         m_selectedSpellHotkey;       // +0x3b4
};
#pragma pack(pop)


// 0x1005a1c0, 704 bytes.
int Player::AcquireObject(int objectLink)
{
    ObjectDefEntry *entry = g_objectDefEntryList->GetEntry(objectLink);
    if (entry == 0)
        return 0;

    switch (entry->m_class_id)
    {
    case kClassRangedWeapon:
    case kClassMeleeWeapon:
        if ((g_engineGlobals->m_flags_bc & 1) == 0)
            EquipWeapon(objectLink);
        break;

    case kClassItem:
        break;

    case kClassTheOneRing:
        if (m_ringHandle.Resolve() != 0)
            NotifyRingAlreadyHeld();
        else
            TakeOneRing(objectLink);
        break;

    case kClassQuestItem:
        break;

    case kClassSpell:
        m_pendingSpellLink = objectLink;
        SendEvent(kEventSpellAcquired, 0);
        if (m_spellNotifier != 0)
            m_spellNotifier->OnSpellAcquired(m_pendingSpellLink);

        if (*(int *)ReadProperty(&m_object->m_properties,
                                 kOrdHKStaffStrikeSpell, kScalar) == objectLink)
        {
            m_selectedSpellHotkey = kHotkeyStaffStrike;
            break;
        }
        if (*(int *)ReadProperty(&m_object->m_properties,
                                 kOrdHKFireAttackSpell, kScalar) == objectLink)
        {
            m_selectedSpellHotkey = kHotkeyFireAttack;
            break;
        }
        if (*(int *)ReadProperty(&m_object->m_properties,
                                 kOrdHKLightningSpell, kScalar) == objectLink)
        {
            m_selectedSpellHotkey = kHotkeyLightning;
            break;
        }
        if (*(int *)ReadProperty(&m_object->m_properties,
                                 kOrdHKHealSpell, kScalar) == objectLink)
        {
            m_selectedSpellHotkey = kHotkeyHeal;
            break;
        }
        if (*(int *)ReadProperty(&m_object->m_properties,
                                 kOrdHKAttractSpell, kScalar) == objectLink)
        {
            m_selectedSpellHotkey = kHotkeyAttract;
            break;
        }
        break;

    default:
        {
            if (m_carried.Find(objectLink, 1) == -1)
                return 0;

            GameObject *spawned = g_objectRegistry->CreateObject(
                m_object, &m_object->m_anchor_ec, 0, objectLink);
            if (spawned == 0)
                return 0;

            ObjectLogic *logic = spawned->m_logic;
            if (logic == 0)
                return 0;

            GameObject *owner = m_object;

            if (logic->m_ownerHandle != 0)
            {
                g_objectRegistry->m_handles.ReleaseHandle(logic->m_ownerHandle);
                logic->m_ownerHandle = 0;
            }

            if (owner != 0)
            {
                unsigned short handle =
                    g_objectRegistry->m_handles.CreateHandle(owner);
                logic->m_ownerHandle = handle;
                if (handle > g_objectRegistry->m_handles.m_capacity)
                {
                    g_consoleHolder->m_console->Printf(
                        "Object link: %d beyond array bounds!  Could crash!",
                        handle);
                }
            }

            logic->OnOwnerChanged();
            spawned->OnAcquired();
            return 1;
        }
    }

    return 1;
}
