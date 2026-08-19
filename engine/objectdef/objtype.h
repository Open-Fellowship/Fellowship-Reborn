/* objtype.h - the engine's ObjType registry, the second of Fellowship.rfl's two.
 *
 * Nineteen records naming the categories a class can belong to. Every ObjectDef's
 * +0x04 field holds one of these ids, so this table is what makes that field
 * readable - and it is the smallest complete system in the engine, eight bytes a
 * record and no behaviour attached.
 *
 * Checked by `python decomp/tools/objdefgen.py --verify`, which compiles the
 * generated table and compares it against the retail image field by field.
 */

#ifndef OF_OBJTYPE_H
#define OF_OBJTYPE_H

typedef struct OF_ObjType {
    unsigned int id;    /* +0x00  1..19, dense, in table order */
    const char  *name;  /* +0x04 */
} OF_ObjType;

/* What GetObjTypeInterface returns the address of. Byte for byte the same shape
 * as the ObjectDef interface, written by an initialiser that is two stores and a
 * return - the engine treats both registries identically. */
typedef struct OF_ObjTypeInterface {
    unsigned int      count;
    const OF_ObjType *table;
} OF_ObjTypeInterface;

extern const OF_ObjTypeInterface g_objTypeInterface;

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(OF_ObjType) == 8, "OF_ObjType must be 8 bytes");
#endif

#endif /* OF_OBJTYPE_H */
