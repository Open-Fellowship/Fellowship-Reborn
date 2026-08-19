// invulnerability.cpp - Fellowship.rfl.
//
//   0x1005adf0  376 bytes  __thiscall  Player::UpdateScabbard()
//
// The file is named for the assignment that produced it. THE ASSIGNMENT'S
// PROPERTY NAMES ARE WRONG and the name should be changed when this is folded
// in - see "WHICH CLASS THE PROPERTIES BELONG TO" below. Nothing here is about
// invulnerability.
//
// WHAT IT DOES
//
// Walk the player's carried-object link list for the first link whose ObjectDef
// class is 0x10031 `Melee Weapon`. Resolve that weapon's `RightProperties`
// record - the already-matched GetRightPropertiesDefByIndex at 0x1005ac90 does
// exactly that, reading ordinal 34 off the weapon - and read three authored
// properties off *it*: a channel and two object references. Spawn the object
// reference, attach it to the channel, and keep a handle to it, releasing the
// previous one first. If no melee weapon link is found at all, send event 11
// and stop.
//
// WHICH CLASS THE PROPERTIES BELONG TO
//
// Not Player. This is the failure mode documentation\ORDINAL-MAP.md warns about
// in as many words: "the compared object and the read object are usually
// different". The class id in the body is 0x10031 `Melee Weapon`, but the
// properties are read off the record 0x1005ac90 hands back, and 0x1005ac90
// returns the record named by ordinal 34 `RightProperties`, whose schema
// declares it an object reference to **`Player Weapon Properties`**, ObjectDef
// class 0x1002e. So the ordinals below index that class's 37 properties:
//
//   ordinal 1  ScabbardChan       channel,  default none   group "Channels"
//   ordinal 3  EmptyScabbard      object reference -> Attachment
//   ordinal 4  ScabbardWithSword  object reference -> Attachment
//
// Three independent checks say that reading is right and the Player reading is
// not:
//
//   * the chain. 0x1005ac90 is matched in decomp\src\objectdef\property_ref.cpp
//     and demonstrably returns the RightProperties record, not the weapon and
//     not the player.
//   * the types. Ordinal 1 comes back through an integer MOV EBX,[EAX] and is
//     compared against -1. On Player ordinal 1 is `MaxHealth`, float, default
//     100.0 - a float that is never -1 and is never loaded with an integer MOV.
//     On Player ordinal 3 is `Invulnerable`, an enum {No,Yes} whose only values
//     are 0 and 1; here it is compared against -1 and handed to the object
//     factory as an object link. `channel` and `object reference` are the two
//     types whose "none" is -1, and both behave that way here.
//   * the behaviour. The two object references are alternatives for one
//     variable, and the one chosen when the handle at +0xd8 resolves to the
//     very weapon this link names - i.e. when that weapon is the one currently
//     in hand - is ordinal 3. `EmptyScabbard` when the sword is drawn,
//     `ScabbardWithSword` when it is not, attached to `ScabbardChan`. That is
//     the whole of the "Scabbard" and "Channels" groups doing the obvious
//     thing.
//
// `Player Weapon Properties`, `Melee Weapon`, `ScabbardChan`, `EmptyScabbard`
// and `ScabbardWithSword` are real names from the rfl's ObjectDef table.
// Everything else below is invented.
//
// 0x1005ac90 IS A MEMBER FUNCTION, not the __stdcall free function
// property_ref.cpp models it as. The call site here is
// PUSH EAX / MOV ECX,ESI / CALL - the player's own `this` goes into ECX. A
// __thiscall member that never reads `this` compiles to bytes identical to a
// __stdcall free function, so property_ref.cpp could not have told the
// difference from the callee alone; this call site can. Nothing there is wrong
// as bytes; the convention is. Flagged rather than edited - it is not my file.
//
// Reused unchanged from already-matched sources:
//
//   Handle::Resolve             decomp\src\core\handle.cpp (inlined here on the
//       field at +0xd8: the zero test, then the packed 6-byte one-based slot)
//   ObjectRegistry / HandleTable / Console
//                               decomp\src\player\spell_hotkeys.cpp - the same
//       global at 0x101326cc, the same CreateObject(owner, &owner->+0xec, 0,
//       link) call, the same release/create/bounds-warn triple and the same
//       "Object link: %d beyond array bounds!  Could crash!" literal. The tail
//       of this function and the tail of the `default:` arm of 0x1005a1c0 are
//       the same code written twice.
//   Player::SendEvent           0x100625f0, from spell_hotkeys.cpp
//
// NOT established, and named for role only: 0x1004c680 and 0x1004c610 (the two
// link-list methods), 0x10063be0 (the attach), event id 11, GameObject +0x0c,
// and every vtable slot index. See the comments at each.

