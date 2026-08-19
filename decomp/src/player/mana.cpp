// Fellowship.rfl - the Player mana setter.
//
//   0x1005c500  262 bytes  Player::SetMana   261 of 262 bytes reproduced
//
// BUILD: this file needs /GX in addition to /O2 /Gy.
//
// This is the first function in the project with a Win32 exception frame, and
// what it turns out to be is the main finding here. The prologue
//
//     PUSH -1 / PUSH 0x100e7a20 / MOV EAX,FS:[0] / PUSH EAX / MOV FS:[0],ESP
//
// pushes only THREE words, not the four an __except_handler3 frame needs, so
// it is not __try/__except. 0x100e7a20 (.text) holds
//
//     MOV EAX,0x100f74e8 / JMP 0x100d8c15
//
// which is the per-function __ehhandler thunk: 0x100f74e8 (.rdata) begins
// 0x19930520, the MSVC EH FuncInfo magic, and 0x100d8c15 begins PUSH EBP /
// MOV EBP,ESP / SUB ESP,4 / PUSH EBX / PUSH ESI / PUSH EDI / CLD /
// MOV [EBP-4],EAX, which is __CxxFrameHandler. So this is C++ exception
// handling, and the FuncInfo says maxState 2, nTryBlocks 0 - there is no
// try/catch in the source at all. The frame is here only because the function
// builds a local object with a destructor, in a module compiled with /GX.
//
// /GX emits nothing whatever in a function with no destructible local, which
// is why none of the functions matched before this one revealed it. Adding it
// to the project-wide flag set should be free.
//
// The unwind map at 0x100f74d8 has two entries, {toState -1, 0x100e7a10} and
// {toState -1, 0x100e7a18}; those funclets are LEA ECX,[EBP-0x10] / JMP and
// LEA ECX,[EBP-0x18] / JMP, into two different destructors. The frame has no
// real EBP, but __CxxFrameHandler reconstructs one as (registration node + 12)
// = the entry ESP, and against that the funclets name entry-ESP-0x18 (the
// 12-byte message this function builds) and entry-ESP-0x10 (its member at +8).
// Two entries both unwinding to -1 is the partially-constructed-object shape:
// state 0 = the member alone, state 1 = the whole message. The body stores
// state 1 while the message is alive, which is the number that could not be
// reproduced - see the note on Message below.

// ---------------------------------------------------------------------------
// The property-read machinery. Copied verbatim from player\stats.cpp, which
// holds the matched Player::GetMaxMana at 0x1005c4e0 - the function
// immediately before this one. GetMaxMana is inlined into this function's
// body (ADD EAX,0x14 / MOV EAX,[EAX] / ... / CALL [EDX+8] / FLD [EAX]), which
// only happens if the two were in one translation unit, so the integrator
// should fold stats.cpp and this file together.
// ---------------------------------------------------------------------------

class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);
};

class GameObject
{
public:
    char unknown_00[0x14];
    PropertyBlock *properties;   // +0x14
};

inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar     = -1;
const int kOrdMaxMana = 139;     // Player "Maximum Mana", float, default 0.0

// ---------------------------------------------------------------------------
// The handle released by the message's destructor. Identity unestablished:
// the global at 0x101326cc owns a polymorphic subobject at +0x10 and the call
// goes through its [vtable+4] with the handle as its only argument.
// ---------------------------------------------------------------------------

class HandleTable
{
public:
    virtual void Slot0();
    virtual void Release(unsigned short handle);   // [vtable+4]
};

class HandleTableOwner
{
public:
    char unknown_00[0x10];
    HandleTable table;           // +0x10
};

extern HandleTableOwner *g_handleTableOwner;       // 0x101326cc

// A handle member that frees itself. Its zero-initialising constructor and its
// destructor are what create the two EH states.
class MessageHandle
{
public:
    MessageHandle() { m_handle = 0; }
    ~MessageHandle()
    {
        if (m_handle != 0)
        {
            g_handleTableOwner->table.Release(m_handle);
            m_handle = 0;
        }
    }

