// Fellowship.rfl - Player weapon equip/stow.
//
//   0x1005a790  273 bytes  Player::EquipWeapon
//   0x1005a8b0  318 bytes  Player::StowWeapon
//
// THE FILE NAME IS WRONG AND THE ASSIGNMENT'S PROPERTY TABLE IS WRONG.
// Neither function reads InitialHealth or Difficulty, and neither touches
// health at all. See the block below; the integrator should rename this file
// to player\weapon_equip.cpp.
//
// WHAT THE PROPERTIES ACTUALLY ARE
//
// ordmap.py attributes both reads to Player because Player is the only class
// with enough properties to survive the count test on a same-this call chain.
// That is exactly the failure ORDINAL-MAP.md documents for 0x1002c1c0: the
// object that is compared and the object that is read are different objects.
// Here the read object is not `this` at all - it is the ObjectDefEntry that
// GetRightPropertiesDef (0x1005ad00, matched in objectdef\property_ref.cpp)
// returns, and the block read is that record's own at +0x08.
//
// property_ref.cpp already says what that record is: ordinal 34 on `Ranged
// Weapon` and `Melee Weapon` is `RightProperties`, an object reference
// accepting `Player Weapon Properties` (ObjectDef class 0x1002e). Read the two
// ordinals against that class instead and everything in these 591 bytes lines
// up:
//
//   ordinal 0  WeaponChan   "Weapon Attachment",                channel
//   ordinal 2  AmmoChannel  "Ammo Attachment (for Ranged Wpn)", channel
//
// Four independent things confirm it and nothing contradicts it:
//
//   * both values are dereferenced as signed ints and compared against -1,
//     which is what `channel` defaults to ("none"). InitialHealth is a float
//     defaulting to 100.0 and is never compared to -1 as an integer, and
//     Difficulty is an object reference to `Difficulty Modifier`.
//   * the failure message for ordinal 0 being -1 is the string at 0x1012a0d4,
//     "Weapon's node channel is not set!" - it names the property.
//   * both values are then handed straight to the channel interface at
//     GameObject+0x10: attach an object to it, detach it, ask what is on it.
//   * 0x1005adf0, the function both of these tail-call, reads ordinals 1, 3
//     and 4 off the same record through GetRightPropertiesDefByIndex. On
//     `Player Weapon Properties` those are ScabbardChan, EmptyScabbard and
//     ScabbardWithSword, and it picks EmptyScabbard when a weapon is currently
//     held and ScabbardWithSword when it is not. That is a scabbard, property
//     for property. ordmap names those same three ordinals MaxHealth,
//     Invulnerable and InvulnerablilityOnMsg.
//
// So this is the falsification ORDINAL-MAP.md invites, and it lands on three
// functions at once. The Player attribution for 0x1005a790, 0x1005a8b0 and
// 0x1005adf0 in generated\ordinal-map.md should be withdrawn: all three read
// `Player Weapon Properties`. The *class of `this`* is still Player - that is
// established by the call chain from Player::AcquireObject (0x1005a1c0) and by
// the shared layout with health_regen.cpp and spell_hotkeys.cpp - but the
// class of the object whose properties they read is not.
//
// WHAT THE TWO FUNCTIONS DO
//
//   EquipWeapon  resolve the object link; stow whatever is held; find the
//                weapon's `Player Weapon Properties` record; attach the weapon
//                to its WeaponChan node; replace the held-weapon handle;
//                update the scabbard. Returns the weapon, or null.
//   StowWeapon   the inverse: detach the weapon from WeaponChan, destroy
//                anything hanging off AmmoChannel, release the handle, tell
//                the weapon's logic, update the scabbard. Returns 1 unless it
//                bailed out. Its name is not a guess - the string at
//                0x1012a0f8 is "StowWeapon was called, but we didn't have any
//                weapons equipped!".
//
// EquipWeapon was provisionally named that in spell_hotkeys.cpp from its one
// call site. These bytes support it: it reads a property literally called
// "Weapon Attachment" and attaches the object to it. The name stands, but the
// return type there is wrong - it returns the game object, not void.
//
// 0x1005ad00 IS __thiscall, NOT __stdcall. property_ref.cpp matched it as a
// free `__stdcall` function, and it does match, because the body never touches
// ECX and a __thiscall that ignores `this` emits identical bytes. Both call
// sites in this file settle it the other way: each is `PUSH <object> /
// MOV ECX,ESI / CALL`, and VC6 at /O2 does not emit a dead ECX load. It is a
// member of Player taking the object as its one argument. That does not
// invalidate property_ref.cpp's bytes, but its signature and its file should
// move.
//
// EVERY OTHER NAME BELOW IS INVENTED, and the shared vocabulary comes from
// already-matched files rather than being reinvented here: ObjectRegistry,
// HandleTable, HandleSlot, Handle, Console, ConsoleHolder, GameObject,
// ObjectLogic, PropertyBlock and ObjectDefEntry are all spelled as in
// player\spell_hotkeys.cpp, core\handle.cpp and objectdef\property_ref.cpp.
// The declarations are local copies on purpose; nothing shared is edited.
//
// NOT ESTABLISHED, named for position and role only:
//
//   Player+0xd0        an int reset to -1 at the top of EquipWeapon and never
//                      read here. -1 is the engine's "no channel", but nothing
//                      in these bytes says that is what it holds.
//   GameObject+0x10    the channel interface. Three virtuals are exercised -
//                      attach(channel, object, 0, 0), detach(channel), and
//                      lookup(channel) -> object - and the argument that is
//                      always a WeaponChan/AmmoChannel value is what names it.
//   GameObject vtable  slot 5 is called on a weapon whose properties record
//                      would not resolve and on the ammo attachment as it is
//                      thrown away, so its role here is "discard"; the same
//                      slot is called on a freshly created object in
//                      spell_hotkeys.cpp, where it is named OnAcquired. One of
//                      the two names is wrong and these bytes cannot say which,
//                      so it is left as Slot05 here.
//   GameObject vtable  slot 47 takes a string and is only ever reached on a
//                      misauthored-data path, so it is a diagnostic.
//   ObjectLogic 0x10026500  the stowed weapon's logic, handed the player's own
//                      game object. Not identified.
//   Player 0x1005a5c0  turns an object link into a game object. Not identified
//                      beyond that.
//   Player 0x1005adf0  the scabbard update described above. The name is from
//                      its own property reads, not from these call sites.
//   Console slot 8     a second __cdecl vararg printer alongside slot 3.
//                      Which is which severity is not established.
//
// THE INT3 at 0x1005a8ec is real and is in the middle of the function; Ghidra
// loses six bytes of alignment after it and resumes at 0x1005a8f3. The bytes
// are `83 c4 0c` (the vararg cleanup), `cc`, then `8b 0d cc 26 13 10`, a reload
// of the registry global on the error path only. It is written here as an
// inline `int 3` after the out-of-bounds complaint.

