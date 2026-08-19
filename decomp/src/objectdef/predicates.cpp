// Fellowship.rfl - three exported predicates over ObjectDef class ids.
//
//   0x1000d760  163 bytes  IsObjectLight
//   0x1000d810   16 bytes  IsObjectPortal
//   0x1000d820   16 bytes  IsObjectMoveNode
//
// All three are exported from Fellowship.rfl under exactly those names, with
// no decoration, so they are `extern "C"`. Each is a bare RET reading its
// arguments from [ESP+4] onward, i.e. __cdecl free functions.
//
// The argument is an ObjectDef class id - the same numbering the engine's own
// class table uses, so the ids below are named from that table rather than
// guessed at. IsObjectLight takes a second argument as well; see below.

// ObjectDef class ids, from the engine's class table.
const int kClassStaticLight     = 0x1000b;   // "Static Light"
const int kClassDynamicLight    = 0x1000c;   // "Dynamic Light"
const int kClassStaticSpotLight = 0x1002c;   // "Static Spot Light"
const int kClassDynamicSpotLight= 0x1002d;   // "Dynamic Spot Light"
const int kClassPortal          = 0x10108;   // "Portal"
const int kClassMoveNodeObject  = 0x10140;   // "Move Node Object"

// The second argument of IsObjectLight is written before either helper call,
// and only ever its first dword, with a small distinct constant per class. It
// is therefore a descriptor whose leading field is a light-kind tag, filled in
// for the caller when the class does name a light. The tag values are the four
// below; 0 and 1 are not produced here and are unestablished, as is everything
// past the first dword - the helper call is what fills that in.
struct LightDesc
{
    int kind;   // +0x00  one of the kLight* values below
    // remainder not established by this function
};

const int kLightStaticPoint   = 2;
const int kLightDynamicPoint  = 3;
const int kLightStaticSpot    = 4;
const int kLightDynamicSpot   = 5;

// Two __cdecl helpers taking the descriptor, one per light family, called
// after the tag is stored. Their identity is unestablished: only the call
// site's argument count, calling convention and the family split are known.
// Both call operands are rel32 relocations and are masked in the comparison.
extern "C" void FillPointLightDesc(LightDesc *desc);   // 0x1002bdb0
extern "C" void FillSpotLightDesc(LightDesc *desc);    // 0x10044040

// 0x1000d760, 163 bytes.
//
// Returns non-zero when the class id names a light of any of the four kinds,
// and - when `desc` is non-null - tags the descriptor with which kind it is
// and hands it to that family's helper. A null `desc` still returns non-zero;
// the JZ from both null tests lands on the shared `MOV EAX,1 / RET` tail.
//
// Return type is `int`, not `bool`: the returns are `MOV EAX,1` and
// `XOR EAX,EAX`, with no byte-sized SETcc anywhere.
extern "C" int IsObjectLight(int classId, LightDesc *desc)
{
    if (classId == kClassStaticLight || classId == kClassDynamicLight)
    {
        if (desc != 0)
        {
            if (classId == kClassStaticLight)
            {
                desc->kind = kLightStaticPoint;
            }
            else if (classId == kClassDynamicLight)
            {
                desc->kind = kLightDynamicPoint;
            }
            FillPointLightDesc(desc);
        }
    }
    else if (classId == kClassStaticSpotLight || classId == kClassDynamicSpotLight)
    {
        if (desc != 0)
        {
            if (classId == kClassStaticSpotLight)
            {
                desc->kind = kLightStaticSpot;
            }
            else if (classId == kClassDynamicSpotLight)
            {
                desc->kind = kLightDynamicSpot;
            }
            FillSpotLightDesc(desc);
        }
    }
    else
    {
        return 0;
    }
    return 1;
}

// 0x1000d810, 16 bytes. XOR EAX,EAX / CMP / SETZ AL - a byte-sized SETcc
// return, so the source returns `bool`.
extern "C" bool IsObjectPortal(int classId)
{
    return classId == kClassPortal;
}

// 0x1000d820, 16 bytes. Same shape as IsObjectPortal.
extern "C" bool IsObjectMoveNode(int classId)
{
    return classId == kClassMoveNodeObject;
}