    unsigned short m_handle;
};

// The message pushed to the message system. 12 bytes: an id, one parameter and
// the self-freeing handle at +8. Bytes +10..+11 are never touched by this
// function.
//
// This class is where the one unreproduced byte lives. As written it gives the
// function a single EH state, so the store before the Send call comes out
// MOV [ESP+0x28],0 where the original has MOV [ESP+0x28],1 - the original's
// state 0 is the partially-constructed message, which VC6 only numbers when
// the constructor contains a call it cannot inline; the state-0 store itself
// is then dead and removed, leaving just the 1. Every shape tried that
// produced two states either emitted a visible call or numbered the states the
// other way round (see the report). The single byte then cascades: it is a
// fourth use of the constant 0 in the function, and at four uses VC6 hoists
// zero into EDI, which shortens eleven instructions and shifts everything
// after offset 0x5c.
class Message
{
public:
    Message(int id, int param) { m_id = id; m_param = param; }

    int           m_id;          // +0
    int           m_param;       // +4
    MessageHandle m_handle;      // +8
};

const int kMessageManaChanged = 0x200d;

// The message system. Only [vtable+0x10] is exercised.
class MessageSystem
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();
    virtual void Send(GameObject *object, Message *message, int a, int b);
};

extern MessageSystem *g_messageSystem;             // 0x101326b8

// ---------------------------------------------------------------------------
// The mana bar the player's mana is mirrored into. Only the float at +0xa4 and
// the virtual at [vtable+0x70] are established.
// ---------------------------------------------------------------------------

class ManaBar
{
public:
    virtual void Slot00();  virtual void Slot01();  virtual void Slot02();
    virtual void Slot03();  virtual void Slot04();  virtual void Slot05();
    virtual void Slot06();  virtual void Slot07();  virtual void Slot08();
    virtual void Slot09();  virtual void Slot0a();  virtual void Slot0b();
    virtual void Slot0c();  virtual void Slot0d();  virtual void Slot0e();
    virtual void Slot0f();  virtual void Slot10();  virtual void Slot11();
    virtual void Slot12();  virtual void Slot13();  virtual void Slot14();
    virtual void Slot15();  virtual void Slot16();  virtual void Slot17();
    virtual void Slot18();  virtual void Slot19();  virtual void Slot1a();
    virtual void Slot1b();
    virtual void Refresh();                        // [vtable+0x70]

    char  unknown_04[0xa0];
    float m_value;                                 // +0xa4
};

// ---------------------------------------------------------------------------
// Player. The members reached here are two-byte aligned but not four, so the
// class is packed.
// ---------------------------------------------------------------------------

#pragma pack(push, 2)
class Player
{
public:
    void       *unknown_00;      // +0x000
    GameObject *object;          // +0x004
    char        unknown_08[0x7e];
    float       m_mana;          // +0x086
    char        unknown_8a[0x2f0];
    ManaBar    *m_manaBar;       // +0x37a

    float GetMaxMana();
    void  SetMana(float mana);
};
#pragma pack(pop)

// 0x1005c4e0, 25 bytes. Matched in player\stats.cpp; `inline` because SetMana
// inlines it, which at /O2 (= /Ob1) only happens for a function so marked. The
// out-of-line COMDAT is still emitted for the other callers.
inline float Player::GetMaxMana()
{
    return *(float *)ReadProperty(&object->properties, kOrdMaxMana, kScalar);
}

// 0x1005c500, 262 bytes.
void Player::SetMana(float mana)
{
    float previous = m_mana;

    float maximum = GetMaxMana();
    float value   = (mana > maximum) ? maximum : mana;

    m_mana = value;

    if (value != previous)
    {
        Message message(kMessageManaChanged, 0);
        g_messageSystem->Send(object, &message, 10, 1);
    }

    ManaBar *bar = m_manaBar;
    if (bar != 0)
    {
        float shown = m_mana;
        if (shown < 0.0f)
            shown = 0.0f;
        bar->m_value = shown;
        bar->Refresh();
    }
}