// ---------------------------------------------------------------------------
// The authored-property block. Only slot 2 is exercised; the two ahead of it
// are declared purely to place it at [vtable+0x08].

class GameObject;

class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);   // [vtable+0x08]
};

const int kScalar = -1;          // element index for a non-list property

// `Player Weapon Properties`, ObjectDef class 0x1002e. Real names, from the
// rfl's own table; see the header comment for why this is the right class.
const int kOrdWeaponChan  = 0;   // "Weapon Attachment",                channel
const int kOrdAmmoChannel = 2;   // "Ammo Attachment (for Ranged Wpn)", channel

const int kChannelNone = -1;     // a channel property's "none"


// One ObjectDef record. 36 bytes; only +0x04 and +0x08 are established, and
// only +0x08 is read here. The field holds the pointer, so the direct spelling
// `entry->m_properties->GetValue(...)` is the one that reproduces the two
// instructions - see the note in WORKER-BRIEF.md.
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


// ---------------------------------------------------------------------------
// The channel interface at GameObject+0x10. Slots 0..6 and 9 exist only to
// place the three that are called.

class ChannelTable
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();
    virtual void Slot4();
    virtual void Slot5();
    virtual void Slot6();

    // [vtable+0x1c]. The two trailing zeros are always zero at both call sites
    // in the rfl and their meaning is not established.
    virtual void Attach(int channel, GameObject *object, int a, int b);

    virtual void Detach(int channel);                     // [vtable+0x20]
    virtual void Slot9();
    virtual GameObject *GetAttached(int channel);         // [vtable+0x28]
};


