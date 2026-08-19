// aim.cpp - Fellowship.rfl.
//
//   0x1005ddc0  1186 bytes  __thiscall  void Player::UpdateAim()
//
// THE CLASS IS FORCED. Four property reads through the ordinal accessor at
// [vtable+8] use ordinals 0x9f, 0x9e, 0x9c and 0x9d - 159, 158, 156 and 157.
// Only Player has more than 154 properties, so no other class in the game can
// hold them. In Player's schema they are, in disassembly order:
//
//     159  YawSpeed     "Yaw (Turn) Speed (deg/s)"    float  250.0
//     158  PitchSpeed   "Pitch Speed (deg/s)"         float  150.0
//     156  MaxPitch     "Max Pitch (deg)"             float   40.0
//     157  FPMaxPitch   "Max FirstPerson Pitch (deg)" float   40.0
//
// all four in the group "PC Controls Configuration" (the assignment sheet
// files them under "Head & Neck"; classdump.py puts them in the last group).
// Every one is loaded with FLD and every one is multiplied by the same
// constant at 0x100ed128, which the image holds as 3c8efa35 = the float
// nearest pi/180. Degrees per second and degrees converted to radians, exactly
// as the schema labels them. Nothing here contradicts Player.
//
// This is the anchor health_regen.cpp already cites: it calls 0x10057060
// (Player::RegenerateHealth) and 0x1005a790 (Player::EquipWeapon) with
// MOV ECX,ESI, its own `this`, so the whole same-object call chain hangs off
// these four ordinals.
//
// WHAT IT DOES - the per-frame update of where the player is looking.
//
//   * housekeeping first: regenerate health, equip a pending weapon, run five
//     unidentified per-frame updates, clear bit 0x400 of the state word at
//     +0x50, and poke the animation channels at gameobject+0x10.
//   * poll control 0x415 through the input manager at 0x101326f4, the same
//     object and the same [vtable+0x6c] targeting.cpp polls with 0x42d, and
//     with the same state word at Player+0x2e. Non-zero and it returns.
//   * read the four properties, convert to radians, take the frame delta from
//     0x101326a8+0x04 (health_regen.cpp) and the look sensitivity from
//     0x101323a8, mapped through sens = value * 2.5 + 0.25.
//   * take the stick from the object at 0x1013135c and put it through a radial
//     response curve: normalise, square the y with its sign kept, rescale by
//     the original length. The x magnitude is clamped to 1.0 separately.
//   * yaw: unless the state word vetoes it, rotate the object about the first
//     column of the matrix at +0x10 of whatever [vtable+0xd0] returns, scaled
//     by the float at +0x34 of the same block.
//   * pitch: integrate into the float at Player+0x94 and clamp it to
//     FPMaxPitch in first person (flag 8) or MaxPitch otherwise.
//
// In first person, both yaw and pitch come from a second input object at
// 0x10131324 - 0x38 bytes below the first, so the two are neighbours in one
// table - and the stick result is not used at all.
//
// NOT ESTABLISHED, and named for role only: every callee except the two named
// above; the input objects at 0x1013135c and 0x10131324 and the two-float
// value their one method returns; the three globals at 0x101323a8, 0x101323ac
// and 0x101323b0; the object at 0x101326ac and its [vtable+0x2c]/[vtable+0x30]
// pair, which bracket the rotate; the transform at [vtable+0xd0]; the state
// object at Player+0x50 and its two methods; and every Player field below
// except m_object, m_flags and the control state word at +0x2e, which come
// from targeting.cpp.
//
// The float constants were read out of the image:
//   0x100ed128  3c8efa35  0.017453292f   pi/180
//   0x100ed140  00800000  1.1754944e-38  FLT_MIN
//   0x100ed0fc  0000803f  1.0f
//   0x100ed0f0  00000000  0.0f
//   0x100effe0  4400000000000000  2.5   (double - a qword FMUL)
//   0x100effd8  3fd0000000000000  0.25  (double - a qword FADD)

#include <math.h>

class GameObject;


// ---------------------------------------------------------------------------
// The property-value block and the read through the ADDRESS of the slot -
// decomp\src\player\stats.cpp, unchanged. That is what produces
// ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX at all four reads.

class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);   // [vtable+0x08]
};

inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar = -1;          // element index for a non-list property

