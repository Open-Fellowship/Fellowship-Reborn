// targeting.cpp - Fellowship.rfl.
//
//   0x10050aa0  459 bytes  __thiscall  int Player::UpdateTargeting()
//
// THE CLASS IS ESTABLISHED by the two ordinals the tail reads through the
// standard property accessor: 0x5c = 92 and 0x58 = 88. On Player those are
// TargetingToggleTime ("Target Button Toggle Time", float, 0.2) and
// AimStateTimeout ("Pause In Aimed State After Attack (sec.)", float, 2.0),
// both in the "Targeting Parameters" group, and both are loaded with FLD -
// which is the type test that separates Player from Control Input Names.
// 0x100625f0 is the same Player::SendEvent that spell_hotkeys.cpp calls.
//
// WHAT IT DOES. Two timers, both using -1.0f as "not running":
//
//   +0x3c  time the target button has been released (guarded by
//          TargetingToggleTime)
//   +0x38  time since the aimed state was last refreshed (guarded by
//          AimStateTimeout)
//
// Each frame it polls control 0x42d through the input object at 0x101326f4,
// advances whichever timer is running by the frame delta, and:
//
//   * button released while the release timer is off -> start it at 0
//   * button pressed while it is running -> stop it, toggle the target lock,
//     and republish the player target link into the handle at +0x76 of the
//     logic hanging off the registry object at +0xbc
//   * unless flag 0x800 is set, if the button has been up longer than
//     TargetingToggleTime and the aim pause has expired (or was never armed),
//     send event 0x0e and answer 0 - "no longer aiming". Otherwise 1.
//
// Reused unchanged from already-matched sources, because the shapes are
// established:
//
//   ReadProperty      decomp\src\player\stats.cpp - the read through the
//       ADDRESS of the block slot (ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX)
//   Handle / HandleTable / Console / ConsoleHolder
//                     decomp\src\player\spell_hotkeys.cpp and
//                     decomp\src\core\handle.cpp - the 6-byte packed slot, the
//       release/create virtuals at [vtable+4] and [vtable+8], and the __cdecl
//       vararg console at [vtable+0xc]
//   FrameTimer        decomp\src\player\health_regen.cpp - the global at
//       0x101326a8, whose +0x04 is the frame delta in seconds
//
// Handle::Resolve here is a longer form than the one in core\handle.cpp: the
// same one-based lookup, but preceded by a bounds check that prints
// "May exhibit odd behavior!" and executes INT3, and followed by a
// release-and-clear when the slot turns out to be empty. core\handle.cpp is
// the same code with both debug arms compiled out, so whatever header held it
// had them under a build switch. Both must have been visible to this
// translation unit; the integrator should fold them.
//
// NOT established, and named for role only: the input object at 0x101326f4 and
// its virtual at [vtable+0x6c]; the registry field at +0xbc and the +0x76
// handle in the logic it points at; Player::ToggleTargetLock (0x10062f00, no
// arguments); the state word at Player+0x2e whose ADDRESS the poll receives;
// and event 0x0e.
//
// TWO SPELLINGS ARE LOAD-BEARING and are the reason for the casts below.
//
//   The -1.0f sentinel on +0x3c is compared and stored as a BIT PATTERN, not
//   as a float: CMP dword ptr [ESI+0x3c],EDX with EDX = 0xbf800000 hoisted
//   above the branch and reused for the store at 0x10050b21. A float
//   `== -1.0f` compiles to FLD/FCOMP and cannot produce that. The same field
//   is read with FLD three lines later, so it is a float that the sentinel
//   paths address by bits.
//
//   The float comparisons use two different VC6 sequences, and each is forced
//   by how it is written:
//       x >  y   FLD x / FCOMP y / AND EAX,0x4100 / JNZ   (the -1.0f guards)
//       x <  y   FLD x / FCOMP y / TEST AH,5      / JP    (the property tests)
//   health_regen.cpp records the third, `!(x <= y)` giving TEST AH,0x41 / JNP.
//   The operand loaded with FLD is the left-hand side in every case.