// The logic object at GameObject+0xc8 - spell_hotkeys.cpp's ObjectLogic. Only
// one non-virtual method is reached from here.
class ObjectLogic
{
public:
    void OnWeaponStowed(GameObject *owner);   // 0x10026500, __thiscall
};


// A game object. Slots 0..46 exist only to place the two that are called.
class GameObject
{
public:
    virtual void Slot00();  virtual void Slot01();  virtual void Slot02();
    virtual void Slot03();  virtual void Slot04();

    // [vtable+0x14]. See the header comment - role only, name deliberately
    // positional because spell_hotkeys.cpp reads the same slot differently.
    virtual void Slot05();

    virtual void Slot06();  virtual void Slot07();  virtual void Slot08();
    virtual void Slot09();  virtual void Slot10();  virtual void Slot11();
    virtual void Slot12();  virtual void Slot13();  virtual void Slot14();
    virtual void Slot15();  virtual void Slot16();  virtual void Slot17();
    virtual void Slot18();  virtual void Slot19();  virtual void Slot20();
    virtual void Slot21();  virtual void Slot22();  virtual void Slot23();
    virtual void Slot24();  virtual void Slot25();  virtual void Slot26();
    virtual void Slot27();  virtual void Slot28();  virtual void Slot29();
    virtual void Slot30();  virtual void Slot31();  virtual void Slot32();
    virtual void Slot33();  virtual void Slot34();  virtual void Slot35();
    virtual void Slot36();  virtual void Slot37();  virtual void Slot38();
    virtual void Slot39();  virtual void Slot40();  virtual void Slot41();
    virtual void Slot42();  virtual void Slot43();  virtual void Slot44();
    virtual void Slot45();  virtual void Slot46();

    // [vtable+0xbc]. Reached only when authored data is missing.
    virtual void ReportError(const char *message);

    char           m_unknown_04[0x10 - 0x04];
    ChannelTable  *m_channels;       // +0x10
    PropertyBlock *m_properties;     // +0x14 - not read by these two
    char           m_unknown_18[0xc8 - 0x18];
    ObjectLogic   *m_logic;          // +0xc8
};


// ---------------------------------------------------------------------------
// The object/handle registry at 0x101326cc - spell_hotkeys.cpp's spelling.

#pragma pack(push, 2)
struct HandleSlot                // 6 bytes; the -6 displacement and the *2
{                                // index scale are only possible packed
    GameObject    *object;
    unsigned short extra;        // purpose unknown
};
#pragma pack(pop)

class HandleTable
{
public:
    virtual void Slot0();
    virtual void ReleaseHandle(unsigned short id);           // [vtable+0x04]
    virtual unsigned short CreateHandle(GameObject *owner);  // [vtable+0x08]

    HandleSlot *m_slots;         // +0x04  (registry +0x14)
    int m_unknown_08;            // +0x08  (registry +0x18)
    int m_capacity;              // +0x0c  (registry +0x1c)
};

class ObjectRegistry
{
public:
    char        m_unknown_00[0x10];
    HandleTable m_handles;       // +0x10, a polymorphic subobject
};

extern ObjectRegistry *g_objectRegistry;    // 0x101326cc


// A one-based handle into the registry's slot array - core\handle.cpp. Both
// forms are spelled inline; the out-of-line copy at 0x100051c0 is not what
// these two call.
//
// Resolve is core\handle.cpp verbatim. ResolveOrClear is the same lookup with
// the two debug arms left in: a capacity check that complains and breaks, and
// a release-and-clear when the slot it names is empty. EquipWeapon inlines the
// short one and StowWeapon the long one, so both are present in the image.
class Handle
{
public:
    GameObject *Resolve() const
    {
        if (m_id != 0)
            return g_objectRegistry->m_handles.m_slots[m_id - 1].object;

        return 0;
    }

