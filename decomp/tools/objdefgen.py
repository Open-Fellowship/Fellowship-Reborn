#!/usr/bin/env python3
"""
objdefgen.py - emit the engine's four registries as C, and prove they match.

`classdump.py` reads them out of Fellowship.rfl. This writes them back out as
compilable C so that engine/'s proxy can serve them instead of forwarding to the
retail module - the first systems that are ours rather than the 2002 DLL's.

  python objdefgen.py --emit <dir>     write the tables and their map
  python objdefgen.py --verify <dir>   compile them and compare against the image

Four registries, because the engine publishes four and they are one mechanism:

  ObjectDef  397 classes, 494 property groups, 4,262 properties  objectdef_table.c
  LandType   192 records, {name, flags}                          landtype_table.c
  Message     54 records, {name, id}                             message_table.c
  ObjType     19 categories, {id, name}                          objtype_table.c

Each is published by a function that is two `mov dword [global], imm32` stores
and a return, registered in the C++ static-initialiser table at 0x100fd004 and
run at DLL load; the matching `Get*Interface` export returns the address of the
global pair. Scanning .text for that shape finds exactly four, which is also
exactly the number of exports that return a {count, table} pair - so the set is
complete rather than as far as anyone looked.

An ObjectDef's +0x04 holds an ObjType id, so the smallest table is what makes the
largest one's category field readable.

## Why this is worth doing carefully

The registry is static data behind a six-byte getter: an initialiser stores a
count and a base pointer into two globals, `GetObjectDefInterface` returns their
address, and nothing else in the engine touches them. So a faithful table plus a
getter IS the implementation - there is no behaviour to reproduce.

That makes the whole thing verifiable the way a decompiled function is. `--verify`
compiles the generated C and compares the emitted bytes against the retail image
field by field, with pointer fields masked, exactly as matchtool.py does for code.
Anything that is not a pointer - ids, ObjTypes, counts, type codes, defaults,
constraints, flags - must match verbatim, and the pointers are then followed and
compared in turn.

Two fields have no established meaning: ObjectDef+0x08 and the flags at +0x0c.
They are emitted verbatim and the comparison proves they were carried across. A
field does not need to be understood to be reproduced correctly, and pretending
otherwise would be the only way to get it wrong.

## Sharing is structure, not an optimisation

Property groups are shared between classes: Player, NPC, Nazgul, Balrog and the
rest all point at the *same* three base-class group records. Emitting each group
once and referencing it reproduces the original's structure; copying them into
each class would produce a table that behaves the same and is not the same. This
generator keys every group and property array on its address in the retail image,
so the sharing comes out of the data rather than out of a guess about it.
"""

import io
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classdump  # noqa: E402
import matchtool  # noqa: E402

REPO = os.path.dirname(os.path.dirname(HERE))

# Which fields hold an address is NOT a fixed list. A property's default and its
# constraint are a pointer or a literal depending on the type code, so the offset
# alone cannot say. The object file can: a pointer field is emitted as zero plus a
# relocation, so the relocation records are the authority on which four bytes are
# an address.
#
# Masking those is the same move matchtool.py makes for code - the retail table's
# pointers are its own addresses and ours are ours, and requiring them to agree
# would be requiring two linkers to lay memory out identically.
#
# The converse is checked too, and it is the part that catches a real bug: if the
# retail field holds an address and ours has no relocation there, we emitted a
# literal where a pointer belongs.


def is_pointer_default(img, type_code, value):
    """Whether a property's default or constraint holds an address.

    The resource types default to a pointer into the zero-filled tail of .data -
    an empty string, meaning unset - and enums point their constraint at a
    comma-separated list of value names. Everything else is a literal: a float
    bit pattern, an integer, a colour, a message id, a class id.
    """
    base = type_code & 0xFFF
    if base in (0x03, 0x04, 0x05, 0x09, 0x0A, 0x0B, 0x0E, 0x10, 0x12):
        return True
    return img.base <= value < img.base + 0x200000 and img.off(value) is not None


