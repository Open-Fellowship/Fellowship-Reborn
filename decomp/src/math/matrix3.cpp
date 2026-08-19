// 100047a0.cpp - Matrix3::SetIdentity, 0x100047a0 in Fellowship.rfl.
//
// ECX is the only input and the function ends in a bare RET with no stack
// cleanup, so it is a __thiscall member taking no arguments. EAX holds 0 on
// exit rather than `this`, so it is not a constructor.
//
// The stores cover offsets 0x00..0x20 in four-byte steps: a 3x3 float matrix
// (0x00, 0x10, 0x20 are the diagonal).
//
// MSVC emits the stores in exact source order, and the original's order is
// 0x20, 0x10, 0x00, then 0x1c, 0x18, 0x14, 0x0c, 0x08, 0x04 - two runs, each
// descending. That is a chained assignment: `a = b = c = v` assigns
// right-to-left, so one statement per constant reproduces both runs, and the
// 1.0f statement coming first is why EAX can be reloaded by XOR for the zeros.

class Matrix3
{
public:
    float m[3][3];

    void SetIdentity();      // 0x100047a0, 34
};

void Matrix3::SetIdentity()
{
    m[0][0] = m[1][1] = m[2][2] = 1.0f;
    m[0][1] = m[0][2] = m[1][0] = m[1][2] = m[2][0] = m[2][1] = 0.0f;
}
