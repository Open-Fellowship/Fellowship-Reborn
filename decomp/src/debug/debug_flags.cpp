// 0x00411ba0 (Fellowship.exe), 29 bytes - read one value out of the engine's
// built-in debug menu.
//
// ECX holds `this` and the function ends RET 4, so it is __thiscall taking a
// single dword. The index is loaded from [ESP+4] before anything else touches
// the object, and the result comes back in EAX, so it is a plain value getter
// rather than an out-parameter form.
//
// The values live in an array hanging off the object at +0xe0; the index scale
// is *4, so they are dword-sized. Index 0x2f is special-cased and read from a
// standalone global at 0x00543434 instead of from the array. What that one
// entry means is not established - only that the setter at 0x00411800 routes
// the same index to the same global, so the two agree on it.
//
// Branch polarity: the original's JZ jumps to a tail that loads the global, so
// the array read is the body of the `if` and the global read is the fallthrough
// return, i.e. the test is written `!=` and the special case comes last.
//
// The only relocation is the moffs32 operand of MOV EAX,[0x00543434], so four
// of the 29 bytes are masked in the comparison.

class DebugMenuFlags
{
public:
    int GetValue(int index) const;

private:
    char m_unknown_00[0xe0];      // contents not established by this function
    int *m_values;                // +0xe0  index * 4
};

// The value index 0x2f keeps outside the array. Not otherwise identified: the
// getter and setter agree that it is separate, and nothing here says why.
extern int g_debugMenuValue2f;

int DebugMenuFlags::GetValue(int index) const
{
    if (index != 0x2f)
    {
        return m_values[index];
    }
    return g_debugMenuValue2f;
}