    GameObject *ResolveOrClear();

    unsigned short m_id;         // 0 = none
};


// The console - spell_hotkeys.cpp. Both printers are __cdecl varargs, which is
// why `this` is pushed as the leftmost argument and the caller pops it.
class Console
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void __cdecl Printf(const char *format, ...);    // [vtable+0x0c]
    virtual void Slot4();
    virtual void Slot5();
    virtual void Slot6();
    virtual void Slot7();
    virtual void __cdecl Message(const char *format, ...);   // [vtable+0x20]
};

struct ConsoleHolder
{
    void    *m_unknown_00;
    Console *m_console;          // +0x04
};

extern ConsoleHolder *g_consoleHolder;      // 0x1013269c


// The checked form. The `+ 1` on the capacity is literal, and is not what
// EquipWeapon's own bounds check does with the same field - that one compares
// against the capacity itself.
__inline GameObject *Handle::ResolveOrClear()
{
    if (m_id != 0)
    {
        if (m_id > g_objectRegistry->m_handles.m_capacity + 1)
        {
            g_consoleHolder->m_console->Printf(
                "Object link: %d beyond array bounds!  May exhibit odd behavior!",
                m_id);
            _asm int 3
        }

        GameObject *object =
            g_objectRegistry->m_handles.m_slots[m_id - 1].object;
        if (object != 0)
            return object;

        g_objectRegistry->m_handles.ReleaseHandle(m_id);
        m_id = 0;
    }

    return 0;
}


// ---------------------------------------------------------------------------
// Player. +0x00, +0x04 and the general shape are as in health_regen.cpp and
// spell_hotkeys.cpp; +0xd0 and +0xd8 are new here.

class Player
{
public:
    GameObject *EquipWeapon(int objectLink);
    int         StowWeapon();

    // Not defined here - three __thiscall members of Player reached by
    // CALL rel32, named for what the call sites do with them.
    GameObject     *ResolveObjectLink(int objectLink);          // 0x1005a5c0
    ObjectDefEntry *GetWeaponPropertiesDef(GameObject *object); // 0x1005ad00
    void            UpdateScabbard();                           // 0x1005adf0

    void       *m_unknown_00;             // +0x00
    GameObject *m_object;                 // +0x04
    char        m_unknown_08[0xd0 - 0x08];
    int         m_field_d0;               // +0xd0 - reset to -1, never read here
    int         m_unknown_d4;             // +0xd4
    Handle      m_weaponHandle;           // +0xd8 - the weapon being held
};


// 0x1005a8b0, 318 bytes.
//
// The bounds check, the int 3 and the release-when-empty are all inside the
// inlined Handle::ResolveOrClear above; what is left here is the stow itself.
// The compiler merges the helper's own `object != 0` test with this function's
// `weapon != 0`, which is why there is exactly one TEST and it jumps straight
// into the body.
int Player::StowWeapon()
{
    GameObject *weapon = m_weaponHandle.ResolveOrClear();
    if (weapon != 0)
    {
        ObjectDefEntry *def = GetWeaponPropertiesDef(weapon);
        if (def == 0)
            return 0;

        // The vtable temp here is the one byte pair that will not reproduce:
        // the original loads it into EAX (8b 01 / ff 50 08), VC6 picks EDX
        // (8b 11 / ff 52 08) whatever shape the source takes. See the notes at
        // the end of the file.
        int channel = *(int *)def->m_properties->GetValue(kOrdWeaponChan,
                                                          kScalar);
        if (channel == kChannelNone)
            return 0;

        m_object->m_channels->Detach(channel);

        int ammoChannel = *(int *)def->m_properties->GetValue(kOrdAmmoChannel,
                                                              kScalar);
        if (ammoChannel != kChannelNone)
        {
            GameObject *ammo = m_object->m_channels->GetAttached(ammoChannel);
            if (ammo != 0)
            {
                m_object->m_channels->Detach(ammoChannel);
                ammo->Slot05();
            }
        }

        if (m_weaponHandle.m_id != 0)
        {
            g_objectRegistry->m_handles.ReleaseHandle(m_weaponHandle.m_id);
            m_weaponHandle.m_id = 0;
        }

        weapon->m_logic->OnWeaponStowed(m_object);
        UpdateScabbard();
        return 1;
    }

    g_consoleHolder->m_console->Message(
        "StowWeapon was called, but we didn't have any weapons equipped!\n");
    return 1;
}


