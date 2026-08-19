// Fellowship.rfl - 0x10055650, 816 bytes.  Player, conversation eligibility.
//
// WHAT IT DOES
//
// The Player holds a two-byte object link at +0xcc naming the party it wants
// to talk to.  This resolves that link and answers whether a conversation may
// run right now: 0 if every test passes, -6 if any of them fails.  Every
// rejection lands on one shared epilogue, which is why the whole body is a
// single nested if with `return kNoTarget` only at the very end.
//
// The tests, in order:
//
//   * the link is set, and resolves to a live object (a broken link is
//     reported, cleared, and rejected)
//   * the Player is in none of three states, 6, 4 and 0x15
//   * a global at 0x101326a8 does not veto it through [vtable+0x34]
//   * the height difference to the other object is within VerticalThreshold
//   * the horizontal distance to the other object's conversation point is
//     within ConversationRadius
//   * two facing tests against cos(ConversationAngle), one on each party's
//     local Z axis
//
// The three properties are Player's, and that is arithmetic rather than
// judgement: the ordinals are 119, 120 and 121, only Player and
// `Control Input Names` have that many properties, all 154 of the latter's are
// strings, and all three of these are dereferenced with FLD.  See
// documentation\ORDINAL-MAP.md.  They are the first three members of Player's
// "Conversation distances" group.
//
// The two distances are authored in level units and positions are in world
// units, so each is scaled by 2048 - the same wu-per-lu constant
// player\npc_activation.cpp found squared as 4194304.
//
// WHAT IS NOT ESTABLISHED
//
//   * the name.  It is a predicate over the conversation target and it returns
//     a small negative code, so it is named for what it decides.  -6 is one
//     value out of a result enumeration this function alone cannot recover.
//   * the direction convention.  The two facing tests use the object's local Z
//     axis scaled by scale.z, and the vector they dot it with points from the
//     other party to the Player.  Taken at face value they require the Player
//     to be facing away from the other party and the other party to be facing
//     away from the Player, which is the opposite of what "activation angle"
//     suggests.  Either the stored Z column is the backward axis or the result
//     drives a turn rather than a go-ahead.  Nothing here settles it, so the
//     comparisons below are written exactly as the disassembly reads and are
//     not "corrected".
//   * StateSet, 0x1000f1e0.  A subobject at Player+0x54 with one non-virtual
//     __thiscall method taking an int and returning an int tested against
//     zero.  States 6, 4 and 0x15 are numbers, not names.
//   * GameMode, 0x101326a8.  A global object whose [vtable+0x34] takes no
//     argument and returns an int tested against zero.
//   * Vector3::Set, 0x100b82f0, and Vector3::Length, 0x100029e0.  Both are
//     __thiscall on a Vector3; Set takes three floats and cleans them, Length
//     returns in ST(0).  The names are the roles the call sites give them.
//     Neither is in math\vector3.cpp - that class's members all sit between
//     0x10002200 and 0x10004600 and neither of these does - so whether this is
//     the same class or a second one is open.
//   * GameObject [vtable+0x48], returning a Vector3 * that is used as a
//     position.  Called only on the other party, so it is the point one talks
//     *to* rather than the object's origin.
//   * ObjectLinkList at 0x101326cc and the console at 0x1013269c.  The list
//     holds the entry array at +0x14, its count at +0x1c and a notification
//     subobject at +0x10; entries are six bytes, so the struct is packed.
//     Indexing is entries[link - 1] and the bound is checked against
//     count + 1, both of which say link numbering is one-based.
//
// The Matrix layout is math\matrix.h's, and it lands exactly: a Matrix at
// GameObject+0xec puts origin at +0xec (the position this function subtracts),
// m[0][2]/m[1][2]/m[2][2] at +0x100/+0x10c/+0x118 and scale.z at +0x124 -
// every offset the disassembly uses, with nothing left over.  It is declared
// locally rather than included, following player\npc_activation.cpp, because
// this file needs an out-of-line Vector3::Set that math\vector3.h does not
// declare.

#include <math.h>

class Vector3
{
public:
    float x, y, z;

    void  Set(float ix, float iy, float iz);   // 0x100b82f0
    float Length() const;                      // 0x100029e0
};

class Matrix
{
public:
    Vector3 origin;
    float   m[3][3];
    Vector3 scale;
};

// The ordinal accessor.  Only slot 2 is used: it maps (ordinal, element) to a
// pointer to the authored value.  Slots 0 and 1 exist to place it at
// [vtable+8]; what they do is not established.
class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);
};

// Reached through the slot's ADDRESS, not the slot: that is what produces
// ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX rather than the three-byte-shorter
// MOV ECX,[EAX+0x14].  Same idiom as player\stats.cpp.
inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar = -1;          // element index for a non-list property

