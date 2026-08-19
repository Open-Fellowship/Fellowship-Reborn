#!/usr/bin/env python3
"""
classdump.py - read the Riot Engine class registry out of Fellowship.rfl.

The engine registers every level object type by name, with a full property
schema attached: display label, serialisation key, type, default value and
constraint. That table is plain data in .data, so this needs no decompilation
and no Ghidra - it just parses the image.

  python classdump.py                 summary and the class index
  python classdump.py Player          one class in full
  python classdump.py --json out.json every class, machine readable
  python classdump.py --tables <dir>  generated markdown tables for documentation/

RFL points at a PRISTINE Fellowship.rfl; see documentation/TOOLCHAIN.md.

## The format

A 32-byte record per class, in one contiguous table:

  +0x00  id            0x00010001 upward, dense, in table order
  +0x04  unknown       small int
  +0x08  unknown       small int
  +0x0c  flags         0x?00004?? - meaning not established
  +0x10  name          char*
  +0x14  properties    total across all groups, including inherited
  +0x18  groups        number of property groups
  +0x1c  group array   pointer to `groups` pointers

The id precedes the name rather than following it, which matters: read the
record the other way round and every class reference resolves one class off.
The give-away is a property named for its own target - `Footsteps` must point
at the class named `Footsteps`, `RegionList` at `Map Region`, `HitReactions` at
`Hit Reaction`, `WaveBumps` at `Wave Bump`. Only this boundary satisfies all
four.

Each group is 12 bytes - name, count, pointer to `count` property records - and
each property record is 20 bytes:

  +0x00  label         char*, the editor's display name
  +0x04  key           char*, the serialisation name used in level data
  +0x08  type          see TYPES below
  +0x0c  default       interpreted per type
  +0x10  constraint    enum value list, or the class id a reference accepts

There is NO member offset in a property record. The schema names the engine's
data model; it does not describe the C++ object layout, and nothing here says
whether a property is a real member or an entry in a keyed bag.
"""

import json
import os
import struct
import sys

RFL = os.environ.get(
    "RFL", r"K:\OPEN FELLOWSHIP\reference\Fellowship.rfl")

# Where the table starts, and how many records. Not guessed - the engine
# publishes both. The initialiser at 0x1004c210 is exactly two stores:
#
#   mov dword [0x10132874], 0x18d          the record count, 397
#   mov dword [0x10132878], 0x1010f0a0     the table base
#
# and the getter at 0x1004c230 returns the address of that pair. The base it
# names is the same boundary the reference semantics force, which is what
# settles the id-before-name question independently of any reading of the data.
TABLE_VA = 0x1010F0A0
TABLE_N = 0x18D

# The ObjType table, the engine's second registry, published by GetObjTypeInterface
# the same way: an initialiser stores the count and the base into a pair of globals.
# Records are 8 bytes, {id, name}, and the id is what an ObjectDef's +0x04 field
# holds - so every class names one of these nineteen categories.
#
# Established by mapping every class's +0x04 through it: all 397 resolve, type 3
# "Player" holds exactly the Player class, and type 14 "Light Source" holds exactly
# the four classes the exported IsObjectLight predicate tests.
OBJTYPE_VA = 0x10129AA0
OBJTYPE_N = 0x13

# The engine's other two registries, published by the same shape - a function
# that is two `mov dword [global], imm32` stores and a return, registered in the
# C++ static-initialiser table at 0x100fd004 and run at DLL load. Scanning .text
# for that shape finds exactly four, which is also exactly the number of
# `Get*Interface` exports that return a {count, table} pair:
#
#   0x10048680  LandType   192 records at 0x100ffe70
#   0x1004c210  ObjectDef  397 records at 0x1010f0a0
#   0x1004c250  Message     54 records at 0x10112240
#   0x1004c290  ObjType     19 records at 0x10129aa0
#
# LandType and Message records are {char *name, u32 value} - the reverse field
# order from ObjType's {id, char *name}, which is worth stating because the two
# are otherwise identical in size and shape. Both tables end where the string
# data they point into begins.
LANDTYPE_VA = 0x100FFE70
LANDTYPE_N = 192
MESSAGE_VA = 0x10112240
MESSAGE_N = 54

