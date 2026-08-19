// special_attack.cpp - Fellowship.rfl.
//
//   0x100621b0  692 bytes  __thiscall  void Player::UpdateSpecialAttackTap()
//
// NOT MATCHED. 674 bytes against the original's 692, and 162 of the original's
// 182 instructions come out with the same opcode and the same length in the
// same place. What is left is listed at the bottom of this comment.
//
// THE CLASS IS ESTABLISHED by the two ordinals it reads through the standard
// property accessor, 0x6a = 106 and 0x6b = 107. Ordinal 107 is loaded with
// FLD, and only Player and Control Input Names have an ordinal that high;
// every Control Input Names property is a string, so the schema is Player's.
// Both ordinals land in Player's "Combat Related" group and both are floats:
//
//   106  SpecialAttackTimer                                     float,  0.25
//   107  SpecialAttackTapMaxAngle
//        "Max Angle Allowed For Tap Directions (degrees)"        float, 90.0
//
// WHAT IT DOES. A two-tap directional gesture detector for the special attack,
// driven off the movement-input object at 0x10131394 and a countdown timer.
// Both pieces of gesture state are function-local statics, because the gesture
// belongs to the one player rather than to the Player object:
//
//   0x101328e0  the countdown, seeded with SpecialAttackTimer
//   0x101328d8  the direction the first tap was made in (two floats)
//   0x101328e4  VC6's initialisation guard for the two of them
//
// and the progress counter is a member, Player+0xec: 0 nothing seen, 1 first
// tap registered, 2 gesture complete.
//
// Each frame it runs the timer down by the frame delta. Unless the gesture is
// already complete it takes the stick direction and asks whether the stick has
// just settled - magnitude over 0.4 and the component of the stick's secondary
// vector along that direction under 0.4 - and if so:
//
//   * from state 1, if this direction is within SpecialAttackTapMaxAngle of
//     the direction the first tap was made in, the gesture completes and the
//     timer is re-seeded
//   * from state 0, the same angle test against the player's own facing: the
//     aim object's transform column 0 is taken into input space and dotted
//     with the negated, scale.z-weighted column 2 of the player's transform.
//     On a hit the state advances to 1 and the timer is re-seeded. Either way
//     the direction is remembered as the first tap
//
// When the timer runs out the whole thing resets.
//
// THE ANGLE TEST IS AN APPROXIMATION, not a real cosine: the authored angle is
// converted to radians and halved, and the dot product is required to lie in
// [1 - that, 1]. At the default 90 degrees that is a dot of 0.215 upward, so
// roughly a 77-degree cone rather than the 45 the name suggests. The two FMULs
// are separate constants in .rdata - 0.017453292 then 0.5 - so the source has
// them as two multiplies rather than one folded factor.
//
// Reused unchanged from already-matched sources:
//
//   ReadProperty   decomp\src\player\stats.cpp - the read through the ADDRESS
//       of the block slot, ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX
//   FrameTimer     decomp\src\player\health_regen.cpp - the global at
//       0x101326a8 whose +0x04 is the frame delta in seconds
//   ObjectRegistry decomp\src\player\spell_hotkeys.cpp and targeting.cpp - the
//       global at 0x101326cc and the object at its +0xbc
//   Matrix         decomp\src\math\matrix.h - Vector3 at 0x00, float m[3][3]
//       at 0x0c where the index selects a COLUMN, Vector3 scale at 0x30. The
//       offsets this function uses (0x0c/0x18/0x24 and 0x14/0x20/0x2c/0x38)
//       fall out of that layout exactly, and 0x10017ba0 is two instructions,
//       LEA EAX,[ECX+0xec] / RET, which fixes the matrix at GameObject+0xec
//
// NOT established, and named for role only: the movement-input object at
// 0x10131394 and its five methods. Their bodies do say what they compute -
// 0x10006080 and 0x10006120 normalise the float pairs at +0x10/+0x14 and
// +0x18/+0x1c, 0x100060f0 is the length of the first clamped to 1.0,
// 0x10006190 is the length of the second unclamped, and 0x100061b0 rotates a
// Vector3 into the frame at +0x2c - but what the two pairs are is a guess.
//
// ONE DIVERGENCE FROM A SHARED HEADER, reported rather than made. The by-value
// Vector3 argument at 0x1006237c is copy-constructed with a CALL to 0x10002230,
// which decomp\src\math\vector3.cpp matches as Vector3::operator=. An
// out-of-line copy constructor with that body compiles to the same bytes and
// /Gy lets the linker fold the two, so the original almost certainly declared
// the copy constructor in the header and DEFINED it in vector3.cpp. vector3.h
// defines it inline, which makes VC6 expand the copy in place and emit no call
// at all - so this file declares its own Vector3 rather than including it.


