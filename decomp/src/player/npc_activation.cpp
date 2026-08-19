// Fellowship.rfl - 0x1005efa0, 348 bytes.
//
// THE FILE NAME IS WRONG, AND SO WAS THE ASSIGNMENT THAT PRODUCED IT.
// This function does not read Player's "Camera Options" group. It reads
// TurnOnDistance and TurnOffDistance off the NPC family, and it is an NPC
// activation sweep. The name is kept only because a worker may not rename its
// own file; whoever integrates this should move it to something like
// player\npc_activation.cpp. See "Which class the ordinals belong to" below.
//
// WHAT IT DOES
//
// Walks the whole global object-reference list once. For every entry that
// resolves, is a character, and passes a liveness guard, it measures the
// squared distance from that object's position to the sweeping object's
// position and drives a two-radius hysteresis:
//
//   distance < TurnOnDistance   and the character is off  ->  turn it on
//   distance > TurnOffDistance  and the character is on   ->  turn it off
//   distance < 3 lu                                       ->  a third call
//
// TurnOnDistance defaults to 30 lu and TurnOffDistance to 40 lu, so the
// turn-off radius is the larger one and a character on the boundary does not
// flicker. That is what makes the reading certain: the smaller radius is the
// one that switches on.
//
// WHICH CLASS THE ORDINALS BELONG TO - and it is not Player
//
// The two reads are ordinals 81 then 80, and they are taken off `other`, the
// object being iterated, not off `this`. `other` is also the object whose
// character subobject at +0xc8 receives all three calls, so the ordinals index
// that object's schema, and that object is a character, not the sweeper.
//
// Ten ObjectDef classes have more than 81 properties. Nine of them declare
// ordinals 80 and 81 as floats, so the type test in decomp\tools\ordmap.py
// does not narrow it to one:
//
//   Player            80 FromPlayerOffsetY  81 FromPlayerOffsetZ
//   Level Properties  80 CacheSize_Texture  81 CacheSize_Model
//   NPC, Nazgul, Tolkien NPC, BarrowWight, FellBeast, OldManWillow, Balrog
//                     80 TurnOffDistance    81 TurnOnDistance
//
// The code decides between them, three ways:
//
//   * hysteresis. The NPC family puts the *smaller* default (TurnOnDistance,
//     30 lu) at ordinal 81, which is the one compared on the "switch on"
//     branch, and the larger (TurnOffDistance, 40 lu) at ordinal 80, on the
//     "switch off" branch. Under the Player reading the switch-on radius
//     would be FromPlayerOffsetZ, default 0.0 - that branch would be dead
//     code on every unedited Player in the game - and the switch-off radius
//     FromPlayerOffsetY, 500.0 wu, which scaled as below comes out at 500 lu.
//   * units. Both constants below are the square of a level-unit distance
//     times 2048, so the properties are authored in lu. TurnOnDistance and
//     TurnOffDistance are labelled "(lu)" in the schema. All three
//     FromPlayerOffset properties are labelled "(wu)".
//   * the group name. The NPC family's ordinals 80-83 are the group
//     "Turning On & Off", and this function turns things on and off.
//
// Level Properties is excluded because it is the per-level singleton, not
// something held by every entry of an object list.
//
// So the ordinals name NPC-family properties. Which of the seven classes
// cannot be told apart and does not need to be: all seven carry the same two
// keys at the same two ordinals, which is exactly the shared-accessor
// situation documentation\ORDINAL-MAP.md records for JawChan. The read is
// against the family's common schema.
//
// WHAT IS NOT ESTABLISHED
//
//   * the class of `this`. Nothing here identifies it. It is called Player
//     because the reference point for a "turn on when the player is near"
//     sweep is the player, because the schema labels these as distances to
//     the player, and because 0x1005efa0 lies inside the
//     0x10055650-0x100621a2 stretch ORDINAL-MAP.md reads as Player's
//     implementation. That is judgement, not proof, and this function adds
//     nothing to it.
//   * Character. The subobject at gameobject+0xc8 is given a name because
//     Player and the NPC family share the ObjectDef groups Health, Melee
//     Attack Positions and Incoming Messages, and because
//     player\health_regen.cpp found this same +0x08/+0x0e/+0x12/+0x1a field
//     block on Player. A shared C++ base is the natural reading of both; it
//     is not proven.
//   * TurnOn, TurnOff and NotifyPlayerNearby - 0x10093f10, 0x10093f70 and
//     0x10094020. The names are the roles the call sites give them and
//     nothing more. All three operands are rel32 relocations and are masked.
//   * IsCharacterObject, 0x100921f0. __cdecl, one argument, result tested
//     against zero. All that is known is that a false result substitutes null
//     for the object's own +0xc8.
//   * ObjectRef::Resolve, 0x1009b1e0. __thiscall on the *address* of a
//     two-byte list element - LEA ECX,[EAX+EBP*2] - returning a game object
//     or null. It is not Handle::Resolve, which is 0x100051c0.
//   * +0x0e, +0x12, +0x1a and bit 13 of +0x39a. Runtime state. The names say
//     where they are, not what they mean.
//
// The constants were read out of the image: 0x100ee250 is 4194304.0f,
// 0x100efff4 is 37748736.0f and 0x100ed0f0 is 0.0f.