// The four reads here are all of float properties that are immediately scaled,
// so the deref is wrapped: an inlined float-returning helper leaves the value
// in ST(0) as the inline's result, and the multiply that consumes it then has
// to fold its other operand - FLD [EAX] ... FMUL <scale>. Written out in line
// as `*(float *)ReadProperty(...) * scale` the two are just a pair of memory
// operands and VC6 loads the scale and folds [EAX] instead, whichever order
// they are written in and whether the scale is a constant, an extern constant
// or an extern variable.
__inline float ReadFloatProperty(PropertyBlock **slot, int ordinal)
{
    return *(float *)(*slot)->GetValue(ordinal, kScalar);
}

// Player property ordinals, from the ObjectDef schema for class 0x1000e,
// group "PC Controls Configuration".
const int kOrdMaxPitch   = 156;  // "Max Pitch (deg)"
const int kOrdFPMaxPitch = 157;  // "Max FirstPerson Pitch (deg)"
const int kOrdPitchSpeed = 158;  // "Pitch Speed (deg/s)"
const int kOrdYawSpeed   = 159;  // "Yaw (Turn) Speed (deg/s)"


// ---------------------------------------------------------------------------
// The animation/channel object at gameobject+0x10 - head_neck.cpp, which uses
// its [vtable+0xb4]. Only slots 31, 32 and 33 are called here, each with a
// single zero argument; the 31 ahead of them exist to place them.

class ChannelOwner
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
    virtual void Slot27();  virtual void Slot28();  virtual void Slot29();
    virtual void Slot30();
    virtual void Slot31(int flag);          // [vtable+0x7c]
    virtual void Slot32(int flag);          // [vtable+0x80]
    virtual void Slot33(int flag);          // [vtable+0x84]
};


// ---------------------------------------------------------------------------
// The 3x3 matrix - decomp\src\math\matrix3.cpp - and the vector.

class Vector3
{
public:
    float x, y, z;
};

class Matrix3
{
public:
    float m[3][3];
};

// What GameObject's [vtable+0xd0] hands back. Only the matrix at +0x10, the
// float at +0x34 that immediately follows it, and the rotate at 0x10003990
// are established.
class Transform
{
public:
    void Rotate(float radians, const Vector3 &axis);    // 0x10003990

    char    m_unknown_00[0x10];
    Matrix3 m_basis;             // +0x10
    float   m_scale;             // +0x34 - role unestablished; the first
                                 //         column is multiplied by it
};

class GameObject
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
    virtual void Slot27();  virtual void Slot28();  virtual void Slot29();
    virtual void Slot30();  virtual void Slot31();  virtual void Slot32();
    virtual void Slot33();  virtual void Slot34();  virtual void Slot35();
    virtual void Slot36();  virtual void Slot37();  virtual void Slot38();
    virtual void Slot39();  virtual void Slot40();  virtual void Slot41();
    virtual void Slot42();  virtual void Slot43();  virtual void Slot44();
    virtual void Slot45();  virtual void Slot46();  virtual void Slot47();
    virtual void Slot48();  virtual void Slot49();  virtual void Slot50();
    virtual void Slot51();
    virtual Transform *GetTransform();      // [vtable+0xd0]

    char           m_unknown_04[0x10 - 0x04];
    ChannelOwner  *m_channels;              // +0x10
    PropertyBlock *m_properties;            // +0x14
};


// ---------------------------------------------------------------------------
// The input manager at 0x101326f4 - targeting.cpp, unchanged.

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
// The two axis sources. Read fills a caller-owned pair of floats and hands the
// same pointer back - an OUT-PARAMETER, not a by-value return. A by-value
// Vector2 is eight bytes, so it needs an explicit copy constructor to get the
// hidden-pointer convention at all, and VC6 then emits that copy at the call
// site; the original reads the buffer straight back with no copy, and the
// three call sites that want one component use the returned pointer directly.

class Vector2
{
public:
    float x, y;
};

class AxisSource
{
public:
    Vector2 *Read(Vector2 *out);         // 0x10006060
};

extern AxisSource g_stickAxis;              // 0x1013135c
extern AxisSource g_mouseAxis;              // 0x10131324