# Base type, the low 12 bits. Named where the evidence is unambiguous: floats
# hold float bit patterns in the default, colours hold 0x00RRGGBB, channels
# default to -1, enums carry their value names in the constraint, and the
# resource types all default to the same zero-filled global, i.e. "unset".
TYPES = {
    0x001: "int",
    0x002: "float",
    0x003: "object reference",
    0x004: "model",
    0x005: "sound",
    0x007: "enum",
    0x008: "channel",
    0x009: "animation",
    0x00A: "string",
    0x00B: "sequence",
    0x00E: "texture",
    0x00F: "colour",
    0x010: "movie",
    0x011: "message",
    0x012: "wave",
    0x013: "object link",
}

# The high nibble. Only 1 is established - every property carrying it is named
# as a plural or a list. The others occur on references and presumably say how
# the reference is held; that is not established, so they are reported raw.
MODIFIERS = {0x0: "", 0x1: "list of "}


class Image(object):
    def __init__(self, path):
        self.d = open(path, "rb").read()
        d = self.d
        e = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, e + 6)[0]
        optsz = struct.unpack_from("<H", d, e + 20)[0]
        self.base = struct.unpack_from("<I", d, e + 24 + 28)[0]
        self.secs = []
        for i in range(nsec):
            o = e + 24 + optsz + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode()
            vsz, va, rsz, ptr = struct.unpack_from("<IIII", d, o + 8)
            self.secs.append((name, self.base + va, vsz, ptr, rsz))

    def off(self, va):
        """File offset for a VA, or None if it lands in uninitialised data."""
        for _, sva, vsz, ptr, rsz in self.secs:
            if sva <= va < sva + vsz and (va - sva) < rsz:
                return ptr + (va - sva)
        return None

    def u32(self, va):
        return struct.unpack_from("<I", self.d, self.off(va))[0]

    def string(self, va, limit=4096):
        o = self.off(va)
        if o is None:
            return None
        end = self.d.find(b"\0", o)
        if end < 0 or end - o > limit:
            return None
        s = self.d[o:end]
        return s.decode("latin-1") if all(0x20 <= c <= 0x7E for c in s) else None


def type_name(code):
    base = TYPES.get(code & 0xFFF)
    mod = code >> 12
    if base is None:
        return "type %#x" % code
    prefix = MODIFIERS.get(mod)
    if prefix is None:
        return "%s (modifier %d)" % (base, mod)
    return prefix + base


def default_text(img, code, value):
    base = code & 0xFFF
    if base == 0x002:
        f = struct.unpack("<f", struct.pack("<I", value))[0]
        return repr(round(f, 6))
    if base == 0x00F:
        return "#%06X" % (value & 0xFFFFFF)
    if base == 0x008:
        return "none" if value == 0xFFFFFFFF else str(value)
    if base in (0x001, 0x011, 0x013):
        return str(struct.unpack("<i", struct.pack("<I", value))[0])
    if base == 0x007:
        return str(value)
    s = img.string(value)
    if s is not None:
        return '"%s"' % s
    if img.off(value) is None:
        return ""          # points into the zero-filled tail: unset
    return "%#x" % value


def read_objtypes(img):
    """{id: name} for the nineteen object categories."""
    out = {}
    for i in range(OBJTYPE_N):
        tid, nameptr = struct.unpack_from("<II", img.d, img.off(OBJTYPE_VA + i * 8))
        out[tid] = img.string(nameptr)
    return out


def read(path=RFL):
    img = Image(path)
    classes = []
    for i in range(TABLE_N):
        va = TABLE_VA + i * 32
        cid, a, b, flags, nptr, nprops, ngroups, gptr = struct.unpack_from(
            "<8I", img.d, img.off(va))
        name = img.string(nptr)
        if name is None or ngroups > 64 or (ngroups and img.off(gptr) is None):
            continue
        groups = []
        for g in range(ngroups):
            gp = img.u32(gptr + g * 4)
            gname, gcount, garr = struct.unpack_from("<III", img.d, img.off(gp))
            props = []
            for k in range(gcount):
                label, key, code, dflt, extra = struct.unpack_from(
                    "<5I", img.d, img.off(garr + k * 20))
                props.append({
                    "label": img.string(label),
                    "key": img.string(key),
                    "type": type_name(code),
                    "type_code": code,
                    "default": default_text(img, code, dflt),
                    "constraint": extra,
                })
            groups.append({"name": img.string(gname), "properties": props})
        classes.append({
            "id": cid, "name": name, "flags": flags,
            "property_count": nprops, "objtype": a, "unknown_08": b,
            "groups": groups,
        })
    types = read_objtypes(img)
    for c in classes:
        c["objtype_name"] = types.get(c["objtype"])

    # Resolve reference constraints now that every id is known.
    by_id = dict((c["id"], c["name"]) for c in classes)
    for c in classes:
        for g in c["groups"]:
            for p in g["properties"]:
                x = p.pop("constraint")
                if (p["type_code"] & 0xFFF) == 0x003:
                    p["accepts"] = by_id.get(x, "%#x" % x)
                elif (p["type_code"] & 0xFFF) == 0x007:
                    s = img.string(x)
                    p["values"] = s.split(",") if s else []
                elif (p["type_code"] & 0xFFF) == 0x013 and x:
                    p["accepts"] = by_id.get(x, "%#x" % x)
    return img, classes


