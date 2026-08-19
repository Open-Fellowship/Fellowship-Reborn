// vector3.cpp - Fellowship.rfl, the Vector3 class.
//
// Every function here matches the original byte for byte with /O2 /Gy. The
// class declaration, and the two conventions the whole codebase follows, are
// in vector3.h.

#include <math.h>
#include "vector3.h"


// 0x10002230 - copies the three floats with integer MOV, not FLD/FSTP: VC6
// moves a float member as a bit pattern and only involves the FPU when
// arithmetic demands it. Returns `this` in EAX.
Vector3 &Vector3::operator=(const Vector3 &v)
{
    x = v.x;
    y = v.y;
    z = v.z;
    return *this;
}


// 0x10002250 - EAX holds the argument at the RET rather than `this`, so this
// returns void, not the Vector3& that operator-= conventionally returns.
void Vector3::operator-=(const Vector3 &v)
{
    x -= v.x;
    y -= v.y;
    z -= v.z;
}


// 0x10002270 - the scalar is reloaded from the stack for each component
// rather than kept on the FPU stack.
void Vector3::operator*=(float s)
{
    x *= s;
    y *= s;
    z *= s;
}


// 0x10002290 - the mirror of operator-=, and like it returns void.
void Vector3::operator+=(const Vector3 &v)
{
    x += v.x;
    y += v.y;
    z += v.z;
}


// 0x100022d0
Vector3 Vector3::operator+(const Vector3 &v) const
{
    Vector3 r;
    r.z = z + v.z;
    r.y = y + v.y;
    r.x = x + v.x;
    return r;
}


// 0x10002300
Vector3 Vector3::operator-(const Vector3 &v) const
{
    Vector3 r;
    r.z = z - v.z;
    r.y = y - v.y;
    r.x = x - v.x;
    return r;
}


// 0x100022b0 - free function, __cdecl.
Vector3 operator-(const Vector3 &v)
{
    Vector3 r;
    r.z = -v.z;
    r.y = -v.y;
    r.x = -v.x;
    return r;
}


// 0x10002380 - returns a float in ST(0).
float Vector3::dot(const Vector3 &v) const
{
    return x * v.x + y * v.y + z * v.z;
}


// 0x100023a0 - no square root. The three components are pulled onto the FPU
// stack first because each is needed twice.
float Vector3::LengthSquared() const
{
    return x * x + y * y + z * z;
}


// 0x10002330 - the reciprocal is computed once, stored to a stack slot (the
// PUSH ECX at the top is that slot, not a saved register), and reloaded for
// each component. FDIVR against the 1.0f in .rdata is a relocation.
Vector3 Vector3::Normalized() const
{
    float inv = 1.0f / (float)sqrt(x * x + y * y + z * z);
    Vector3 r;
    r.z = z * inv;
    r.y = y * inv;
    r.x = x * inv;
    return r;
}


// 0x100044e0 - division by a scalar is a reciprocal and three multiplies, and
// the reciprocal is kept on the FPU stack and duplicated with FLD ST(0)
// rather than spilled. Stores are in forward order because they go straight
// to members, not through a return buffer.
void Vector3::operator/=(float s)
{
    float inv = 1.0f / s;
    x *= inv;
    y *= inv;
    z *= inv;
}


// 0x10004510
Vector3 Vector3::operator*(float s) const
{
    Vector3 r;
    r.z = z * s;
    r.y = y * s;
    r.x = x * s;
    return r;
}