// The object at 0x101326ac. Its [vtable+0x2c] and [vtable+0x30] bracket the
// rotate, one before and one after, both taking the game object; nothing else
// identifies them.
class TransformNotifier
{
public:
    virtual void Slot00();  virtual void Slot01();  virtual void Slot02();
    virtual void Slot03();  virtual void Slot04();  virtual void Slot05();
    virtual void Slot06();  virtual void Slot07();  virtual void Slot08();
    virtual void Slot09();  virtual void Slot10();
    virtual void BeginMove(GameObject *object);     // [vtable+0x2c]
    virtual void EndMove(GameObject *object);       // [vtable+0x30]
};

extern TransformNotifier *g_transformNotifier;      // 0x101326ac

// Runtime settings, all three read as plain globals and all three in BSS.
extern float g_lookSensitivity;             // 0x101323a8
extern int   g_invertLook;                  // 0x101323ac
extern int   g_lookEnabled;                 // 0x101323b0


// ---------------------------------------------------------------------------
// Constants, all read out of the image.

const float  kDegToRad     = 0.017453292f;      // 0x100ed128, pi/180
const float  kLengthEps    = 1.1754944e-38f;    // 0x100ed140 - FLT_MIN
const float  kOne          = 1.0f;              // 0x100ed0fc
const float  kZero         = 0.0f;              // 0x100ed0f0
const double kSensSpan     = 2.5;               // 0x100effe0 - qword FMUL
const double kSensBase     = 0.25;              // 0x100effd8 - qword FADD

const int kControlLook     = 0x415;             // polled through [vtable+0x6c]
const int kNotify16        = 0x16;              // the argument to +0x50's Notify

const unsigned int kStateBusy       = 0x400;      // cleared each frame at +0x50
const unsigned int kStatePending    = 0x400000;   // drained at +0x50
const unsigned int kStateNoYaw      = 0x8000;     // at +0x50
const unsigned int kStateLookHold   = 0x200000;   // at +0x50

const unsigned int kFlagLockedA     = 0x81;       // at +0x54
const unsigned int kFlagLockedB     = 0x40000;    // at +0x54
const unsigned int kFlagNoYaw       = 0x82700;    // at +0x54
const unsigned int kFlagFirstPerson = 0x8;        // at +0x54


// Three returns and a merge is what produces MOV EAX,1 / OR EAX,-1 /
// XOR EAX,EAX joining at a single NEG. Every single-exit spelling folds.
__inline int Sign(float value)
{
    if (value > kZero)
        return 1;
    if (value < kZero)
        return -1;
    return 0;
}


// ---------------------------------------------------------------------------
// Player. +0x2e, +0x38a and +0x38e are not 4-aligned, so the class is packed;
// pack(2) is the loosest setting that puts them there - health_regen.cpp and
// targeting.cpp reach the same conclusion from other fields.

#pragma pack(push, 2)

// The object at Player+0x50. Its first dword is a bit set this function reads
// and writes directly; the two methods are __thiscall with one int each.
class StateWord
{
public:
    void Reset(int mode);                // 0x1000a830
    void Notify(int code);               // 0x1000a850

    unsigned int m_bits;                 // +0x00
};

// The object at Player+0x32. All that is established is that a __thiscall with
// no arguments hands back something whose [vtable+0x100] takes one float.
class BlendTarget
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
    virtual void Slot27();  virtual void Slot28();  virtual void Slot29();
    virtual void Slot30();  virtual void Slot31();  virtual void Slot32();
    virtual void Slot33();  virtual void Slot34();  virtual void Slot35();
    virtual void Slot36();  virtual void Slot37();  virtual void Slot38();
    virtual void Slot39();  virtual void Slot40();  virtual void Slot41();
    virtual void Slot42();  virtual void Slot43();  virtual void Slot44();
    virtual void Slot45();  virtual void Slot46();  virtual void Slot47();
    virtual void Slot48();  virtual void Slot49();  virtual void Slot50();
    virtual void Slot51();  virtual void Slot52();  virtual void Slot53();
    virtual void Slot54();  virtual void Slot55();  virtual void Slot56();
    virtual void Slot57();  virtual void Slot58();  virtual void Slot59();
    virtual void Slot60();  virtual void Slot61();  virtual void Slot62();
    virtual void Slot63();
    virtual void SetBlend(float weight);            // [vtable+0x100]
};

class Blender
{
public:
    BlendTarget *Resolve();              // 0x1009b1e0

