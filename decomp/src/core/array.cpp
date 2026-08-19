// 0x10004690, 42 bytes - walk an array of objects and invoke a member
// function on each element.
//
// __stdcall (RET 0x10, four dword arguments, callee cleans). ECX is loaded
// with the running element pointer before every CALL and the callee is passed
// no stack arguments, so the called function is __thiscall taking only `this`.
// This toolchain rejects the __thiscall keyword (C4234), so the callback is
// spelled as a pointer-to-member; under the default single-inheritance
// representation that is the same bare 4-byte code pointer the argument slot
// holds, and `(p->*m)()` emits exactly MOV ECX,p / CALL m.
//
// The guard is DEC EAX / JS - the sign of the *first* decrement of the count
// itself, not a strength-reduced `count - 1`. That only comes out of
// `while (--count >= 0)`; a `for (i = count - 1; i >= 0; --i)` costs an extra
// TEST EAX,EAX before the branch. The compiler then rewrites the sign-tested
// loop as a counted one, LEA EDI,[EAX+1] / DEC EDI / JNZ.
//
// `base` is walked in place rather than copied into a local `char *`: a local
// initialised before the loop is live at entry, and the compiler hoists its
// load above the guard, which the original does not do.

class ArrayElement;                    // name invented; the element type is unknown

typedef void (ArrayElement::*ElementMethod)();

class ArrayElement
{
public:
    void Apply();
};

void __stdcall ForEachArrayElement(void *base, int stride, int count,
                                   ElementMethod method)
{
    while (--count >= 0)
    {
        (((ArrayElement *)base)->*method)();
        base = (char *)base + stride;
    }
}