class GameObject;


// ---------------------------------------------------------------------------
// One ObjectDef entry - decomp\src\objectdef\property_ref.cpp. Only +0x08, the
// record's authored-property block, is touched here.

class PropertyBlock;

struct ObjectDefEntry             // 36 bytes
{
    int m_unknown_00;
    unsigned int m_class_id;      // +0x04
    PropertyBlock *m_properties;  // +0x08
    int m_unknown_0c;
    int m_unknown_10;
    int m_unknown_14;
    int m_unknown_18;
    int m_unknown_1c;
    int m_unknown_20;
};


// The authored-property block - decomp\src\player\stats.cpp. The implementation
// is in Fellowship.exe, not here; the two slots before GetValue exist only to
// place it at [vtable+0x08].
class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);   // [vtable+0x08]
};

const int kScalar = -1;           // element index for a non-list property

// `ObjectDefEntry+0x08` holds the block pointer outright, so the read is
// spelled directly - MOV ECX,[EDI+0x8] / MOV EDX,[ECX] / CALL [EDX+0x8] - and
// not through the address of the slot the way a `+0x14` subobject is. See the
// two forms in the worker brief.

// `Player Weapon Properties` (class 0x1002e) ordinals.
const int kOrdScabbardChan      = 1;   // "Scabbard", channel, default none
const int kOrdEmptyScabbard     = 3;   // "Empty scabbard" -> Attachment
const int kOrdScabbardWithSword = 4;   // "Scabbard With Sword" -> Attachment

// The sentinel every one of the three is compared against. `channel` and
// `object reference` both spell "none" this way.
const int kNoRef = -1;

// ObjectDef class id, from documentation\generated\class-index.md.
const int kClassMeleeWeapon = 0x10031;


// ---------------------------------------------------------------------------
// The object/handle registry at 0x101326cc - decomp\src\player\spell_hotkeys.cpp
// and decomp\src\core\handle.cpp, unchanged.

#pragma pack(push, 2)
struct HandleSlot                 // 6 bytes - the -6 displacement and the *2
{                                 // index scale are only possible packed
    void *object;
    unsigned short extra;         // purpose unknown
};
#pragma pack(pop)

class HandleTable
{
public:
    virtual void Slot0();
    virtual void ReleaseHandle(unsigned short id);          // [vtable+0x04]
    virtual unsigned short CreateHandle(GameObject *owner); // [vtable+0x08]

    HandleSlot *m_slots;          // +0x04  (registry +0x14)
    int m_unknown_08;             // +0x08  (registry +0x18)
    int m_capacity;               // +0x0c  (registry +0x1c)
};

class ObjectRegistry
{
public:
    virtual void Slot0();
    virtual void Slot1();

    // [vtable+0x08]. The acquiring object, the address of its field at +0xec,
    // zero, and an object link; returns a game object or null. Identical call
    // to the one in spell_hotkeys.cpp, and no better established here than
    // there.
    virtual GameObject *CreateObject(GameObject *owner, void *anchor,
                                     int flags, int objectLink);

    char m_unknown_04[0x10 - 0x04];
    HandleTable m_handles;        // +0x10
};

extern ObjectRegistry *g_objectRegistry;   // 0x101326cc


// A one-based handle into the registry's slot array - handle.cpp, unchanged
// but for the registry spelling and the return type.
class Handle
{
public:
    GameObject *Resolve() const
    {
        if (m_id != 0)
            return (GameObject *)g_objectRegistry->m_handles.m_slots[m_id - 1].object;
        return 0;
    }

    unsigned short m_id;          // 0 = none
};


// ---------------------------------------------------------------------------
// A game object. +0x00 is a vptr (spell_hotkeys.cpp); no virtual of it is
// called here, so the slots are not declared. +0x0c is an unsigned short
// compared against the link index this function is working on, which makes it
// the object's own object link - that is the role, not a proven identity.

class GameObject
{
public:
    char           m_unknown_00[0x0c];
    unsigned short m_objectLink;  // +0x0c
    char           m_unknown_0e[0x14 - 0x0e];
    PropertyBlock *m_properties;  // +0x14
    char           m_unknown_18[0xec - 0x18];
    int            m_anchor_ec;   // +0xec - passed by address only
};


