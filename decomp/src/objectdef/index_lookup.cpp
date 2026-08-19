// index_lookup.cpp - Fellowship.rfl.
//
//   0x1005e9d0  119 bytes
//   0x1005ea50  119 bytes
//   0x1005eb40  119 bytes
//
// Three near-clones. All 119 bytes, and byte for byte identical for their
// first 0x5a bytes: the same global, the same lookup, the same class test.
// They differ only in the last dozen bytes, which is the one field each one
// reports:
//
//   0x1005e9d0   (state[0x54] >> 7)  & 1     0 when there is no player
//   0x1005ea50   (state[0x50] >> 13) & 1     0 when there is no player
//   0x1005eb40    state[0x17c]              -1 when there is no player
//
// No call, no argument, bare RET - three __cdecl leaf getters over one global.
//
// What the shared head does: load the global at 0x101326cc, take its object at
// +0xb8, read that object's 16-bit ObjectDef index at +0x0c, and if the index
// is not 0xffff resolve it through the global ObjectDef entry list at
// 0x101326e4. If the record it names carries class id 0x1000e the object is a
// Player, and the subobject at +0xc8 is fetched; otherwise the result is null
// and the getter returns its "no player" constant.
//
// 0x1000e is not a guess: it is the ObjectDef id of the class the table names
// `Player` (documentation\generated\class-index.md), and +0xc8 is the offset
// documentation\OBJECT-MODEL.md already attributes to the Player subobject of
// a game object. Nothing here reads an authored property, so the fields at
// +0x50, +0x54 and +0x17c are runtime state, not schema, and are left unnamed.
//
// The lookup itself is ObjectDefEntryList::GetEntry (0x10008f30,
// decomp\src\objectdef\entry_list.cpp) inlined: the >= 0 / < count guard, the
// discarded PrepareEntry call, and the 36-byte stride as
// LEA EAX,[ESI+ESI*8] / LEA EAX,[ECX+EAX*4]. The declarations below are local
// copies on purpose; nothing shared is edited. The integrator should fold this
// record and list with the ones in entry_list.cpp and property_ref*.cpp.
//
// NOT MATCHED. 22 of 119 bytes (23 for 0x1005eb40), all of them in the last
// 21 bytes; offsets 0x00-0x5f are byte for byte identical, both relocations
// included. What is left is one code-layout decision and it does not appear to
// be reachable from source. The original merges the "no player" path into the
// value path and then tests the merged register:
//
//     MOV EAX,[EBX+0xc8] / JMP +2 / XOR EAX,EAX / POP ESI / POP EBX
//     TEST EAX,EAX / JNZ +1 / RET / <the getter>
//
// This compiler jump-threads it instead - it knows the merged value is zero on
// that path, so it sends the three failure branches straight to a
// POP ESI / XOR EAX,EAX / POP EBX / RET and duplicates the epilogue into the
// value arm. Same instructions, same 119 bytes, different arrangement. See the
// ruled-out list in the report; sixteen source shapes for that tail all give
// the threaded form, so this looks like a peephole the original build did not
// run rather than something the source chose.

// Note the index is *not* dereferenced through a null check: GetEntry's null
// return is merged (JMP +2 over a XOR EAX,EAX) and then read at +4 regardless,
// so the source is `GetEntry(i)->m_class_id`, an unguarded deref.


// One ObjectDef record. 36 bytes, from the stride; only +0x04 is read here.
struct ObjectDefEntry
{
    int m_unknown_00;
    int m_class_id;              // +0x04
    int m_unknown[7];
};


class ObjectDefEntryList
{
public:
    // Four slots ahead of the one that is called; declared and not defined,
    // which is enough to place them. The call is CALL dword ptr [EDX+0x10].
    virtual void Slot0();
    virtual void Slot1();
    virtual void Slot2();
    virtual void Slot3();
    virtual void PrepareEntry(int index, int flags);

    ObjectDefEntry *GetEntry(int index)
    {
        if (index >= 0 && index < m_count)
        {
            PrepareEntry(index, 0);
            return m_entries + index;
        }
        return 0;
    }

    ObjectDefEntry *m_entries;   // +0x04
    int m_unknown_08;
    int m_count;                 // +0x0c
};

extern ObjectDefEntryList *g_objectDefEntryList;   // 0x101326e4


// The ObjectDef class id of `Player`, from the engine's own class table.
const int kClassPlayer = 0x1000e;

// 0xffff in the game object's 16-bit ObjectDef index means "no def".
const unsigned short kNoObjectDef = 0xffff;


// The Player subobject at gameobject+0xc8. Only three fields are touched, all
// of them runtime state; none is an authored property, so none has a name from
// the ObjectDef schema and none is invented here.
class Player
{
public:
    char m_unknown_00[0x50];
    unsigned int m_state_50;     // +0x50, bit 13 is reported by 0x1005ea50
    unsigned int m_state_54;     // +0x54, bit 7 is reported by 0x1005e9d0
    char m_unknown_58[0x17c - 0x58];
    int m_value_17c;             // +0x17c, reported whole by 0x1005eb40
};


// Any game object. The 16-bit def index at +0x0c is the documented layout;
// the Player subobject at +0xc8 is the one documented in OBJECT-MODEL.md.
class GameObject
{
public:
    char m_unknown_00[0x0c];
    unsigned short m_defIndex;   // +0x0c, 0xffff = none
    char m_unknown_0e[0xc8 - 0x0e];
    Player *m_player;            // +0xc8
};


// Whatever the global at 0x101326cc points at - a world or session object.
// Only its +0xb8 is read, and only ever as the object these getters report on,
// so it is named for that and nothing more.
class World
{
public:
    char m_unknown_00[0xb8];
    GameObject *m_object;        // +0xb8
};

extern World *g_world;           // 0x101326cc


// Two exits, and that is load bearing. Written as one expression the compare
// folds into the branch; with the 0xffff early return the inlined result is
// materialised in EAX, which is the original's
// XOR EDX,EDX / CMP / SETZ DL / MOV EAX,EDX / TEST EAX,EAX.
__inline int IsObjectDefPlayer(unsigned short defIndex)
{
    if (defIndex != kNoObjectDef)
        return g_objectDefEntryList->GetEntry(defIndex)->m_class_id == kClassPlayer;

    return 0;
}


__inline Player *GetLocalPlayer()
{
    GameObject *object = g_world->m_object;

    if (object != 0 && IsObjectDefPlayer(object->m_defIndex))
        return object->m_player;

    return 0;
}


// 0x1005e9d0
int LocalPlayerState54Bit7()
{
    Player *player = GetLocalPlayer();
    if (player == 0)
        return 0;

    return (player->m_state_54 >> 7) & 1;
}


// 0x1005ea50
int LocalPlayerState50Bit13()
{
    Player *player = GetLocalPlayer();
    if (player == 0)
        return 0;

    return (player->m_state_50 >> 13) & 1;
}


// 0x1005eb40
int LocalPlayerValue17c()
{
    Player *player = GetLocalPlayer();
    if (player == 0)
        return -1;

    return player->m_value_17c;
}