// ---------------------------------------------------------------------------
// The property-value block - decomp\src\player\stats.cpp, unchanged.

class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);   // [vtable+0x08]
};

// Through the ADDRESS of the slot: ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX.
inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar = -1;          // element index for a non-list property

// Player property ordinals, from the ObjectDef schema for class 0x1000e.
// Group "Combat Related".
const int kOrdSpecialAttackTimer       = 106;  // seconds the gesture stays live
const int kOrdSpecialAttackTapMaxAngle = 107;  // degrees


// ---------------------------------------------------------------------------
// Math. Declared here rather than included - see the header note on the copy
// constructor.

class Vector3
{
public:
    float x, y, z;

    Vector3() {}
    Vector3(const Vector3 &o);           // 0x10002230, out of line
};

// decomp\src\math\matrix.h. The index into m selects a column.
class Matrix
{
public:
    Vector3 origin;      // 0x00
    float   m[3][3];     // 0x0c
    Vector3 scale;       // 0x30
};

// The two-component vector the movement input fills in. Plain: no constructor
// and no destructor, because nothing here constructs, copies or destroys one.
class Vector2
{
public:
    float x, y;
};

__inline float Dot2(const Vector2 &a, const Vector2 &b)
{
    return a.y * b.y + a.x * b.x;
}

// The static's type differs from Vector2 in exactly one way - it has a
// destructor - and that is what puts the atexit registration in the
// initialisation guard. The thunk it registers, 0x10062470, is a bare RET,
// so the destructor body is empty.
class TapDirection : public Vector2
{
public:
    TapDirection()  { x = 0.0f; y = 0.0f; }
    ~TapDirection() {}
};


// ---------------------------------------------------------------------------
// The movement input object at 0x10131394. A global instance, not a pointer:
// every call site is MOV ECX,<offset> / CALL rel32.

class MoveInput
{
public:
    // These three write through a reference and answer it. They are NOT
    // by-value returns, and the difference is visible: VC6 never elides the
    // copy in `T x = f()` for a class returned in memory - it builds a
    // temporary and copies, which the original does not do anywhere.
    Vector2 &Direction(Vector2 &out);              // 0x10006080
    Vector2 &SettleDirection(Vector2 &out);        // 0x10006120
    Vector3 &ToInputSpace(Vector3 &out, Vector3 v);// 0x100061b0

    float    Magnitude();            // 0x100060f0  length(+0x10,+0x14), max 1
    float    SettleMagnitude();      // 0x10006190  length(+0x18,+0x1c)
};

extern MoveInput g_moveInput;               // 0x10131394


// ---------------------------------------------------------------------------
// The globals the already-matched player sources share.

struct FrameTimer
{
    void  *m_unknown_00;
    float  m_deltaSeconds;       // +0x04
};

extern FrameTimer *g_frameTimer;            // 0x101326a8


class GameObject
{
public:
    Matrix *GetTransform();                  // 0x10017ba0 - LEA EAX,[ECX+0xec]

    char           m_unknown_00[0x14];
    PropertyBlock *m_properties;             // +0x14
    char           m_unknown_18[0xec - 0x18];
    Matrix         m_transform;              // +0xec
};

class ObjectRegistry
{
public:
    char        m_unknown_00[0xbc];
    GameObject *m_aimReceiver;               // +0xbc - role unestablished
};

extern ObjectRegistry *g_objectRegistry;    // 0x101326cc


// ---------------------------------------------------------------------------

// Player+0xec.
const int kTapNone  = 0;         // nothing seen
const int kTapFirst = 1;         // one tap registered, direction remembered
const int kTapDone  = 2;         // gesture complete

// The stick has to be out past 0.4, and its secondary vector no further than
// 0.4 along the stick direction, for a tap to count. Written as literals: a
// named `const float` becomes a real .rdata variable under VC6 and changes
// which side of a commutative FMUL gets the FLD.


class Player
{
public:
    void UpdateSpecialAttackTap();

