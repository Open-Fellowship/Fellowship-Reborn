/* predicates.c - the first two functions this project runs instead of the
 * retail engine's.
 *
 * Both are exported by the retail Fellowship.rfl under exactly these names, and
 * both were reproduced byte for byte from source before being written here:
 *
 *     0x1000d810  IsObjectPortal     16 bytes, 0 relocations, matched
 *     0x1000d820  IsObjectMoveNode   16 bytes, 0 relocations, matched
 *
 * Zero relocations means the whole sixteen bytes were compared with nothing
 * masked, so "matched" is the strong form of the claim: there is no operand
 * that could have differed unnoticed. The matching source is
 * decomp/src/objectdef/predicates.cpp and the manifest entries are in
 * decomp/manifest.tsv.
 *
 * They are the right pair to move first because they are pure. No state, no
 * globals, no calls, one argument in and a boolean out - so if the game behaves
 * differently after this, the cause is the proxy mechanism rather than these
 * two functions, which is exactly the property a first replacement should have.
 *
 * The retail versions return `bool`: the compiled code ends in a byte-sized
 * SETZ rather than the MOV EAX,1 / XOR EAX,EAX pair that an `int` return gives.
 * The exported signature is widened to `int` here because C has no bool in the
 * ABI sense and the host only ever tests the result against zero. That is a
 * deliberate, and the only, departure from the matched source.
 */

#include <windows.h>

#include "proxy.h"

int __cdecl IsObjectPortal(int class_id)
{
    return class_id == OF_CLASS_PORTAL;
}

int __cdecl IsObjectMoveNode(int class_id)
{
    return class_id == OF_CLASS_MOVE_NODE_OBJECT;
}