class Table(object):
    """The retail registry, read with every address kept."""

    def __init__(self, path=None):
        self.img = classdump.Image(path or classdump.RFL)
        d = self.img.d
        self.classes = []
        self.groups = {}        # group VA   -> {name, count, props_va, props}
        self.proparrays = {}    # props VA   -> [property tuples]
        for i in range(classdump.TABLE_N):
            va = classdump.TABLE_VA + i * 32
            rec = struct.unpack_from("<8I", d, self.img.off(va))
            cid, objtype, unknown8, flags, nameptr, nprops, ngroups, gptr = rec
            name = self.img.string(nameptr)
            if name is None or ngroups > 64 or (ngroups and self.img.off(gptr) is None):
                continue
            gvas = [self.img.u32(gptr + g * 4) for g in range(ngroups)]
            for gva in gvas:
                if gva in self.groups:
                    continue
                gname, gcount, garr = struct.unpack_from("<III", d, self.img.off(gva))
                self.groups[gva] = {
                    "name_va": gname, "name": self.img.string(gname),
                    "count": gcount, "props_va": garr,
                }
                # Keep the LONGEST reading of a shared array, not the first.
                #
                # Two groups have count 0 and point at an array another group
                # also uses with a real count - "Ranged Targeting Position" and
                # "Tolkien NPC" share with "Initial State" and "One Ring
                # Changes". Taking the first reading meant a zero-count group
                # could store an empty array and shadow the real one, dropping
                # four properties from the table without a word. It is only
                # order that decides which wins, so this was one dict iteration
                # away from being invisible.
                if len(self.proparrays.get(garr, ())) < gcount:
                    self.proparrays[garr] = [
                        struct.unpack_from("<5I", d, self.img.off(garr + k * 20))
                        for k in range(gcount)
                    ]
                elif garr not in self.proparrays:
                    self.proparrays[garr] = []
            self.classes.append({
                "va": va, "id": cid, "objtype": objtype, "unknown8": unknown8,
                "flags": flags, "name": name, "name_va": nameptr,
                "props": nprops, "ngroups": ngroups, "gptr": gptr, "gvas": gvas,
            })

        # The ObjType registry. Nineteen records of {id, char*}, published by an
        # initialiser at 0x1004c298 that is two stores and a return, exactly like
        # the ObjectDef one. Read unconditionally rather than filtered: the ids
        # are dense 1..19 and the byte after the last record is the start of a
        # string, so there is no boundary question to be cautious about.
        self.objtypes = [
            struct.unpack_from("<II", d, self.img.off(classdump.OBJTYPE_VA + i * 8))
            for i in range(classdump.OBJTYPE_N)
        ]

        # LandType and Message: the same eight bytes with the fields the other
        # way round, {char *name, u32 value}.
        self.named = {}
        for key, va, n in (("landtype", classdump.LANDTYPE_VA, classdump.LANDTYPE_N),
                           ("message", classdump.MESSAGE_VA, classdump.MESSAGE_N)):
            self.named[key] = [
                struct.unpack_from("<II", d, self.img.off(va + i * 8))
                for i in range(n)
            ]


HEXDIGITS = "0123456789abcdefABCDEF"


def rawstring(img, va):
    """The bytes at `va` up to the NUL, decoded verbatim.

    Image.string() refuses anything outside printable ASCII, which is the right
    caution when guessing whether an address even IS a string. Here we already
    know it is, and eight property labels carry a 0x85 byte - "Chest is
    Initially...", "Opens From..." - so refusing them meant emitting an empty
    label and losing the text. latin-1 round-trips every byte, which is what a
    faithful copy needs.
    """
    o = img.off(va)
    if o is None:
        return None
    end = img.d.find(b"\0", o)
    if end < 0:
        return None
    return img.d[o:end].decode("latin-1")


def cstr(s):
    """A C string literal that survives round-tripping.

    C's hex escape is greedy - it eats every following hex digit - so a label
    like "Opens From\x85" with more text after it would silently become one
    character. Closing the literal and reopening after any escape whose
    successor is a hex digit is the standard way out; adjacent literals
    concatenate, so the value is unchanged.
    """
    out = ['"']
    for i, ch in enumerate(s):
        code = ord(ch)
        if ch == '"' or ch == "\\":
            out.append("\\" + ch)
        elif 0x20 <= code <= 0x7E:
            out.append(ch)
        else:
            out.append("\\x%02x" % code)
            if i + 1 < len(s) and s[i + 1] in HEXDIGITS:
                out.append('" "')
    out.append('"')
    return "".join(out)


