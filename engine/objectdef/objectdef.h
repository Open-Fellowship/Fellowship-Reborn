/* objectdef.h - the engine's ObjectDef registry, as the retail module lays it out.
 *
 * Every field offset here is taken from Fellowship.rfl and is checked by
 * `python decomp/tools/objdefgen.py --verify`, which compiles the generated table
 * and compares it against the retail image field by field. The static asserts
 * below are the cheap half of that: they catch a layout mistake at compile time
 * rather than leaving it to the comparison.
 *
 * Documented in documentation/OBJECT-MODEL.md. Two fields have no established
 * meaning and are carried across verbatim; see the notes on them below.
 */

#ifndef OF_OBJECTDEF_H
#define OF_OBJECTDEF_H

/* A property's default and its constraint are the same four bytes used two ways:
 * a literal for the numeric types, an address for the rest. Spelling that as a
 * union rather than casting through an integer keeps the emitted table a static
 * initialiser and makes which-is-which readable in the generated file. */
typedef union OF_Value {
    unsigned int u;
    const void  *p;
} OF_Value;

typedef struct OF_Property {
    const char  *label;      /* +0x00  the editor's display name */
    const char  *key;        /* +0x04  the serialisation name used in level data */
    unsigned int type;       /* +0x08  base type in the low 12 bits, modifier in the high nibble */
    OF_Value     dflt;       /* +0x0c */
    OF_Value     constraint; /* +0x10  an enum's value names, or the class id a reference accepts */
} OF_Property;

typedef struct OF_PropertyGroup {
    const char        *name;       /* +0x00 */
    unsigned int       count;      /* +0x04 */
    const OF_Property *properties; /* +0x08 */
} OF_PropertyGroup;

typedef struct OF_ObjectDef {
    unsigned int id;         /* +0x00  0x00010001 upward, dense, in table order */
    unsigned int objtype;    /* +0x04  one of the nineteen ObjType categories */
    unsigned int unknown_08; /* +0x08  MEANING UNESTABLISHED. 0 in 189 of 397; range 0-135 */
    unsigned int flags;      /* +0x0c  MEANING UNESTABLISHED. Values look like 0x?00004?? */
    const char  *name;       /* +0x10 */
    unsigned int properties; /* +0x14  total across all groups, including inherited */
    unsigned int groups;     /* +0x18 */
    const OF_PropertyGroup *const *group_list; /* +0x1c  pointers, and groups are SHARED */
} OF_ObjectDef;

/* What GetObjectDefInterface returns the address of. The retail module writes
 * these two fields once, from an initialiser that is two stores and a return. */
typedef struct OF_ObjectDefInterface {
    unsigned int        count;
    const OF_ObjectDef *table;
} OF_ObjectDefInterface;

extern const OF_ObjectDefInterface g_objectDefInterface;
extern const char of_objectdef_unset[];

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(OF_Property) == 20, "OF_Property must be 20 bytes");
_Static_assert(sizeof(OF_PropertyGroup) == 12, "OF_PropertyGroup must be 12 bytes");
_Static_assert(sizeof(OF_ObjectDef) == 32, "OF_ObjectDef must be 32 bytes");
_Static_assert(sizeof(void *) == 4, "the retail module is 32-bit; so is this table");
#endif

#endif /* OF_OBJECTDEF_H */