    char m_unknown_00[4];                // extent unestablished; four bytes is
                                         // what puts the next field at +0x36
};

// Player+0x2e and +0x32 together. Writing them as one subobject with an inline
// member is what makes VC6 compute LEA EDI,[ESI+0x2e] once and reach the
// blender as [EDI+4], the weight as [EDI] and the poll argument as EDI, which
// is what the original has. As two separate members it addresses each off ESI.
struct AimChannel
{
    float   m_weight;                    // +0x00  (Player+0x2e)
    Blender m_blender;                   // +0x04  (Player+0x32)

    void Apply();
};

__inline void AimChannel::Apply()
{
    m_blender.Resolve()->SetBlend(m_weight);
    m_weight = 1.0f;
}

// The subobject at Player+0x38e. Only slot 2 of its vtable is called, with no
// arguments; the two ahead of it exist to place it.
class PitchDriver
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Update();               // [vtable+0x08]
};

// The pointer at Player+0x38a.
class LookController
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
    virtual void Refresh();              // [vtable+0x6c]
};

class Player
{
public:
    void UpdateAim();

    // Not defined here - __thiscall members reached by CALL rel32.
    void        RegenerateHealth();                  // 0x10057060 - matched in
                                                     //   health_regen.cpp
    GameObject *EquipWeapon(int objectLink);         // 0x1005a790 - matched in
                                                     //   weapon_equip.cpp
    void UpdateStance();                             // 0x1005b390
    void UpdateMovement();                           // 0x10053a60
    void UpdateCamera();                             // 0x1005d280
    void UpdateCombat();                             // 0x1005eb10
    void UpdateSpells();                             // 0x1005ef30

    void        *m_unknown_00;               // +0x00
    GameObject  *m_object;                   // +0x04
    char         m_unknown_08[0x2e - 0x08];
    AimChannel   m_aim;                      // +0x2e - the weight at +0x2e is
                                             //         also the control state
                                             //         word the poll receives
    char         m_unknown_36[0x50 - 0x36];
    StateWord    m_state;                    // +0x50
    unsigned int m_flags;                    // +0x54 - targeting.cpp
    char         m_unknown_58[0x94 - 0x58];
    float        m_pitch;                    // +0x94 - radians
    char         m_unknown_98[0x148 - 0x98];
    int          m_pendingWeapon;            // +0x148
    char         m_unknown_14c[0x38a - 0x14c];
    LookController *m_look;                  // +0x38a
    PitchDriver  m_pitchDriver;              // +0x38e
    char         m_unknown_392[0x3b0 - 0x392];
    int          m_lookEnabled;              // +0x3b0
};

#pragma pack(pop)


// ---------------------------------------------------------------------------
// 0x1005ddc0, 1186 bytes.