class Character;

// The ordinal accessor. Only slot 2 is used: it maps (ordinal, element) to a
// pointer to the authored value. Slots 0 and 1 exist to place it at
// [vtable+8]; what they do is not established.
class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);
};

// Three floats at +0xec of a game object. The full class is math\vector3.h;
// only the members are needed here and none of its operators is called, so it
// is declared locally rather than included across modules.
class Vector3
{
public:
    float x, y, z;
};

// A game object. The property block is reached through the slot directly -
// MOV ECX,[ESI+0x14] / MOV EDX,[ECX] - not through the slot's address, which
// is the short form WORKER-BRIEF.md documents for PropertyOwner+0x14.
class GameObject
{
public:
    char           unknown_00[0x14];
    PropertyBlock *properties;      // +0x14
    char           unknown_18[0xb0];
    Character     *character;       // +0xc8
    char           unknown_cc[0x20];
    Vector3        pos;             // +0xec
};

// A two-byte reference into the object registry, resolved by 0x1009b1e0.
class ObjectRef
{
public:
    GameObject *Resolve() const;

private:
    unsigned short m_id;
};

// The list and its length, both re-read on every iteration.
extern ObjectRef *g_objectRefs;      // 0x10132638
extern int        g_objectRefCount;  // 0x10132640

// 0x100921f0 - see the header comment; identity unestablished.
int IsCharacterObject(GameObject *object);

const int kScalar = -1;                 // element index for a non-list property

// NPC-family ordinals, from the ObjectDef schema, group "Turning On & Off".
// The same two keys sit at the same two ordinals on NPC, Nazgul, Tolkien NPC,
// BarrowWight, FellBeast, OldManWillow and Balrog.
const int kOrdTurnOffDistance = 80;     // "Turn Off Distance (lu)", 40.0
const int kOrdTurnOnDistance  = 81;     // "Turn On Distance (lu)",  30.0

// 4194304.0f = 2048 * 2048. The two distances are authored in level units and
// positions are stored in world units, so squaring a distance in lu and
// scaling by this puts it in the same units as the squared world-space
// separation. 2048 wu per lu is what that says.
const float kWorldUnitsPerLevelUnitSquared = 4194304.0f;

// 37748736.0f = (3 * 2048) * (3 * 2048) - a fixed three level units, squared.
const float kNearRadiusSquared = 37748736.0f;

// +0x0e, +0x12 and +0x1a are not 4-aligned, so the class is packed; pack(2) is
// the loosest setting that puts them there. player\health_regen.cpp found the
// same three fields, tested the same way, on Player.
#pragma pack(push, 2)

class Character
{
public:
    void       *field00;         // +0x000
    GameObject *object;          // +0x004

