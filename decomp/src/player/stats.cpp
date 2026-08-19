// Fellowship.rfl - two Player property getters.
//
//   0x10057b60  22 bytes  Player::GetMaxPurity   ordinal 113
//   0x1005c4e0  25 bytes  Player::GetMaxMana     ordinal 139
//
// Both are leaf functions carrying no relocations at all, so the byte
// comparison is unmasked and a match is unambiguous.
//
// Neither reads a C++ member. Both go through the engine's one property-read
// shape, described in documentation\OBJECT-MODEL.md: load the owning game
// object, take the address of its property-block slot, push the element index
// and the property's ordinal in its class's schema, and call the accessor at
// [vtable+8], which returns a pointer to the value.
//
// The names are real. Player is ObjectDef class 0x1000e and its schema lists
// MaxPurity (float, default 100.0) at ordinal 113 and MaxMana (float, default
// 0.0) at ordinal 139 - both above the 154-property ceiling problem only in
// the sense that they are consistent with Player and with nothing observed
// here that contradicts it.

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
// +0x14 is established by these two functions.
class GameObject
{
public:
    char unknown_00[0x14];
    PropertyBlock *properties;   // +0x14
};

// The read is written against the *address* of the slot rather than the slot's
// value, and that is load-bearing: it is what produces `ADD EAX,0x14` followed
// by `MOV EAX,[EAX]`. Reading `object->properties` directly folds both into a
// single `MOV ECX,[EAX+0x14]` and the function comes out three bytes short.
// Given that the same block slot is attested at three different offsets in
// three different owners, a helper over the slot address is what the original
// source almost certainly had.
inline void *ReadProperty(PropertyBlock **slot, int ordinal, int element)
{
    return (*slot)->GetValue(ordinal, element);
}

const int kScalar = -1;          // element index for a non-list property

// Player property ordinals, from the ObjectDef schema for class 0x1000e.
const int kOrdMaxPurity = 113;   // "Maximum Purity", float, default 100.0
const int kOrdMaxMana   = 139;   // "Maximum Mana",   float, default   0.0

class Player
{
public:
    void       *unknown_00;      // +0x00
    GameObject *object;          // +0x04

    float GetMaxPurity();
    float GetMaxMana();
};

// 0x10057b60, 22 bytes.
float Player::GetMaxPurity()
{
    return *(float *)ReadProperty(&object->properties, kOrdMaxPurity, kScalar);
}

// 0x1005c4e0, 25 bytes. Identical but for the ordinal, which needs a four-byte
// immediate (`68 8B 000000`) where 113 fits in one (`6A 71`). That is the
// assembler's encoding choice, not a difference in the source.
float Player::GetMaxMana()
{
    return *(float *)ReadProperty(&object->properties, kOrdMaxMana, kScalar);
}