// Player ordinals, group "Conversation distances", from the ObjectDef schema.
const int kOrdConversationRadius = 119;  // "Minimum distance to listener (lu)", 2.0
const int kOrdVerticalThreshold  = 120;  // "Maximum height difference ... (lu)", 0.5
const int kOrdConversationAngle  = 121;  // "Activation Angle (degrees)", 45.0

class GameObject
{
public:
    virtual void *Slot00();
    virtual void *Slot04();
    virtual void *Slot08();
    virtual void *Slot0c();
    virtual void *Slot10();
    virtual void *Slot14();
    virtual void *Slot18();
    virtual void *Slot1c();
    virtual void *Slot20();
    virtual void *Slot24();
    virtual void *Slot28();
    virtual void *Slot2c();
    virtual void *Slot30();
    virtual void *Slot34();
    virtual void *Slot38();
    virtual void *Slot3c();
    virtual void *Slot40();
    virtual void *Slot44();
    virtual const Vector3 *GetConversationPoint();   // [vtable+0x48]

    char           unknown_04[0x10];
    PropertyBlock *properties;      // +0x14
    char           unknown_18[0xd4];
    Matrix         transform;       // +0xec, origin doubles as the position
};

// Six bytes per entry - LEA EAX,[EAX+EAX*2] then a *2 index scale - so the
// struct is packed.  Only the first field is read here.
#pragma pack(push, 2)
struct ObjectLinkEntry
{
    GameObject     *object;   // +0x00
    unsigned short  tag;      // +0x04, purpose unestablished
};
#pragma pack(pop)

class LinkNotify
{
public:
    virtual void Slot0();
    virtual void OnBrokenLink(unsigned short link);   // [vtable+4]
};

class ObjectLinkList
{
public:
    char             unknown_00[0x10];
    LinkNotify       notify;     // +0x10, a subobject, not a pointer
    ObjectLinkEntry *entries;    // +0x14
    char             unknown_18[4];
    int              count;      // +0x1c
};

// A variadic member function is __cdecl in this ABI and `this` is pushed last,
// so the call site reads PUSH arg / PUSH fmt / PUSH this / CALL [vtable+0xc] /
// ADD ESP,0xc.  That is what identifies it as printf-shaped.
class MessageSink
{
public:
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Printf(const char *format, ...);   // [vtable+0xc]
};

class Console
{
public:
    char         unknown_00[4];
    MessageSink *sink;    // +0x04
};

class GameMode
{
public:
    virtual void Slot00();
    virtual void Slot04();
    virtual void Slot08();
    virtual void Slot0c();
    virtual void Slot10();
    virtual void Slot14();
    virtual void Slot18();
    virtual void Slot1c();
    virtual void Slot20();
    virtual void Slot24();
    virtual void Slot28();
    virtual void Slot2c();
    virtual void Slot30();
    virtual int  IsBlocked();   // [vtable+0x34]
};

extern ObjectLinkList *g_objectLinks;   // 0x101326cc
extern Console        *g_console;       // 0x1013269c
extern GameMode       *g_gameMode;      // 0x101326a8

class StateSet
{
public:
    int IsSet(int state) const;   // 0x1000f1e0
};

const int kStateA = 6;      // names unknown; the numbers are the evidence
const int kStateB = 4;
const int kStateC = 0x15;

const int kNoTarget = -6;   // one value of a result enumeration not recovered

class Player
{
public:
    void          *field00;
    GameObject    *object;         // +0x04
    char           unknown_08[0x4c];
    StateSet       states;         // +0x54
    char           unknown_55[0x77];
    unsigned short convLink;       // +0xcc, the conversation target's link id

    int CheckConversationTarget();
};