    // Declared volatile, and that is load-bearing rather than decoration. A
    // plain member lets VC6 collapse the test below to TEST byte ptr [EAX+8],1
    // - three bytes shorter, no dword load, no local - and the register
    // allocator then spills the loop counter to the stack, which moves every
    // byte of the function after it. The volatile read forces
    // MOV ECX,[EAX+8] / AND ECX,1 and gives the local a stack home, which is
    // the original's MOV dword ptr [ESP+0x10],ECX. Taking the local's address
    // instead produces the same bytes, and so does a volatile *local* over a
    // plain member. All three say the same thing about the original - the
    // value is materialised rather than folded into the branch - and volatile
    // on the member is the only one of the three that reads as ordinary
    // source.
    volatile unsigned flags08;   // +0x008  only bit 0 is tested here

    char        unknown_0c[2];   // +0x00c
    int         field0e;         // +0x00e  runtime state, meaning unknown
    float       field12;         // +0x012  runtime state, meaning unknown
    char        unknown_16[4];   // +0x016
    int         field1a;         // +0x01a  runtime state, meaning unknown
    char        unknown_1e[0x39a - 0x1e];
    unsigned    state39a;        // +0x39a  bit 13 is the on/off state

    void TurnOn();                     // 0x10093f10
    void TurnOff(int reason);          // 0x10093f70, called with 0
    void NotifyPlayerNearby();         // 0x10094020
};

#pragma pack(pop)

// The gated downcast. Two returns with the null one first: that is what puts
// the JNZ over XOR EDI,EDI and the MOV EDI,[ESI+0xc8] behind it. A ternary, or
// either single-exit spelling, swaps the two arms.
__inline Character *GetCharacterOf(GameObject *object)
{
    if (!IsCharacterObject(object))
        return 0;

    return object->character;
}

class Player
{
public:
    void       *field00;
    GameObject *object;          // +0x04

    void UpdateNpcActivation();
};

// 0x1005efa0, 348 bytes.
//
// The two distance tests use the two different float-compare forms VC6
// distinguishes, and they are not interchangeable: `onSq > distSq` gives
// AND EAX,0x4100 / JNZ, `offSq < distSq` gives TEST AH,5 / JP, and the guard's
// last term, `!(field12 <= 0.0f)`, gives TEST AH,0x41 / JNP. All three appear
// in this one function.
//
// The squares are two statements each rather than one expression: written as
// `d * d * kScale` VC6 reassociates to d * (d * kScale) and emits the two
// FMULs in the other order.
void Player::UpdateNpcActivation()
{
    for (int i = 0; i < g_objectRefCount; ++i)
    {
        GameObject *other = g_objectRefs[i].Resolve();

        if (other != 0)
        {
            Character *oc = other->character;
            unsigned enabled = oc->flags08 & 1;

            if (enabled == 0 || oc->field0e != 0 || oc->field1a != 0 ||
                !(oc->field12 <= 0.0f))
            {
                Character *target = GetCharacterOf(other);

                float onDist =
                    *(float *)other->properties->GetValue(kOrdTurnOnDistance, kScalar);
                float offDist =
                    *(float *)other->properties->GetValue(kOrdTurnOffDistance, kScalar);

                float offSq = offDist * offDist;
                offSq *= kWorldUnitsPerLevelUnitSquared;

                const Vector3 *mp = &object->pos;
                unsigned on = (target->state39a >> 13) & 1;

                float dx = other->pos.x - mp->x;
                float dy = other->pos.y - mp->y;
                float dz = other->pos.z - mp->z;
                float distSq = dz * dz + dy * dy + dx * dx;

                float onSq = onDist * onDist;
                onSq *= kWorldUnitsPerLevelUnitSquared;

                if (onSq > distSq && on == 0)
                {
                    target->TurnOn();
                }
                else if (offSq < distSq && on != 0)
                {
                    target->TurnOff(0);
                }

                if (distSq < kNearRadiusSquared)
                {
                    target->NotifyPlayerNearby();
                }
            }
        }
    }
}
