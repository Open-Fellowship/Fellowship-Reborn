// 0x0048bdd0 (Fellowship.exe), 40 bytes.
//
// __thiscall, bare RET, no arguments: `int Method()` on some class. It reads a
// pointer at +0x166, calls that object's vtable slot 35 ([vtable + 0x8c])
// passing the object as a stack argument, and:
//   - returns 0 if the answer is non-zero,
//   - otherwise increments a dword at +0x64 in `this` and returns 1.
//
// What it gates is NOT established. It sits a few hundred bytes below known
// fog-state code, so it is somewhere in the render-state block, but nothing
// here says what the question is, what the counter counts, or what the caller
// does with the answer. Every name below describes only the shape.
//
// Two things the encoding forces:
//   - +0x166 is not a multiple of 4, so the containing class is packed.
//   - the callee's `this` arrives on the stack (PUSH EAX) rather than in ECX,
//     so the virtual is __stdcall, not __thiscall.

// The subobject at +0x166. Only vtable slot 35 is used here; the 35 slots
// ahead of it are declared, never defined, purely to place it at +0x8c.
class GateSubject
{
public:
    virtual int __stdcall Slot00();
    virtual int __stdcall Slot01();
    virtual int __stdcall Slot02();
    virtual int __stdcall Slot03();
    virtual int __stdcall Slot04();
    virtual int __stdcall Slot05();
    virtual int __stdcall Slot06();
    virtual int __stdcall Slot07();
    virtual int __stdcall Slot08();
    virtual int __stdcall Slot09();
    virtual int __stdcall Slot10();
    virtual int __stdcall Slot11();
    virtual int __stdcall Slot12();
    virtual int __stdcall Slot13();
    virtual int __stdcall Slot14();
    virtual int __stdcall Slot15();
    virtual int __stdcall Slot16();
    virtual int __stdcall Slot17();
    virtual int __stdcall Slot18();
    virtual int __stdcall Slot19();
    virtual int __stdcall Slot20();
    virtual int __stdcall Slot21();
    virtual int __stdcall Slot22();
    virtual int __stdcall Slot23();
    virtual int __stdcall Slot24();
    virtual int __stdcall Slot25();
    virtual int __stdcall Slot26();
    virtual int __stdcall Slot27();
    virtual int __stdcall Slot28();
    virtual int __stdcall Slot29();
    virtual int __stdcall Slot30();
    virtual int __stdcall Slot31();
    virtual int __stdcall Slot32();
    virtual int __stdcall Slot33();
    virtual int __stdcall Slot34();

    // slot 35, [vtable + 0x8c]. A non-zero answer makes the gate refuse.
    virtual int __stdcall IsRefused();
};

#pragma pack(push, 1)
class RenderGate
{
public:
    char         m_unknown00[0x64];         // +0x00  not touched here
    int          m_counter;                 // +0x64  bumped on the accepting path
    char         m_unknown68[0x166 - 0x68]; // +0x68  not touched here
    GateSubject *m_subject;                 // +0x166 asked the question

    int Gate();
};
#pragma pack(pop)


int RenderGate::Gate()
{
    if (m_subject->IsRefused() != 0) {
        return 0;
    }
    m_counter = m_counter + 1;
    return 1;
}
