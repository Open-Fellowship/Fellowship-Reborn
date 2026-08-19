// matrix.cpp - Fellowship.rfl, the 3x3 matrix class. Declaration in matrix.h.

#include "matrix.h"


// 0x10004540 - this one does NOT return by value. It writes through a
// reference, and that is what the code generation gives away.
//
// The original moves the three floats with integer MOV and never touches the
// FPU. Writing to a named local and returning it will not produce that, no
// matter how the source is arranged - a local is the return buffer under RVO,
// the compiler knows nothing else can alias it, and it keeps the values in FPU
// registers. Writing through a reference it cannot prove is unaliased forces
// memory-to-memory moves, and for a plain copy those are integer moves.
//
// So the signature is recoverable from the instruction selection alone. The
// stack layout is the same either way - a hidden return pointer at [esp+4] and
// the index at [esp+8] is indistinguishable from an out-parameter at [esp+4]
// and an index at [esp+8] - so the bytes are the only thing that separates
// them. Compare GetColumnFP below, which does return by value.
void Matrix::GetColumn(Vector3 &out, int i) const
{
    out.x = m[0][i];
    out.y = m[1][i];
    out.z = m[2][i];
}


// 0x10004570 - the same column, read through the FPU instead, and it pays the
// FXCH that vector3.h describes: the components are pushed x, y, z, leaving z
// on top, so an FXCH ST(2) is needed before they can be stored x, y, z.
//
// That is the signature of returning a constructed temporary rather than a
// named local. Two functions doing the same job, written two different ways -
// evidence for the convention rather than against it.
Vector3 Matrix::GetColumnFP(int i) const
{
    return Vector3(m[0][i], m[1][i], m[2][i]);
}


// 0x100045a0 - integer MOVs again. Arguments are (index, vector): the index
// is at [esp+4] and the vector pointer at [esp+8].
void Matrix::SetColumn(int i, const Vector3 &v)
{
    m[0][i] = v.x;
    m[1][i] = v.y;
    m[2][i] = v.z;
}


// 0x100045d0 - NOT MATCHED, but down to **4 of 82 bytes**, and the four are
// almost certainly out of reach from source. See the note below.
//
// Each row accumulates: the y term, then += the z term, then += the x term.
// Writing it as a single expression does not work. `a + b + c` lets the
// compiler reassociate - Vector3::dot proves it does, and there it happens to
// land on the grouping the original uses. Here it lands elsewhere, and no
// arrangement of one expression reaches it. Separate `+=` statements are
// sequence points, so the order stops being the compiler's to choose.
Vector3 Matrix::operator*(const Vector3 &v) const
{
    Vector3 r;
    r.z = m[2][1] * v.y;  r.z += m[2][2] * v.z;  r.z += m[2][0] * v.x;
    r.y = m[1][1] * v.y;  r.y += m[1][2] * v.z;  r.y += m[1][0] * v.x;
    r.x = m[0][1] * v.y;  r.x += m[0][2] * v.z;  r.x += m[0][0] * v.x;
    return r;
}

// The four remaining bytes are the last multiply of the last row:
//
//     orig   d9 00 d8 49 0c     fld [eax] ; fmul [ecx+0xc]      v.x first
//     ours   d9 41 0c d8 08     fld [ecx+0xc] ; fmul [eax]      m[0][0] first
//
// Both compute the same product. The original loads v.x through EAX one last
// time immediately before EAX is reloaded with the return-buffer pointer;
// ours loads the matrix element first and reads v.x as the multiply's memory
// operand. It is a scheduling choice around a register about to be clobbered,
// and it appears only in the final row, where that reload happens.
//
// Source does not appear to reach it. VC6 normalises the operand order of a
// commutative multiply: writing `v.x * m[0][0]` produces **byte-identical**
// output to `m[0][0] * v.x`, verified by diffing the two objects rather than
// by comparing mismatch counts. So the choice is the compiler's, not the
// source's, and the only question is what nudges it.
//
// Ruled out, all still exactly 4 of 82 unless noted:
//
//   source shape   v-first throughout; v-first on that term alone; a
//                  `const float *` walked over v; the same over m; per-row
//                  float locals; one reused accumulator local; per-term
//                  temporaries; two statements per row instead of three (16);
//                  a single expression for the last row only (8)
//   structure      an out-parameter instead of a by-value return (72 - this
//                  is definitely a by-value return); `Vector3 row[3]` with
//                  row[i].dot(v) (68 of 80 - wrong length, so wrong shape);
//                  a loop over the three rows, which VC6 does not unroll
//   switches       /G3 /G4 /G5 /G6 /GB - scheduling targets, no effect;
//                  /Ox, /Ob2, all identical; /Ob0 (72), /Oy- (81), /O1 (13)
//   toolchain      the stock CD back end, C2.DLL 12.00.8168, instead of the
//                  Processor Pack's 13.00.9044 - identical 4 bytes. The rfl's
//                  Rich header lists C++ objects from four different builds
//                  (9044, 8447, 8966, 8047), so this file's object plausibly
//                  came from a different one, but the two available back ends
//                  do not distinguish it
//
// What has not been tried: the C++ **front end**. C1XX.DLL here is 12.00.8168
// from the CD, and SP5 ships another in VS6sp55.cab, inside a spanned cabinet
// that `expand` will not walk and cannot even list. Extracting it needs a CAB
// reader rather than the shell. That is the last uncontrolled variable, and it
// is a thin lead - fifteen other functions match with this front end.