    void       *m_unknown_00;                // +0x00
    GameObject *m_object;                    // +0x04
    char        m_unknown_08[0xec - 0x08];
    int         m_tapState;                  // +0xec
};


// WHAT STILL DIFFERS, 18 bytes and six sites:
//
//   1. The original saves EDI and keeps the outgoing by-value argument slot in
//      it: SUB ESP,0xc / MOV EDI,ESP are hoisted ABOVE the GetTransform call,
//      so the pointer has to survive in a callee-saved register. Ours issues
//      the SUB after the call and uses ECX, which drops PUSH EDI / POP EDI (2
//      bytes), shortens the frame by the one x87 spill slot EDI's absence also
//      removes, and shifts every stack displacement in the function by four.
//      Nothing tried moved it: the helper forms that would put the
//      GetTransform call inside the argument list are exactly the ones VC6
//      refuses to inline.
//
//   2. Consequence of (1): with EDI in the frame the original schedules
//      `PUSH -1 / MOV EAX,[ESI+4] / PUSH 0x6a`; ours issues both pushes first.
//      Same bytes, different order.
//
//   3. The negated column: the original spills two of the three products
//      (FSTP/FXCH/FCHS/FSTP/FCHS/FLD, 20 bytes) where ours keeps all three on
//      the x87 stack and reaches ST(4). Same algorithm, one register of
//      pressure apart - 14 of the 18 bytes.
//
//   4. The first term of the state-1 dot has its FLD and FMUL operands the
//      other way round. VC6 picks that per call site and flips BOTH terms
//      together when the arguments are swapped, while the original has one
//      term each way, so no argument order reaches it.
//
//   5. `s_timer = tapTime` in the state-0 arm: the original has the value in
//      EAX and uses the 5-byte A3 moffs form, ours has it in ECX and pays 6.
//      Register allocation.
//
//   6. `s_tapDir.y = dir.y`: the mirror image, ours in EAX and the original in
//      EDX. Register allocation the other way, so these two cancel.


// 0x100621b0, 692 bytes.
void Player::UpdateSpecialAttackTap()
{
    float tapTime = *(float *)ReadProperty(&m_object->m_properties,
                                           kOrdSpecialAttackTimer, kScalar);
    float halfAngle = *(float *)ReadProperty(&m_object->m_properties,
                                             kOrdSpecialAttackTapMaxAngle,
                                             kScalar)
                      * 0.0174532925f;
    halfAngle *= 0.5f;

    static float        s_timer = tapTime;
    static TapDirection s_tapDir;

    if (s_timer >= 0.0f)
        s_timer = s_timer - g_frameTimer->m_deltaSeconds;

    if (m_tapState != kTapDone)
    {
        Vector2 dir, settleDir;
        g_moveInput.Direction(dir);
        float settle = Dot2(dir, g_moveInput.SettleDirection(settleDir));
        settle = settle * g_moveInput.SettleMagnitude();

        if (g_moveInput.Magnitude() > 0.4f && settle < 0.4f)
        {
            if (m_tapState == kTapFirst)
            {
                float d = Dot2(dir, s_tapDir);
                if (d >= 1.0f - halfAngle && d <= 1.0f)
                {
                    m_tapState = kTapDone;
                    s_timer = tapTime;
                }
            }
            else
            {
                Matrix *aim = g_objectRegistry->m_aimReceiver->GetTransform();

                Vector3 forward;
                forward.x = aim->m[0][0];
                forward.y = aim->m[1][0];
                forward.z = aim->m[2][0];

                Vector3 local;
                g_moveInput.ToInputSpace(local, forward);

                Matrix *me = &m_object->m_transform;
                Vector3 n;
                n.x = -(me->m[0][2] * me->scale.z);
                n.y = -(me->m[1][2] * me->scale.z);
                n.z = -(me->m[2][2] * me->scale.z);

                float d = n.x * local.x + n.y * local.y + n.z * local.z;

                if (d >= 1.0f - halfAngle && d <= 1.0f)
                {
                    m_tapState = kTapFirst;
                    s_timer = tapTime;
                }

                s_tapDir.x = dir.x;
                s_tapDir.y = dir.y;
            }
        }
    }

    if (s_timer <= 0.0f)
    {
        m_tapState = kTapNone;
        s_tapDir.x = 0.0f;
        s_tapDir.y = 0.0f;
    }
}