def md_escape(s):
    return (s or "").replace("|", "&#124;")


def write_tables(classes, outdir):
    """Emit the generated halves of the object-model documents.

    Tables only - the prose around them is written by hand and lives in the
    documents themselves. Keeping the two apart means a regenerated table never
    silently overwrites something a person wrote.
    """
    live = [c for c in classes if c["name"] != "Don't Use"]

    with open(os.path.join(outdir, "class-index.md"), "w") as fh:
        print("| id | class | properties | groups | flags |", file=fh)
        print("|---|---|---:|---:|---|", file=fh)
        for c in sorted(live, key=lambda c: c["id"]):
            print("| `%#07x` | %s | %d | %d | `%#010x` |"
                  % (c["id"], md_escape(c["name"]), c["property_count"],
                     len(c["groups"]), c["flags"]), file=fh)

    for c in classes:
        if c["name"] != "Player":
            continue
        with open(os.path.join(outdir, "player-properties.md"), "w") as fh:
            for g in c["groups"]:
                print("### %s" % md_escape(g["name"]), file=fh)
                print("", file=fh)
                print("| property | key | type | default | |", file=fh)
                print("|---|---|---|---|---|", file=fh)
                for p in g["properties"]:
                    note = ""
                    if p.get("values"):
                        note = "`%s`" % "` `".join(p["values"])
                    elif p.get("accepts"):
                        note = "-> %s" % md_escape(str(p["accepts"]))
                    print("| %s | `%s` | %s | %s | %s |"
                          % (md_escape(p["label"]), p["key"] or "",
                             md_escape(p["type"]),
                             md_escape(p["default"]) or "-", note), file=fh)
                print("", file=fh)


def main(argv):
    args = argv[1:]
    if args and args[0] == "--tables":
        _, classes = read()
        write_tables(classes, args[1])
        print("wrote class-index.md and player-properties.md to %s" % args[1])
        return 0
    if args and args[0] == "--json":
        _, classes = read()
        with open(args[1], "w") as fh:
            json.dump(classes, fh, indent=1)
        print("wrote %s: %d classes" % (args[1], len(classes)))
        return 0

    _, classes = read()
    live = [c for c in classes if c["name"] != "Don't Use"]
    total = sum(c["property_count"] for c in classes)

    if args:
        want = args[0].lower()
        hit = [c for c in classes if c["name"].lower() == want]
        if not hit:
            hit = [c for c in classes if want in c["name"].lower()]
        if not hit:
            raise SystemExit("no class matching %r" % args[0])
        for c in hit:
            print("%s   id %#x   type %d %s   %d properties in %d groups   flags %#010x"
                  % (c["name"], c["id"], c["objtype"], c["objtype_name"],
                     c["property_count"], len(c["groups"]), c["flags"]))
            for g in c["groups"]:
                print("\n  %s" % g["name"])
                for p in g["properties"]:
                    extra = ""
                    if p.get("values"):
                        extra = "   {%s}" % ",".join(p["values"])
                    elif p.get("accepts"):
                        extra = "   -> %s" % p["accepts"]
                    print("    %-34s %-20s %-18s %s%s"
                          % (p["label"], p["key"], p["type"], p["default"],
                             extra))
        return 0

    print("%d records, %d named classes (%d live, %d retired as \"Don't Use\")"
          % (TABLE_N, len(classes), len(live), len(classes) - len(live)))
    print("%d properties in total\n" % total)
    for c in sorted(live, key=lambda c: -c["property_count"])[:20]:
        print("  %5d properties  %s" % (c["property_count"], c["name"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
