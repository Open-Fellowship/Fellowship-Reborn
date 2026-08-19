// Fellowship.rfl - Player, "Health Regeneration": the per-tick heal and its
// guard predicate.
//
//   0x10057060  103 bytes  Player::RegenerateHealth
//   0x100570d0   67 bytes  Player::ShouldRegenerate
//
// THE CLASS IS ESTABLISHED, by a same-object call chain rather than by address
// range. 0x1005ddc0 reads ordinals 156-159, and only Player has more than 154
// properties, so that function can only be Player. It saves its own `this`
// with MOV ESI,ECX and calls 0x10057060 with MOV ECX,ESI; 0x10057060 does the
// same to 0x100570d0 and to 0x10057030. One object all the way down, so one
// class.
//
// The ordinals agree. 0x10057060 reads ordinal 10, which on Player is
// HealSpeed - "Heal Speed (health points / sec)", float, default 0.75 - and
// 0x10057030 reads ordinal 11, CriticalHealthPerc, "Critical Health Percentage
// (0 - 100)", float, default 20.0. Those two are the whole of Player's "Health
// Regeneration" group, and these are the only functions in the rfl that read
// them. A rate in health points per second multiplied by a per-frame delta and
// added to a current value, stopping at a percentage of a maximum, is exactly
// what that group describes.
//
// WHAT THE TWO FUNCTIONS DO
//
//   ShouldRegenerate   current < critical, and at least one of three runtime
//                      conditions at +0x0e, +0x1a and +0x12 holds.
//   RegenerateHealth   while that is true, add HealSpeed * dt; then, if it has
//                      just stopped being true, snap the value to exactly the
//                      critical health. So this heals a wounded Player back up
//                      to the critical threshold and no further.
//
// ALL THREE FUNCTIONS WERE IN ONE SOURCE FILE, and that is not a guess about
// tidiness - it is forced by the codegen. ShouldRegenerate compares two
// float-returning calls, and the original holds the first result on the x87
// stack across the second call:
//
//     CALL [EAX+0x38] / MOV ECX,ESI / CALL 0x10057030 / FCOMPP
//
// With 0x10057030 only *declared*, VC6 will not do that: it spills the first
// result to a stack slot and compares against memory, which costs a PUSH ECX,
// an FSTP, two bytes on the FCOMP and a POP ECX in each of the two epilogues -
// 76 bytes instead of 67, with every byte after the first call shifted. With
// 0x10057030 *defined earlier in the same translation unit* VC6 knows what the
// callee does to the FP stack, keeps the value in ST(1) and emits FCOMPP. The
// two forms differ in nothing else; every other byte of the function is
// identical either way. That is why GetCriticalHealth is defined below rather
// than merely declared.
//
// It is the same function as 0x10057030, matched separately in
// objectdef\scaled_percentage.cpp as PropertyOwner::GetScaledPercentage. The
// copy here is that file's source with the names this file uses; the class
// layout, the ordinal and the two `*=` statements all come from there. Whoever
// integrates these should fold the three functions into one file - which is
// what the original had - rather than leave the duplicate standing.
//
// WHAT IS *NOT* ESTABLISHED, and is named here for position and role only:
//
//   [vtable+0x38] / [vtable+0x3c]   slots 14 and 15. The first returns a float
//       in ST0 and takes no argument; the second takes one float and returns
//       nothing. Nothing in either function identifies them beyond that. They
//       are called GetCurrentHealth/SetCurrentHealth because that is the role
//       they play at these three call sites - compared against the critical
//       health, incremented at the heal speed, clamped to the critical health.
//       The vtable belongs to some base of Player, not to Player itself, and
//       the 14 slots ahead of them are declared purely to place them.
//
//   the global at 0x101326a8   a pointer, uninitialised in the image (its
//       address is past .data's raw size, so it is BSS and written at runtime).
//       Only its +0x04 is touched, as a float multiplied into a per-second
//       rate, so that field is a frame delta in seconds and the global is
//       whatever object owns one. Which object that is, is not established.
//
//   +0x0e, +0x12, +0x1a   runtime state, not authored properties. Two are
//       tested against zero as integers and one against 0.0f. Their meaning is
//       unknown; the names say where they are and nothing else.
//
// The float compared against at 0x100ed0f0 was read out of the image: it is
// 00000000, the constant 0.0f, loaded as a dword.
//
// Only Player, HealSpeed and CriticalHealthPerc are real names, from the
// ObjectDef table. Everything else below is invented.

// The property-value block. Only slot 2 of its vtable is exercised here; the
// two slots before it are declared purely to place that one at [vtable+8], and
// their signatures are unknown. The block's implementation is not in the rfl.
class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();

    // ordinal is the property's flat index in its class's schema; element is
    // the index within a list-valued property, -1 for a scalar. Returns a
    // pointer to the stored value.
    virtual void *GetValue(int ordinal, int element);
};