class GameObject;


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
// The console - decomp\src\player\spell_hotkeys.cpp, unchanged. Slot 3 is a
// __cdecl vararg, which is why `this` is pushed as the leftmost argument and
// the caller pops all twelve bytes.

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
    void    *m_unknown_00;
    Console *m_console;          // +0x04
};

extern ConsoleHolder *g_consoleHolder;      // 0x1013269c


// ---------------------------------------------------------------------------
// The object/handle registry at 0x101326cc - spell_hotkeys.cpp, plus the
// object pointer at +0xbc that this function is the first to touch.

#pragma pack(push, 2)
struct HandleSlot                // 6 bytes - the -6 displacement and the *2
{                                // index scale are only possible packed
    GameObject     *object;
    unsigned short  extra;       // purpose unknown
};
#pragma pack(pop)

class HandleTable
{
public:
    virtual void Slot0();
    virtual void ReleaseHandle(unsigned short id);           // [vtable+0x04]
    virtual unsigned short CreateHandle(GameObject *owner);  // [vtable+0x08]

    HandleSlot *m_slots;         // +0x04  (registry +0x14)
    int         m_unknown_08;    // +0x08  (registry +0x18)
    int         m_capacity;      // +0x0c  (registry +0x1c) - warned against
};

class ObjectRegistry
{
public:
    char        m_unknown_00[0x10];
    HandleTable m_handles;                     // +0x10
    char        m_unknown_20[0xbc - 0x20];
    GameObject *m_aimReceiver;                 // +0xbc - role unestablished
};

extern ObjectRegistry *g_objectRegistry;    // 0x101326cc


// ---------------------------------------------------------------------------
// The logic object hanging off a GameObject at +0xc8, and the game object
// itself - spell_hotkeys.cpp, trimmed to what is used here.

#pragma pack(push, 2)

// A one-based handle into the registry slot array.
class Handle
{
public:
    GameObject *Resolve();
    void        Set(GameObject *object);

    unsigned short m_id;         // 0 = none
};

class ObjectLogic
{
public:
    char   m_unknown_00[0x76];
    Handle m_aimTarget;          // +0x76 - role unestablished
};

#pragma pack(pop)

class GameObject
{
public:
    char           m_unknown_00[0x14];
    PropertyBlock *m_properties;               // +0x14
    char           m_unknown_18[0xc8 - 0x18];
    ObjectLogic   *m_logic;                    // +0xc8
};


// core\handle.cpp is this function with the two debug arms compiled out.
__inline GameObject *Handle::Resolve()
{
    if (m_id != 0)
    {
        if (m_id > g_objectRegistry->m_handles.m_capacity + 1)
        {
            g_consoleHolder->m_console->Printf(
                "Object link: %d beyond array bounds!  May exhibit odd behavior!",
                m_id);
            __asm int 3
        }

        GameObject *object =
            g_objectRegistry->m_handles.m_slots[m_id - 1].object;
        if (object == 0)
        {
            g_objectRegistry->m_handles.ReleaseHandle(m_id);
            m_id = 0;
        }
        return object;
    }

    return 0;
}

// The release-then-create pair, identical to the owner-handle assignment at
// the end of Player::AcquireObject in spell_hotkeys.cpp.
__inline void Handle::Set(GameObject *object)
{
    if (m_id != 0)
    {
        g_objectRegistry->m_handles.ReleaseHandle(m_id);
        m_id = 0;
    }

    if (object != 0)
    {
        m_id = g_objectRegistry->m_handles.CreateHandle(object);
        if (m_id > g_objectRegistry->m_handles.m_capacity)
        {
            g_consoleHolder->m_console->Printf(
                "Object link: %d beyond array bounds!  Could crash!", m_id);
        }
    }
}


// ---------------------------------------------------------------------------
// The input object at 0x101326f4. Only [vtable+0x6c] - slot 27 - is used: it
// takes the address of a caller-owned state word, a control id and a flag, and
// answers non-zero while the control is pressed. Nothing here identifies it
// further; the 27 slots ahead of it exist only to place it.