void Player::UpdateAim()
{
    m_state.Reset(0);
    RegenerateHealth();

    if (m_pendingWeapon > 0)
    {
        EquipWeapon(m_pendingWeapon);
        m_pendingWeapon = 0;
    }

    UpdateStance();
    m_pitchDriver.Update();

    m_aim.Apply();

    UpdateMovement();
    UpdateCamera();

    m_state.m_bits &= ~kStateBusy;

    UpdateCombat();

    if (m_look && m_lookEnabled)
        m_look->Refresh();

    m_object->m_channels->Slot32(0);
    m_object->m_channels->Slot33(0);

    if (m_state.m_bits & kStatePending)
    {
        m_state.Notify(kNotify16);
        m_object->m_channels->Slot31(0);

        if (m_state.m_bits & kStatePending)
        {
            m_state.Notify(kNotify16);
            m_object->m_channels->Slot31(0);
        }
    }

    UpdateSpells();

    if (g_inputManager->PollControl(&m_aim, kControlLook, 0))
        return;

    float delta = g_frameTimer->m_deltaSeconds;

    float yawSpeed =
        ReadFloatProperty(&m_object->m_properties, kOrdYawSpeed) * kDegToRad;
    float pitchSpeed =
        ReadFloatProperty(&m_object->m_properties, kOrdPitchSpeed) * kDegToRad;
    float maxPitch =
        ReadFloatProperty(&m_object->m_properties, kOrdMaxPitch) * kDegToRad;
    float fpMaxPitch =
        ReadFloatProperty(&m_object->m_properties, kOrdFPMaxPitch) * kDegToRad;

    float sens = (float)(g_lookSensitivity * kSensSpan + kSensBase);

    int locked;
    int noYaw;

    if ((m_flags & kFlagLockedA) || (m_flags & kFlagLockedB) ||
        (m_state.m_bits & kStateNoYaw))
    {
        locked = 1;
        noYaw = 1;
    }
    else if (m_flags & kFlagNoYaw)
    {
        locked = 0;
        noYaw = 1;
    }
    else
    {
        locked = 0;
        noYaw = 0;
    }

    Vector2 stick;
    g_stickAxis.Read(&stick);

    float mag = (float)fabs(stick.x);

    // Spelled as the negation of `<` rather than as `>=`: `>=` gives
    // AND EAX,0x100 / JNZ, which ignores the unordered case, and the original
    // has TEST AH,5 / JNP. health_regen.cpp records the same trick for `<=`.
    float lengthSq = stick.y * stick.y + stick.x * stick.x;
    if (!(lengthSq < kLengthEps))
    {
        float length = (float)sqrt(lengthSq);
        float inv = kOne / length;
        float nx = stick.x * inv;
        float ny = inv * stick.y;
        stick.y = ny * (float)fabs(ny);
        stick.x = length * nx;
        stick.y = length * stick.y;
    }

    if (mag > kOne)
        mag = kOne;

    if (!noYaw)
    {
        Vector2 mouse;
        float yaw;

        if (m_flags & kFlagFirstPerson)
            yaw = -(sens * g_mouseAxis.Read(&mouse)->x * yawSpeed * delta);
        else
            yaw = -Sign(stick.x) * mag * sens * yawSpeed * delta;

        g_transformNotifier->BeginMove(m_object);

        Transform *t = m_object->GetTransform();
        Vector3 axis;
        axis.x = t->m_basis.m[0][0] * t->m_scale;
        axis.y = t->m_basis.m[1][0] * t->m_scale;
        axis.z = t->m_basis.m[2][0] * t->m_scale;
        t->Rotate(yaw, axis);

        g_transformNotifier->EndMove(m_object);
    }

    if (locked)
        return;

    // One store of m_pitch, at the end, shared by both branches: that is what
    // puts a single FSTP [ESI+0x94] at 0x1005e255 with three of the six paths
    // reaching it and the other three carrying a tail-duplicated copy. Written
    // as `m_pitch = limit; return;` in each clamp the copy is a memory-to-
    // memory integer MOV instead of the original's FLD/FSTP.
    float pitch;

    if (m_flags & kFlagFirstPerson)
    {
        Vector2 mouse;

        if (g_invertLook)
            pitch = m_pitch - sens * g_mouseAxis.Read(&mouse)->y * pitchSpeed * delta;
        else
            pitch = m_pitch + sens * g_mouseAxis.Read(&mouse)->y * pitchSpeed * delta;

        if (pitch > fpMaxPitch)
            pitch = fpMaxPitch;
        else if (pitch < -fpMaxPitch)
            pitch = -fpMaxPitch;
    }
    else
    {
        if (!g_lookEnabled && !(m_state.m_bits & kStateLookHold))
        {
            pitch = kZero;
        }
        else
        {
            pitch = stick.y * sens * pitchSpeed * delta;

            if (g_invertLook)
                pitch = m_pitch - pitch;
            else
                pitch = m_pitch + pitch;
        }

        if (pitch > maxPitch)
            pitch = maxPitch;
        else if (pitch < -maxPitch)
            pitch = -maxPitch;
    }

    m_pitch = pitch;
}