def value(img, type_code, v, kind):
    """One default or constraint, as a C union initialiser."""
    if v == 0:
        return "{ 0 }"
    if kind == "constraint" and (type_code & 0xFFF) == 0x07:
        s = rawstring(img, v)
        return "{ .p = %s }" % cstr(s) if s is not None else "{ .u = %#010xu }" % v
    if kind == "default" and is_pointer_default(img, type_code, v):
        s = rawstring(img, v)
        if s:
            return "{ .p = %s }" % cstr(s)
        # A pointer into the zero-filled tail of .data: the resource types' "unset".
        # One shared empty string reproduces that without inventing a target.
        return "{ .p = of_objectdef_unset }"
    return "{ .u = %#010xu }" % v


def emit(outdir):
    t = Table()
    img = t.img
    lines = []
    w = lines.append
    w("/* Generated by decomp/tools/objdefgen.py from Fellowship.rfl. Do not edit.")
    w(" *")
    w(" * The engine's ObjectDef registry: %d classes, %d property groups,"
      % (len(t.classes), len(t.groups)))
    w(" * %d property records. Groups shared between classes are emitted once and"
      % sum(len(p) for p in t.proparrays.values()))
    w(" * referenced, which is how the retail table is built - Player, NPC, Nazgul and")
    w(" * the rest genuinely point at the same base-class groups.")
    w(" *")
    w(" * Verify with: python decomp/tools/objdefgen.py --verify <dir>")
    w(" */")
    w("")
    w('#include "objectdef.h"')
    w("")
    w("/* Resource properties default to a pointer into the zero-filled tail of the")
    w(" * retail .data - an empty string, meaning unset. */")
    w('const char of_objectdef_unset[] = "";')
    w("")

    for va in sorted(t.proparrays):
        props = t.proparrays[va]
        if not props:
            continue
        w("static const OF_Property g_props_%08x[] = {" % va)
        for label, key, code, dflt, extra in props:
            w("    { %s, %s, %#010xu, %s, %s },"
              % (cstr(rawstring(img, label) or ""), cstr(rawstring(img, key) or ""), code,
                 value(img, code, dflt, "default"), value(img, code, extra, "constraint")))
        w("};")
    w("")

    for va in sorted(t.groups):
        g = t.groups[va]
        arr = ("g_props_%08x" % g["props_va"]) if t.proparrays.get(g["props_va"]) else "0"
        w("static const OF_PropertyGroup g_group_%08x = { %s, %du, %s };"
          % (va, cstr(rawstring(img, g["name_va"]) or ""), g["count"], arr))
    w("")

    seen = set()
    for c in t.classes:
        if c["gptr"] in seen or not c["gvas"]:
            continue
        seen.add(c["gptr"])
        w("static const OF_PropertyGroup *const g_grouplist_%08x[] = {" % c["gptr"])
        for gva in c["gvas"]:
            w("    &g_group_%08x," % gva)
        w("};")
    w("")

    w("static const OF_ObjectDef g_objectdefs[] = {")
    for c in t.classes:
        gl = ("g_grouplist_%08x" % c["gptr"]) if c["gvas"] else "0"
        w("    { %#07xu, %du, %du, %#010xu, %s, %du, %du, %s },"
          % (c["id"], c["objtype"], c["unknown8"], c["flags"], cstr(c["name"]),
             c["props"], c["ngroups"], gl))
    w("};")
    w("")
    w("const OF_ObjectDefInterface g_objectDefInterface = {")
    w("    %du," % len(t.classes))
    w("    g_objectdefs,")
    w("};")
    w("")

    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "objectdef_table.c")
    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))

    tpath = emit_objtypes(t, outdir)
    npaths = [emit_named(t, outdir, k, s, i, d)
              for k, s, i, d in NAMED]

    # The map is what lets --verify line our symbols up with the retail addresses
    # they came from. Without it the comparison would have to guess. The last
    # column names the translation unit the symbol is in, because there is now
    # more than one.
    mp = os.path.join(outdir, "objectdef_table.map")
    with io.open(mp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("# symbol\tretail VA\tkind\tcount\tsource\n")
        fh.write("g_objectdefs\t%#x\tobjectdef\t%d\tobjectdef_table\n"
                 % (classdump.TABLE_VA, len(t.classes)))
        for va in sorted(t.groups):
            fh.write("g_group_%08x\t%#x\tgroup\t1\tobjectdef_table\n" % (va, va))
        for va in sorted(t.proparrays):
            if t.proparrays[va]:
                fh.write("g_props_%08x\t%#x\tproperty\t%d\tobjectdef_table\n"
                         % (va, va, len(t.proparrays[va])))
        fh.write("g_objtypes\t%#x\tobjtype\t%d\tobjtype_table\n"
                 % (classdump.OBJTYPE_VA, len(t.objtypes)))
        for key, symbol, _, _ in NAMED:
            fh.write("%s\t%#x\tnamed\t%d\t%s_table\n"
                     % (symbol, NAMED_VA[key], len(t.named[key]), key))
    print("wrote %s" % path)
    print("  %d classes, %d groups, %d property arrays"
          % (len(t.classes), len(t.groups), len(t.proparrays)))
    print("wrote %s" % tpath)
    print("  %d object types" % len(t.objtypes))
    for p, (key, _, _, _) in zip(npaths, NAMED):
        print("wrote %s" % p)
        print("  %d records" % len(t.named[key]))
    return path


# The two {name, value} registries: key, array symbol, interface symbol, and
# what the value field holds. Kept as data rather than two near-identical
# emitters, because the only thing that differs between them is the text.
NAMED = [
    ("landtype", "g_landtypes", "g_landTypeInterface",
     "a land type's flags - 0 for Normal, 0x00ffff00 for everything else"),
    ("message", "g_messages", "g_messageInterface",
     "the message id the engine dispatches on, sparse above 0x100"),
]
NAMED_VA = {"landtype": classdump.LANDTYPE_VA, "message": classdump.MESSAGE_VA}


def emit_named(t, outdir, key, symbol, iface, what):
    img = t.img
    lines = []
    w = lines.append
    w("/* Generated by decomp/tools/objdefgen.py from Fellowship.rfl. Do not edit.")
    w(" *")
    w(" * The engine's %s registry: %d records of {name, value}, where the value is"
      % (key, len(t.named[key])))
    w(" * %s." % what)
    w(" *")
    w(" * Verify with: python decomp/tools/objdefgen.py --verify <dir>")
    w(" */")
    w("")
    w('#include "registry.h"')
    w("")
    w("static const OF_NamedValue %s[] = {" % symbol)
    for nameptr, value in t.named[key]:
        w("    { %s, %#010xu }," % (cstr(rawstring(img, nameptr) or ""), value))
    w("};")
    w("")
    w("const OF_NamedValueInterface %s = {" % iface)
    w("    %du," % len(t.named[key]))
    w("    %s," % symbol)
    w("};")
    w("")

    path = os.path.join(outdir, "%s_table.c" % key)
    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    return path


def emit_objtypes(t, outdir):
    """The ObjType registry: nineteen records, no sharing, nothing to decide."""
    img = t.img
    lines = []
    w = lines.append
    w("/* Generated by decomp/tools/objdefgen.py from Fellowship.rfl. Do not edit.")
    w(" *")
    w(" * The engine's ObjType registry: %d categories, {id, name}. Every ObjectDef's"
      % len(t.objtypes))
    w(" * +0x04 field holds one of these ids.")
    w(" *")
    w(" * Verify with: python decomp/tools/objdefgen.py --verify <dir>")
    w(" */")
    w("")
    w('#include "objtype.h"')
    w("")
    w("static const OF_ObjType g_objtypes[] = {")
    for tid, nameptr in t.objtypes:
        w("    { %du, %s }," % (tid, cstr(rawstring(img, nameptr) or "")))
    w("};")
    w("")
    w("const OF_ObjTypeInterface g_objTypeInterface = {")
    w("    %du," % len(t.objtypes))
    w("    g_objtypes,")
    w("};")
    w("")

    path = os.path.join(outdir, "objtype_table.c")
    io.open(path, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    return path


STRIDE = {"objectdef": 32, "group": 12, "property": 20, "objtype": 8,
          "named": 8}


def verify(outdir):
    """Compile the generated tables and compare them against the retail image.

    A mismatch here is a generator bug, and the point of comparing against the
    image rather than against classdump's reading is that a shared bug in the
    reader would otherwise be invisible.
    """
    cl = which_cl()
    units = {}
    for unit in ("objectdef_table", "objtype_table",
                 "landtype_table", "message_table"):
        src = os.path.join(outdir, unit + ".c")
        if not os.path.exists(src):
            raise SystemExit("no %s.c in %s - run --emit first" % (unit, outdir))
        obj = os.path.join(outdir, unit + ".obj")
        r = subprocess.run([cl, "/nologo", "/c", "/O2", "/I", outdir, src, "/Fo" + obj],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(r.stdout + r.stderr)
            raise SystemExit("%s.c does not compile" % unit)
        units[unit] = open(obj, "rb").read()

    img = classdump.Image(classdump.RFL)
    rows = [l.split("\t") for l in io.open(os.path.join(outdir, "objectdef_table.map"),
                                           encoding="utf-8").read().splitlines()
            if l and not l.startswith("#")]

    checked = mismatched = pointers = 0
    for symbol, va_text, kind, count_text, unit in rows:
        data = units[unit]
        va, count = int(va_text, 16), int(count_text)
        size = STRIDE[kind] * count
        try:
            ours, relocs, _, _ = matchtool.coff_function(data, symbol)
        except Exception as exc:
            print("  CANNOT READ %s: %s" % (symbol, exc))
            mismatched += 1
            continue
        ours = ours[:size]
        pointer_at = set(o for o, _ in relocs if o < size)
        _, _, orig = matchtool.image_bytes(open(classdump.RFL, "rb").read(), va, size)
        for i in range(count):
            off = i * STRIDE[kind]
            for f in range(0, STRIDE[kind], 4):
                a = orig[off + f:off + f + 4]
                b = ours[off + f:off + f + 4]
                retail_is_addr = (img.base <= struct.unpack("<I", a)[0]
                                  < img.base + 0x200000)
                if off + f in pointer_at:
                    pointers += 1
                    if not retail_is_addr:
                        mismatched += 1
                        if mismatched <= 8:
                            print("  %s[%d]+%#x: we emit a pointer, retail has %s"
                                  % (symbol, i, f, a.hex()))
                    continue
                if retail_is_addr and img.off(struct.unpack("<I", a)[0]) is not None:
                    mismatched += 1
                    if mismatched <= 8:
                        print("  %s[%d]+%#x: retail holds an address (%s), we emit a literal"
                              % (symbol, i, f, a.hex()))
                    continue
                checked += 1
                if a != b:
                    mismatched += 1
                    if mismatched <= 8:
                        print("  MISMATCH %s[%d]+%#x: retail %s ours %s"
                              % (symbol, i, f, a.hex(), b.hex()))
    print("\n  %d non-pointer fields compared against the retail image" % checked)
    if mismatched:
        print("  %d DIFFER" % mismatched)
        return 1
    print("  all identical")
    return 0


def which_cl():
    for root in (os.environ.get("VCToolsInstallDir"), ):
        if root and os.path.exists(os.path.join(root, "bin", "Hostx64", "x86", "cl.exe")):
            return os.path.join(root, "bin", "Hostx64", "x86", "cl.exe")
    import glob
    pat = r"C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x86\cl.exe"
    hits = sorted(glob.glob(pat))
    if hits:
        return hits[-1]
    raise SystemExit("no 32-bit cl.exe found; run from a VS developer command prompt")


def main(argv):
    if len(argv) >= 3 and argv[1] == "--emit":
        emit(argv[2])
        return 0
    if len(argv) >= 3 and argv[1] == "--verify":
        return verify(argv[2])
    print(__doc__.strip().splitlines()[2])
    print("  --emit <dir>    write the table")
    print("  --verify <dir>  compile it and compare against the retail image")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