// The game object the Player belongs to. Only the property-block slot at
// +0x14 is established here.
class GameObject
{
public:
    char unknown_00[0x14];
    PropertyBlock *properties;   // +0x14
};

// The read goes through the *address* of the slot, not the slot: that is what
// produces ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX. Reading
// object->properties directly folds to MOV ECX,[EAX+0x14] and loses bytes.
inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar = -1;                // element index for a non-list property

// Player property ordinals, from the ObjectDef schema for class 0x1000e.
const int kOrdHealSpeed         = 10;  // "Heal Speed (health points / sec)"
const int kOrdCriticalHealthPct = 11;  // "Critical Health Percentage (0 - 100)"

// Percentage to fraction. Declared double, initialised from a float literal:
// the constant in .rdata at 0x100efdf0 is 3f847ae140000000, the double nearest
// the float 0.01f rather than the double 0.01, and it is loaded with a qword
// FMUL. A bare 0.01f gives a dword FMUL instead.
const double kPercentToFraction = 0.01f;

// The global at 0x101326a8. Identity unestablished - see the header comment.
// Only +0x04 is read, and only as a per-frame scale on a per-second rate.
struct FrameTimer
{
    void  *unknown_00;           // +0x00
    float  deltaSeconds;         // +0x04
};

extern FrameTimer *gFrameTimer;

// +0x0e, +0x12 and +0x1a are not 4-aligned, so the class is packed. pack(2) is
// the loosest setting that puts them there.
#pragma pack(push, 2)

class Player
{
public:
    // Slots 0..13 exist only to place the two that are called at [vtable+0x38]
    // and [vtable+0x3c]. Their signatures are unknown.
    virtual void Slot00();
    virtual void Slot01();
    virtual void Slot02();
    virtual void Slot03();
    virtual void Slot04();
    virtual void Slot05();
    virtual void Slot06();
    virtual void Slot07();
    virtual void Slot08();
    virtual void Slot09();
    virtual void Slot10();
    virtual void Slot11();
    virtual void Slot12();
    virtual void Slot13();
    virtual float GetCurrentHealth();            // [vtable+0x38]
    virtual void  SetCurrentHealth(float value); // [vtable+0x3c]

    GameObject *object;          // +0x04
    char        unknown_08[6];   // +0x08 .. +0x0d
    int         field0e;         // +0x0e  runtime state, meaning unknown
    float       field12;         // +0x12  runtime state, meaning unknown
    float       percentBase;     // +0x16  the quantity the critical-health
                                 //        percentage is taken of; which
                                 //        quantity is not established
    int         field1a;         // +0x1a  runtime state, meaning unknown

    float GetCriticalHealth();
    int   ShouldRegenerate();
    void  RegenerateHealth();
};

#pragma pack(pop)

// 0x10057030, 41 bytes. Matched separately as
// objectdef\scaled_percentage.cpp; see the header comment for why it has to be
// defined here and not merely declared. Returns percentBase scaled by
// CriticalHealthPerc read as a 0-100 percentage. The two `*=` statements are
// load-bearing: written as one expression VC6 reassociates and emits the two
// FMULs in the other order.
float Player::GetCriticalHealth()
{
    float base = percentBase;
    double scaled =
        *(float *)ReadProperty(&object->properties, kOrdCriticalHealthPct, kScalar);
    scaled *= kPercentToFraction;
    scaled *= base;
    return (float)scaled;
}

// 0x100570d0, 67 bytes.
//
// The last disjunct is spelled `!(field12 <= 0.0f)` and not `field12 > 0.0f`.
// The two differ only for a NaN, and VC6 emits the difference: `>` gives
// AND EAX,0x4100 / JNE, while the negated `<=` gives TEST AH,0x41 / JNP, which
// is what the original has. The first comparison in the same function *is* the
// plain `<` form and does give AND EAX,0x4100 / JNE, so the two spellings sit
// side by side on purpose.
int Player::ShouldRegenerate()
{
    if (GetCurrentHealth() < GetCriticalHealth() &&
        (field0e != 0 || field1a != 0 || !(field12 <= 0.0f)))
    {
        return 1;
    }

    return 0;
}

// 0x10057060, 103 bytes.
//
// HealSpeed comes back as a pointer and is copied to a stack local with an
// integer MOV rather than an FLD - VC6 copies by bits a float it is only going
// to spill. It has to be spilled because the GetCurrentHealth call below
// clobbers the registers. It is a float all the same, and the FLD that reads
// the slot back again proves it.
void Player::RegenerateHealth()
{
    if (ShouldRegenerate())
    {
        float healSpeed =
            *(float *)ReadProperty(&object->properties, kOrdHealSpeed, kScalar);

        SetCurrentHealth(GetCurrentHealth() +
                         healSpeed * gFrameTimer->deltaSeconds);

        if (!ShouldRegenerate())
        {
            SetCurrentHealth(GetCriticalHealth());
        }
    }
}
