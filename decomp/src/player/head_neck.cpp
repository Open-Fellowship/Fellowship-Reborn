// Fellowship.rfl - 0x10060210, 77 bytes.
//
// Player, "Head & Neck": clears the state of the upper-body pitch channel.
//
// The function reads ordinal 129 - UBPitchChannel - twice, through the ordinal
// accessor at [vtable+8]. A channel property holds a channel index and its
// default is "none", encoded as -1; that is what the CMP against -1 tests.
// So the two reads have different jobs and both are accounted for:
//
//   read 1   is the "is a channel authored at all?" test. If not, nothing
//            happens and the function returns 0.
//   read 2   supplies the same index again as the argument to the virtual at
//            [vtable+0xb4] on the object at gameobject+0x10, which resolves an
//            index to a per-channel state block.
//
// It is not a getter: it returns 0 unconditionally and its only effect is the
// store of zero to +0x0c of that state block. The property is re-read rather
// than kept in a register because the accessor is a virtual call the compiler
// cannot see through, not because the two reads mean different things.
//
// Class attribution: ordinal 129 is only legal for Player (166 properties) and
// Control Input Names (154). Every Control Input Names property is a string,
// and this code compares the value to -1 and hands it to a channel lookup, so
// Control Input Names is excluded on type. Player's ordinal 129 is
// UBPitchChannel, type channel, default none - which is exactly -1. This
// function therefore supports the reading of 0x10055650-0x100621a2 as Player.
//
// Only Player, "Head & Neck" and UBPitchChannel are real names, from the
// ObjectDef table. The two engine classes below are invented.

// The ordinal accessor. Only slot 2 is used here: it maps (ordinal, element)
// to a pointer to the authored value. Slots 0 and 1 exist to place it at
// [vtable+8]; what they do is not established.
class PropertyBlock
{
public:
    virtual void *Slot0();
    virtual void *Slot1();
    virtual void *GetValue(int ordinal, int element);
};

// What GetChannel hands back. Only +0x0c is touched, and only ever written
// zero, so its meaning is not established - do not read the name as more than
// "the field this function clears".
struct ChannelState
{
    char reserved[0x0c];
    int  field0c;                // +0x0c  set to 0 by this function
};

// The object at gameobject+0x10. Its identity is unestablished; all this
// function shows is that it turns a channel index into a ChannelState through
// the virtual in slot 45, [vtable+0xb4]. The 45 slots ahead of it are declared
// only to place it at that offset.
class ChannelOwner
{
public:
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
    virtual void Slot14();
    virtual void Slot15();
    virtual void Slot16();
    virtual void Slot17();
    virtual void Slot18();
    virtual void Slot19();
    virtual void Slot20();
    virtual void Slot21();
    virtual void Slot22();
    virtual void Slot23();
    virtual void Slot24();
    virtual void Slot25();
    virtual void Slot26();
    virtual void Slot27();
    virtual void Slot28();
    virtual void Slot29();
    virtual void Slot30();
    virtual void Slot31();
    virtual void Slot32();
    virtual void Slot33();
    virtual void Slot34();
    virtual void Slot35();
    virtual void Slot36();
    virtual void Slot37();
    virtual void Slot38();
    virtual void Slot39();
    virtual void Slot40();
    virtual void Slot41();
    virtual void Slot42();
    virtual void Slot43();
    virtual void Slot44();
    virtual ChannelState *GetChannel(int channel);   // [vtable+0xb4]
};

// The game object holding the authored property block.
class PropertyOwner
{
public:
    char           reserved[0x10];
    ChannelOwner  *channels;     // +0x10  identity unestablished; see above
    PropertyBlock *properties;   // +0x14
};

class Player
{
public:
    void          *field00;
    PropertyOwner *object;       // +0x04

    int ClearUpperBodyPitch();
};

const int kOrdUBPitchChannel = 129;   // Player, "Head & Neck", UBPitchChannel
const int kScalar            = -1;    // element index for a non-list property
const int kNoChannel         = -1;    // a channel property's "none"

// Taking the address of the member and dereferencing it separately is what
// produces the original's ADD EAX,0x14 / MOV EAX,[EAX] / MOV ECX,EAX. Writing
// object->properties->GetValue(...) folds to MOV ECX,[EAX+0x14] and is four
// bytes shorter. Hoisting object->channels above the second read is likewise
// load-bearing: it is what keeps the object pointer live in EAX so the second
// property load can use the displacement form.
int Player::ClearUpperBodyPitch()
{
    PropertyBlock *const *pp = &object->properties;

    if (*(int *)(*pp)->GetValue(kOrdUBPitchChannel, kScalar) != kNoChannel)
    {
        ChannelOwner *channels = object->channels;
        PropertyBlock *const *qq = &object->properties;

        ChannelState *state =
            channels->GetChannel(*(int *)(*qq)->GetValue(kOrdUBPitchChannel, kScalar));
        state->field0c = 0;
    }

    return 0;
}
