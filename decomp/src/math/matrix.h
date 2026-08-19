// matrix.h - the 3x3 matrix class, as far as the matched code constrains it.
//
// Layout is fixed by the offsets the code uses. The 3x3 sits at 0x0c, indexed
// as [ecx + i*4 + 0x0c], [ecx + i*4 + 0x18], [ecx + i*4 + 0x24] - three rows
// twelve bytes apart, so `float m[3][3]` at 0x0c, and an index into it selects
// a **column**.
//
// What sits at 0x00 and 0x30 is not settled. 0x00..0x08 is three floats that
// 0x100023c0 reads and writes alongside the matrix; 0x30..0x38 is three floats
// that the two-vector constructor at 0x10004470 sets to 1.0f, which reads like
// a scale. Both are declared only so the offsets of the 3x3 come out right -
// nothing matched so far touches either, so nothing matched depends on the
// guess.
//
// The name is invented. See decomp/README.md - nothing in these binaries
// carries the original names.

#ifndef MATRIX_H
#define MATRIX_H

#include "vector3.h"

class Matrix
{
public:
    Vector3 origin;    // 0x00  - purpose not established
    float   m[3][3];   // 0x0c
    Vector3 scale;     // 0x30  - set to (1,1,1) by the constructor at 0x10004470

    void    GetColumn(Vector3 &out, int i) const;   // 0x10004540, 33
    Vector3 GetColumnFP(int i) const;               // 0x10004570, 33
    void    SetColumn(int i, const Vector3 &v);     // 0x100045a0, 33
    Vector3 operator*(const Vector3 &v) const;      // 0x100045d0, 82
};

#endif