// 0x10055650, 816 bytes.
//
// Shape notes, all of them load-bearing:
//
//   * one nested if with a single trailing `return kNoTarget`.  Written as a
//     chain of early returns VC6 duplicates the fifteen-byte epilogue at every
//     one of them instead of tail-merging, and the JZ at the top of the
//     original is a jump to a shared epilogue.
//   * `!(A > B)` and `!(A < B)` rather than `A <= B` and `A >= B`.  VC6 picks
//     the FNSTSW mask from the operator as written and the four are distinct:
//     `>` gives TEST AH,5, `>=` gives TEST AH,0x41, `<` gives AND EAX,0x4100
//     and `<=` gives AND EAX,0x100.  The original uses TEST AH,5 for the
//     height and distance guards, so those were written with `>` and negated.
//   * the vertical threshold is read into a named local first.  Inlined into
//     the comparison the FLD moves after the FABS; as a local it stays at the
//     call and VC6 pays the FXCH the original has.
//   * 2048.0f spelled as a literal.  As a named `const float` VC6 loads the
//     constant and multiplies by ST instead of doing FMUL against it.
//   * two Vector3 locals reused through Set() rather than scoped temporaries.
//     The frame is 0x1c bytes - one float and two vectors - so the second
//     vector is the same storage three times over.
//
// WHAT STILL DIFFERS, and why it is believed unreachable from source
//
// The first 486 bytes are exact.  Three places after that differ, all of them
// x87 scheduling rather than instruction selection, and all three share one
// signature: the original leaves a value on the FPU stack that ours consumes
// at its last use, then discards it with an explicit FSTP ST(0) at the next
// control-flow point.
//
//   * the cone pre-test.  The original evaluates all three scaled-axis
//     products onto the FPU stack first and then folds the vector in with
//     FXCH; ours sinks each product into its own term, which is ten bytes
//     shorter.  Everything else about the block - the term order z, x, y, the
//     operand order, the two branch encodings - already agrees.  Fifteen
//     spellings were tried: separate locals, const locals, a local for
//     scale.z, an array, a struct, comma-sequenced assignments, += chains, and
//     four shapes of __inline helper taking the products as parameters.  VC6
//     sinks a single-use float definition into its use in every one of them,
//     and spills to memory in the two that stop it.  Nothing tried keeps three
//     of them live on the stack.
//   * the normalisation.  The original multiplies all three components with
//     FLD/FMUL ST(1)/FSTP and pops the reciprocal afterwards; ours folds the
//     third into the reciprocal, four bytes shorter.  Note that the matched
//     Vector3::operator/= at 0x100044e0 does fold its last one, so this is not
//     a spelling this codebase settles either way.
//   * the last two dot products come out z, y, x where the original has
//     z, x, y.  Reordering the source terms changes nothing - VC6 normalises
//     the sum.  The squared distance a few lines earlier, written identically,
//     does come out z, x, y, and the only difference between the two sites is
//     that the radius is live on the FPU stack across the first one.
//
// Those three cost fourteen bytes between them, and every byte after the first
// is a consequence of the shift rather than an independent error: the tail
// realigns exactly, instruction for instruction, once the offset is removed.
int Player::CheckConversationTarget()
{
    if (convLink != 0)
    {
        if (convLink > g_objectLinks->count + 1)
        {
            g_console->sink->Printf(
                "Object link: %d beyond array bounds!  May exhibit odd behavior!",
                convLink);
            __asm { int 3 }
        }

        GameObject *other = g_objectLinks->entries[convLink - 1].object;

        if (other == 0)
        {
            g_objectLinks->notify.OnBrokenLink(convLink);
            convLink = 0;
        }
        else if (states.IsSet(kStateA) == 0 &&
                 states.IsSet(kStateB) == 0 &&
                 states.IsSet(kStateC) == 0 &&
                 g_gameMode->IsBlocked() == 0)
        {
            Vector3 d;
            Vector3 t;

            const Vector3 *p = &object->transform.origin;
            d.Set(p->x - other->transform.origin.x,
                  p->y - other->transform.origin.y,
                  p->z - other->transform.origin.z);

            float vertical =
                *(float *)ReadProperty(&object->properties, kOrdVerticalThreshold, kScalar);

            // y is up: this is the component the height threshold guards, and
            // the one zeroed below to flatten the separation into the XZ plane.
            if (!(fabs(d.y) > vertical * 2048.0f))
            {
                const Vector3 *q = &object->transform.origin;
                const Vector3 *c = other->GetConversationPoint();
                t.Set(q->x - c->x, q->y - c->y, q->z - c->z);

                d.x = t.x;
                d.z = t.z;
                d.y = 0.0f;

                float radius =
                    *(float *)ReadProperty(&object->properties, kOrdConversationRadius, kScalar)
                    * 2048.0f;

                if (!(d.x * d.x + d.y * d.y + d.z * d.z > radius * radius))
                {
                    float cone = (float)cos(
                        *(float *)ReadProperty(&object->properties, kOrdConversationAngle, kScalar)
                        * 0.017453292f);

                    // Skipped entirely at an activation angle of zero, where
                    // the cosine is 1 and the cone tests below cannot pass.
                    if (cone < 1.0f)
                    {
                        float ox = other->transform.m[0][2] * other->transform.scale.z;
                        float oy = other->transform.m[1][2] * other->transform.scale.z;
                        float oz = other->transform.m[2][2] * other->transform.scale.z;

                        if (d.z * oz + ox * d.x + oy * d.y > 0.0f)
                            return kNoTarget;
                    }

                    {
                        float inv = 1.0f / d.Length();
                        d.x *= inv;
                        d.y *= inv;
                        d.z *= inv;

                        t.Set(other->transform.m[0][2] * other->transform.scale.z,
                              other->transform.m[1][2] * other->transform.scale.z,
                              other->transform.m[2][2] * other->transform.scale.z);

                        if (!(t.x * d.x + t.y * d.y + t.z * d.z > cone))
                        {
                            const Matrix *mine = &object->transform;

                            t.Set(mine->m[0][2] * mine->scale.z,
                                  mine->m[1][2] * mine->scale.z,
                                  mine->m[2][2] * mine->scale.z);

                            if (!(t.x * d.x + t.y * d.y + t.z * d.z < cone))
                                return 0;
                        }
                    }
                }
            }
        }
    }

    return kNoTarget;
}