// ---------------------------------------------------------------------------
// WHERE THIS STOPS - 1,168 bytes against the original's 1,186, after eight
// attempts. 802 of 1,186 bytes agree under an alignment-tolerant diff; the
// positional count try.py prints is much lower only because the whole body is
// shifted by the 18-byte deficit, which has exactly two causes.
//
// (A) THE CONSTANT ZERO ENDS UP IN EBX, and in the original it does not. VC6
//     emits XOR EBX,EBX in the prologue and then spends it on everything that
//     wants a zero: PUSH EBX for all six zero-valued call arguments (the
//     original pushes `6a 00` each time), CMP r,EBX where the original has
//     TEST r,r, MOV [ESI+0x148],EBX where the original stores the immediate,
//     and MOV EBX,1 / an implied 0 for `locked` where the original keeps that
//     flag in a stack slot with MOV dword ptr [ESP+0x24],0 and ...,1. It costs
//     a PUSH EBX in the prologue and a POP EBX in each of the four epilogues
//     and comes to -16 bytes net. Everything from offset 0x08 onward shifts.
//
//     Ruled out as the cause:
//       * the count of zero-valued call arguments. Both versions have exactly
//         six, plus the two zero stores, and the original still does not pool.
//       * spelling the zero tests as `x != 0` / `x == 0` rather than `x` /
//         `!x`. Changing every one of them made no difference; the CMP r,EBX
//         forms are a consequence of EBX already holding zero, not the cause.
//       * the pooled zero being `locked`. Declaring `locked` volatile forces
//         it to memory as the original has it and EBX is *still* zeroed at
//         entry and still spent on the six pushes.
//       * the argument type at the three ChannelOwner slots. Declaring them
//         float and passing 0.0f still pushes EBX.
//     What is left is register-allocation policy: EBX is free in both, and
//     VC6 took it here and not there. If someone finds the lever, this alone
//     should bring the function to within a handful of bytes.
//
// (B) THE NORMALISE BLOCK SCHEDULES ny BEFORE nx, -4 bytes. The original
//     computes nx = stick.x * inv first, leaves it on the x87 stack across the
//     whole y shaping, and pays an FXCH to get inv back to the top for
//     inv * stick.y; VC6 sinks nx to its first use, saving the FXCH and one
//     FLD. Writing the second product as `inv * stick.y` rather than
//     `stick.y * inv` is right - it is what the FXCH says - but it does not on
//     its own force nx to be evaluated first. The remaining 4 bytes are here.
//
// (C) The frame is 0x34 where the original's is 0x30: one 4-byte slot more.
//     Scoping the mouse Vector2 to the two blocks that use it recovered the
//     other four and let VC6 overlap it with the rotation axis, as the
//     original does; the last slot has not been tracked down.
//
// Everything else agrees instruction for instruction: the whole housekeeping
// head, the four property reads with their interleaved argument setup, the
// sensitivity curve, the three-armed flag chain, the FSQRT/FDIV normalise, the
// inlined Sign, the yaw expression in both its forms, the bracketed rotate,
// and all four pitch clamp-and-store paths with their tail-duplicated
// epilogues.
//
// Conventions this function confirmed, all of them new or newly sharpened:
//
//   * an inlined float-RETURNING helper is what puts a property value in ST(0)
//     so the scale folds into the FMUL. Written in line as
//     `*(float *)ReadProperty(...) * k` VC6 loads k and folds [EAX] instead,
//     whichever order the operands are written and whether k is a literal, a
//     const float, an extern const float or an extern float. Four sites, and
//     the wrapper fixed all four at once. See ReadFloatProperty above.
//   * `if (!(a < b))` gives FCOM / TEST AH,5 / JNP where `a >= b` gives
//     AND EAX,0x100 / JNZ - the same trick health_regen.cpp records for `<=`,
//     and a fourth distinct float-compare form for the brief's list.
//   * an 8-byte pair of floats coming back from a call is an OUT-PARAMETER
//     that returns its own argument, not a by-value return. By value it needs
//     an explicit copy constructor to get the hidden-pointer convention at
//     all, and VC6 then emits the copy at the call site; the original reads
//     the buffer straight back.
//   * two adjacent fields reached both individually and by address want to be
//     one subobject with an __inline member. That is what produces
//     LEA EDI,[ESI+0x2e] once and [EDI], [EDI+4] and EDI thereafter, instead
//     of three separate displacements off ESI.
//   * clamps that assign the local and fall through to ONE trailing store give
//     the original's shared FSTP with tail-duplicated epilogues; written as
//     `m_field = limit; return;` the copy becomes a memory-to-memory integer
//     MOV and the store is duplicated instead of the epilogue.
//
// Also worth folding at integration: 0x10057060 (Player::RegenerateHealth,
// health_regen.cpp) and 0x1005a790 (Player::EquipWeapon, weapon_equip.cpp) are
// both called from here with MOV ECX,ESI. Neither needed its body visible -
// no float crosses either call - so they are declared, not copied.