class InputManager
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
    virtual void Slot24();  virtual void Slot25();  virtual void Slot26();
    virtual int  PollControl(void *state, int control, int flags);  // +0x6c
};

extern InputManager *g_inputManager;        // 0x101326f4


// The global at 0x101326a8 - health_regen.cpp. Only +0x04 is read, as the
// frame delta in seconds.
struct FrameTimer
{
    void  *m_unknown_00;
    float  m_deltaSeconds;       // +0x04
};

extern FrameTimer *g_frameTimer;            // 0x101326a8


// ---------------------------------------------------------------------------

// -1.0f means "this timer is not running". The bit-pattern form is what the
// compare and the two sentinel stores actually use; see the header comment.
const float kTimerOff     = -1.0f;
const int   kTimerOffBits = (int)0xbf800000;

// Player property ordinals, from the ObjectDef schema for class 0x1000e.
// Group "Targeting Parameters".
const int kOrdAimStateTimeout     = 88;   // "Pause In Aimed State After Attack (sec.)"
const int kOrdTargetingToggleTime = 92;   // "Target Button Toggle Time"

const int kControlTarget      = 0x42d;    // the control id polled each frame
const unsigned int kFlagNoAim = 0x800;    // Player+0x54, vetoes the exit test
const int kEventLeaveAim      = 0x0e;     // sent when the aimed state ends


// Player. +0x2e and +0x122 are not 4-aligned, so the class is packed; pack(2)
// is the loosest setting that puts them there.
#pragma pack(push, 2)
class Player
{
public:
    int UpdateTargeting();

    // Not defined here - __thiscall members reached by CALL rel32.
    void ToggleTargetLock();                 // 0x10062f00, no arguments
    void SendEvent(int eventId, int param);  // 0x100625f0 - spell_hotkeys.cpp

    void        *m_unknown_00;               // +0x00
    GameObject  *m_object;                   // +0x04
    char         m_unknown_08[0x2e - 0x08];
    int          m_targetButtonState;        // +0x2e - the poll state word,
                                             //         passed by address only
    char         m_unknown_32[0x38 - 0x32];
    float        m_aimStateTimer;            // +0x38
    float        m_buttonUpTimer;            // +0x3c
    char         m_unknown_40[0x54 - 0x40];
    unsigned int m_flags;                    // +0x54
    char         m_unknown_58[0x122 - 0x58];
    Handle       m_targetLink;               // +0x122
};
#pragma pack(pop)


// 0x10050aa0, 459 bytes.
int Player::UpdateTargeting()
{
    int pressed = g_inputManager->PollControl(&m_targetButtonState,
                                              kControlTarget, 0);

    if (m_aimStateTimer > kTimerOff)
        m_aimStateTimer = g_frameTimer->m_deltaSeconds + m_aimStateTimer;

    if (pressed == 0 && *(int *)&m_buttonUpTimer == kTimerOffBits)
    {
        *(int *)&m_buttonUpTimer = 0;
    }
    else if (m_buttonUpTimer > kTimerOff)
    {
        m_buttonUpTimer = g_frameTimer->m_deltaSeconds + m_buttonUpTimer;

        if (pressed != 0)
        {
            *(int *)&m_buttonUpTimer = kTimerOffBits;
            ToggleTargetLock();

            ObjectLogic *receiver = g_objectRegistry->m_aimReceiver->m_logic;
            receiver->m_aimTarget.Set(m_targetLink.Resolve());
        }
    }

    if ((m_flags & kFlagNoAim) == 0)
    {
        if (*(float *)ReadProperty(&m_object->m_properties,
                                   kOrdTargetingToggleTime, kScalar)
                < m_buttonUpTimer)
        {
            if (*(float *)ReadProperty(&m_object->m_properties,
                                       kOrdAimStateTimeout, kScalar)
                        < m_aimStateTimer
                || m_aimStateTimer < 0.0f)
            {
                SendEvent(kEventLeaveAim, 0);
                return 0;
            }
        }
    }

    return 1;
}
