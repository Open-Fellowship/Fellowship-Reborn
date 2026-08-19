// vector3.h - the Vector3 class as the original declares it, as far as the
// matched code generation constrains it.
//
// Two conventions run through this whole codebase and both are load-bearing.
// They are documented once here rather than on every function:
//
//   * A function returning a Vector3 by value uses a **named local assigned
//     in reverse member order** - r.z, then r.y, then r.x. The x87 stack
//     forces it: all three components are computed before any is stored, and
//     FSTP pops from the top, so storing x first means pushing x last.
//     Assigning forwards and returning a constructed temporary also works,
//     but costs an FXCH - see Matrix::GetColumnFP, where the original pays it.
//
//   * The **copy constructor is declared explicitly**. Nothing calls it out of
//     line and it appears nowhere in the binary, but without it the compiler
//     builds a temporary and copies rather than constructing straight into the
//     caller's return buffer.

#ifndef VECTOR3_H
#define VECTOR3_H

class Vector3
{
public:
    float x, y, z;

    Vector3() {}
    Vector3(float ix, float iy, float iz) : x(ix), y(iy), z(iz) {}
    Vector3(const Vector3 &o) : x(o.x), y(o.y), z(o.z) {}

    Vector3 &operator=(const Vector3 &v);          // 0x10002230, 25
    void     operator-=(const Vector3 &v);         // 0x10002250, 31
    void     operator*=(float s);                  // 0x10002270, 31
    void     operator+=(const Vector3 &v);         // 0x10002290, 31
    Vector3  operator+(const Vector3 &v) const;    // 0x100022d0, 35
    Vector3  operator-(const Vector3 &v) const;    // 0x10002300, 35
    Vector3  Normalized() const;                   // 0x10002330, 75
    float    dot(const Vector3 &v) const;          // 0x10002380, 27
    float    LengthSquared() const;                // 0x100023a0, 31
    void     operator/=(float s);                  // 0x100044e0, 33
    Vector3  operator*(float s) const;             // 0x10004510, 35
};

// 0x100022b0, 31 - unary minus. A **free** function, not a member: it never
// touches ECX and ends in a bare RET, so the caller cleans the stack. That is
// __cdecl, which a member of this class would never be.
Vector3 operator-(const Vector3 &v);

#endif