// The console - spell_hotkeys.cpp. Slot 3 is a __cdecl vararg, which is why
// `this` is pushed as the leftmost argument and the caller pops all twelve
// bytes.
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
    Console *m_console;           // +0x04
};

extern ConsoleHolder *g_consoleHolder;      // 0x1013269c


// ---------------------------------------------------------------------------
// The list at Player+0x138 - spell_hotkeys.cpp calls it the carried-object
// link list. Two more of its methods appear here, both __thiscall and neither
// established beyond what the call sites show.

class ObjectLinkList
{
public:
    // 0x1004c680. Given a start index and an ObjectDef class id, answers the
    // index of the next entry of that class, or -1.
    int FindNextOfClass(int start, int classId);

    // 0x1004c610. Given an index, writes out the entry. Only the first of the
    // two out-parameters is read here, and it is an object link.
    void GetEntry(int index, int *objectLink, int *extra);

    char m_unknown_00[0x14];
};


// ---------------------------------------------------------------------------
// Player. All the offsets used are 4-aligned or 2-aligned in a 4-aligned
// region, so no packing is needed for this function.

class Player
{
public:
    void UpdateScabbard();

    // Not defined here.
    //
    // 0x1005ac90 - matched in decomp\src\objectdef\property_ref.cpp as the
    // __stdcall free function GetRightPropertiesDefByIndex. It is called here
    // with `this` in ECX, so it is a member; see the header comment.
    ObjectDefEntry *GetRightPropertiesDefByIndex(int objectLink);

    // 0x10063be0. Five arguments: a channel, the object to put on it, and
    // three constants. "Attach" is the role the call site gives it.
    void AttachToChannel(int channel, GameObject *object, int a, int b, int c);

    // 0x100625f0, from spell_hotkeys.cpp.
    void SendEvent(int eventId, int param);

    void       *m_unknown_00;                 // +0x00
    GameObject *m_object;                     // +0x04
    char        m_unknown_08[0xd8 - 0x08];
    Handle      m_heldWeapon;                 // +0xd8 - resolves to the object
                                              //   currently in hand; role only
    Handle      m_scabbard;                   // +0xda - the object this
                                              //   function spawns and owns
    char        m_unknown_dc[0x138 - 0xdc];
    ObjectLinkList m_carried;                 // +0x138
};


// Sent when the player carries no melee weapon link at all. 11 is also the
// default of `MsgBlowUp` on both Player and Melee Weapon, which is a
// coincidence of the number and not a property read - it is an immediate here.
const int kEventNoMeleeWeapon = 0xb;


// 0x1005adf0, 376 bytes.
void Player::UpdateScabbard()
{
    int index = -1;
    int objectLink;
    int extra;
    ObjectDefEntry *def;

    do
    {
        index = m_carried.FindNextOfClass(index + 1, kClassMeleeWeapon);
        m_carried.GetEntry(index, &objectLink, &extra);

        if (index == -1 || objectLink == -1)
        {
            SendEvent(kEventNoMeleeWeapon, 0);
            return;
        }

        def = GetRightPropertiesDefByIndex(objectLink);
    }
    while (def == 0);

    int channel = *(int *)def->m_properties->GetValue(kOrdScabbardChan, kScalar);
    int scabbardLink =
        *(int *)def->m_properties->GetValue(kOrdScabbardWithSword, kScalar);

    GameObject *held = m_heldWeapon.Resolve();
    if (held != 0 && held->m_objectLink == objectLink)
        scabbardLink = *(int *)def->m_properties->GetValue(kOrdEmptyScabbard, kScalar);

    if (scabbardLink != kNoRef && channel != kNoRef)
    {
        GameObject *scabbard = g_objectRegistry->CreateObject(
            m_object, &m_object->m_anchor_ec, 0, scabbardLink);

        if (scabbard != 0)
        {
            AttachToChannel(channel, scabbard, 1, 0, 0);

            if (m_scabbard.m_id != 0)
            {
                g_objectRegistry->m_handles.ReleaseHandle(m_scabbard.m_id);
                m_scabbard.m_id = 0;
            }

            unsigned short handle =
                g_objectRegistry->m_handles.CreateHandle(scabbard);
            m_scabbard.m_id = handle;

            if (handle > g_objectRegistry->m_handles.m_capacity)
            {
                g_consoleHolder->m_console->Printf(
                    "Object link: %d beyond array bounds!  Could crash!",
                    handle);
            }
        }
    }
}