// 0x1005a790, 273 bytes.
GameObject *Player::EquipWeapon(int objectLink)
{
    GameObject *weapon = ResolveObjectLink(objectLink);
    if (weapon == 0)
        return 0;

    m_field_d0 = -1;

    if (m_weaponHandle.Resolve() != 0)
        StowWeapon();

    ObjectDefEntry *def = GetWeaponPropertiesDef(weapon);
    if (def == 0)
    {
        weapon->Slot05();
        return 0;
    }

    int channel = *(int *)def->m_properties->GetValue(kOrdWeaponChan, kScalar);
    if (channel == kChannelNone)
    {
        m_object->ReportError("Weapon's node channel is not set!");
        return 0;
    }

    m_object->m_channels->Attach(channel, weapon, 0, 0);

    if (m_weaponHandle.m_id != 0)
    {
        g_objectRegistry->m_handles.ReleaseHandle(m_weaponHandle.m_id);
        m_weaponHandle.m_id = 0;
    }

    m_weaponHandle.m_id = g_objectRegistry->m_handles.CreateHandle(weapon);
    if (m_weaponHandle.m_id > g_objectRegistry->m_handles.m_capacity)
    {
        g_consoleHolder->m_console->Printf(
            "Object link: %d beyond array bounds!  Could crash!",
            m_weaponHandle.m_id);
    }

    UpdateScabbard();
    return weapon;
}


// ---------------------------------------------------------------------------
// THE REMAINDER, and what was ruled out.
//
// EquipWeapon matches exactly. StowWeapon differs in two bytes and only two,
// at 0x1005a95b: the vtable temp for the FIRST of its two GetValue calls.
//
//     original   8b 01  MOV EAX,[ECX]   ff 50 08  CALL [EAX+8]
//     ours       8b 11  MOV EDX,[ECX]   ff 52 08  CALL [EDX+8]
//
// Same instructions, same operands, different scratch register, and the length
// is identical so nothing after it shifts. The SECOND GetValue call, twenty
// bytes later and syntactically identical, picks EAX in both. So does the same
// construct in EquipWeapon. Only this one site disagrees, and at it EAX holds a
// dead copy of `def` (the live copy is in EDI) in the original and in ours
// alike.
//
// Ruled out, each rebuilt and compared:
//
//   * the block layout. The `weapon == 0` arm placed first with the body in an
//     `else` is what puts the body out of line after the shared tail; written
//     the other way round the JZ at the top grows to a near jump and all 318
//     bytes shift. That fixed 253 of the 255 bytes that first differed.
//   * spelling the handle lookup longhand versus inlining it as
//     Handle::ResolveOrClear. Byte-identical either way, including the merged
//     `object != 0` / `weapon != 0` test. ResolveOrClear is kept because it
//     explains the capacity check and the int 3 as the debug arms of the same
//     helper core\handle.cpp holds in its stripped form.
//   * routing both property reads through one __inline helper taking (def,
//     ordinal). Byte-identical.
//   * hoisting `def`, `channel` and `ammoChannel` to declarations at the top of
//     the function, in case VC6's enregistration followed declaration order.
//     Byte-identical.
//   * removing the inline `int 3` to see whether the asm block was constraining
//     the allocator. It is not: the register choice is unchanged and only the
//     one byte and the following offsets move.
//
// That leaves it as register allocation, which WORKER-BRIEF.md names as the
// stop-and-report case. Everything reachable from source is already right.
