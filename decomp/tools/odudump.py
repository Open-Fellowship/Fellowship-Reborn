#!/usr/bin/env python3
"""
odudump.py - read the authored property values out of a class database (*.odu).

classdump.py reads the *schema*: every level object class the engine knows, with
its properties in a defined order. This reads the *values*: what a designer
actually set on a particular authored class. The two halves fit together because
the .odu stores one 4-byte slot per property, in the schema's flat order, and
nothing else - no keys, no tags, no per-value type byte. The schema is the only
thing that says what the slots mean, and it is also the only check that they have
been read correctly.

  python odudump.py                        verify the format against every .odu
  python odudump.py --verify [<game root>]  the same, explicitly
  python odudump.py <file.odu>             list the class records in one database
  python odudump.py <file.odu> <name>      one record's authored values
  python odudump.py <file.odu> '#4370'     the same, by record id
  python odudump.py --json <file.odu>      every record in the file, machine readable

GAME points at the installed game; RFL (see classdump.py) at a pristine
Fellowship.rfl. Both are read-only - this tool never writes to either.

## Where the values live

An .odu is an SRSC archive (see the importer's fotr/srsc.py). Class records are
type 0x0102, one per authored class, and each one is:

  u16 + bytes  name              length-prefixed, NUL padded to the stated length
  u32          2                 a version; it is 2 in all 5,838 records
  u64          saved             Windows FILETIME, values land in 2001-2002
  u16 + bytes  author            the developer's login: todd, alan, Eron, Bach...
  u16, u16     model             record id, database id; 0,0 when the class draws
                                 nothing
  u32          flags             UNESTABLISHED, see FLAGS below
  u16, u16     related class     record id, database id of another 0x0102 record,
                                 or 0. Every one of the 101 non-zero values in the
                                 game resolves to a real class record; what the
                                 relation *means* is unestablished.
  u16          bounds present    0 or 1; see below
  16 x f32     bounds            present `bounds present` times
  u32          class             the ObjectDef id - the same id classdump.py reads
                                 out of Fellowship.rfl
  u16                            UNESTABLISHED, see UNKNOWN_U16 below
  u32          properties        the class's property count, per the schema
  u32          slots             total 4-byte slots that follow
  u32 x slots  the value block

The first `properties` slots are the values, one per property, in the schema's
flat order - group by group, in table order, which is exactly what classdump.py
prints and exactly the ordinal the engine's property accessor takes (see
documentation/ORDINAL-MAP.md). There is no key, no length and no type tag in the
block: slot *i* is property *i* and nothing in the file says so.

Every property is present. There is no "changed from default" encoding: a value
equal to the schema default occupies its slot like any other.

## Lists

Slots after the first `properties` are a heap holding list contents. A property
whose type modifier has bit 0 set is a list, and its own slot is not a value but

  u16 first slot | u16 count

pointing into that heap. An empty list stores 0.

So the low bit of the type modifier is the list flag. That is not read off a
plural name - it is what makes the block add up. Treating exactly modifiers 1, 3
and 5 as lists tiles the heap of **all 5,838 class records in the retail game
perfectly**: every heap slot is claimed by exactly one list, none twice, none
left over, no offset out of range. Include modifier 2 or 4 and 430 records fault
immediately with out-of-range offsets; leave out 3 and 52 records have orphan
slots; leave out 5 and 305 do. The list bit is therefore established, and the
remaining modifier bits (0, 2, 4 - "kinds" of object reference) are reported raw
because nothing here establishes what they mean.

## Value encodings

Everything is 4 bytes.

  int, message, channel       i32; a channel is -1 when unset
  float                       f32
  enum                        u32 index into the schema's value-name list.
                              21,577 enum values in the game, every one in range.
  colour                      0x00RRGGBB. 2,615 values, high byte always zero.
  model sound animation       u16 record id, u16 database id, into the database
  string sequence texture     with that id - .mdu, .sdu, .adu, .xdu, .qdu, .tdu,
  movie wave                  .vdu respectively. 0 means unset.
  object reference            u16 record id, u16 database id of another 0x0102
                              class record. 2,938 of these in the game resolve to
                              a record whose class is *exactly* the class the
                              schema says the property accepts, with no
                              mismatches and one dangling id.
  object link                 a level object id - a placement in the .lvl, not a
                              class. Checked against the matching .lvl for every
                              level: 33 of 33 typed links land on a placement of
                              the accepted class. That check needs a .lvl reader
                              and so is NOT reproduced by --verify; it was run
                              against the Blender importer's level.py. An .odu
                              shared between a day and a night .lvl only resolves
                              against the one it was authored with, so a link is
                              level-relative, not database-relative.

## How the ordinal ordering was confirmed

Four things, none of which can be arranged by accident:

**The file carries its own copy of the schema.** Each .odu holds one record of
type 0x0103, zlib-compressed, holding one block per class record:

  u16 count | u16 class record id | count x (u32 type code, u16+bytes label)

Decompressed and compared against Fellowship.rfl, the type-code sequence is
identical for all 5,838 records, and the label is identical for 5,827 - the 11
exceptions are labels the editor truncated to 64 characters. The data files and
the engine image agree on the order, property for property, independently.

**Enums.** 21,577 enum values, every one inside its own property's value list. A
single slot of misalignment would put floats and record ids into those slots.

**Object references.** 2,938 resolve to a class record of exactly the class the
schema says the property accepts. Nothing in the value block records what a slot
points at; the agreement is between the value and a schema the file never states.

**Arithmetic.** `properties` in the record header equals the schema's property
count for that class in every one of the 5,838 records, and the block is exactly
`slots` * 4 bytes long, and the lists tile the remainder exactly.

## What is not established

`FLAGS` - the u32 after the model reference. Observed values are 0, 2, 8, 9, 32,
40, 41, 42, 43, 64 and 72, so only the low seven bits are ever used. Bits 0 and 1
occur essentially only on records that reference a model (113 of 114 and 83 of
84), which suggests they describe the model rather than the class; bit 3 is
uncorrelated with everything checked. Reported raw.

`UNKNOWN_U16` - the u16 between the class id and the property count. Range 0-94,
62 distinct values. It is not the SRSC group id (never equal, in 5,838 records),
not the record id, not constant per class, not constant per file, and not
monotonic in the save time. Reported raw.

The `related class` reference. It resolves, so it is a reference; the 101 uses
chain effect classes together ("small fire [par gen]" -> "small fire
[billboard]"), which is suggestive and is not evidence.

The first 4 of the 16 bounds floats. The last 12 are established: a box given as
a minimum corner and three edge vectors, and the edge vectors are axis-aligned in
all 3,227 records that carry bounds, so it is an AABB. The first 4 are a point
and a positive scalar and are consistent with a bounding sphere, but the sphere
does not contain the box in 2,921 of those records, so calling it a bounding
sphere for the same geometry would be a guess. `bounds present` is 0 or 1 only,
and is 1 exactly when a model is referenced.

Resource references are resolved to `id@database` but not to a name; pulling
model, texture and sound names in would mean a reader for each of those formats.
The Blender importer at fotr_riot_importer/ has them.
"""

import glob
import json
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import classdump                                             # noqa: E402

GAME = os.environ.get(
    "GAME", r"C:\Program Files (x86)\Surreal\Fellowship - Copy")

T_CLASS = 0x0102
T_CLASS_SCHEMA = 0x0103
T_STRING = 0x0400

# Which database file holds each resource type. Read off where the ids actually
# resolve: a sound id lands on a 0x0302 record in the .sdu, an animation on a
# 0x0501 in the .adu, and so on. Record ids are only unique within a file, so a
# sound id often also matches something unrelated in the .tdu; the extension
# below is the one where the *type* matches too.
RESOURCE_FILE = {
    0x004: ".mdu", 0x005: ".sdu", 0x009: ".adu", 0x00A: ".xdu",
    0x00B: ".qdu", 0x00E: ".tdu", 0x010: ".vdu", 0x012: ".wdu",
}

# The low bit of the type modifier means "list": the slot is a
# (first slot, count) pair into the heap rather than a value. Established by the
# heap tiling exactly for modifiers {1,3,5} and faulting for any other set - see
# the module docstring, and --verify, which recomputes it.
LIST_BIT = 0x1


class OduError(Exception):
    pass


# ---------------------------------------------------------------------------
# SRSC. Deliberately a local 30-line reader rather than an import: this tool
# has to run against decomp/ alone, and the format is a header and a table.
# ---------------------------------------------------------------------------

class Archive(object):
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as fh:
            head = fh.read(12)
            if len(head) < 12 or head[:4] != b"SRSC":
                raise OduError("%s is not an SRSC archive" % path)
            self.version, diroff, count = struct.unpack("<HIH", head[4:12])
            fh.seek(diroff)
            raw = fh.read(count * 14)
            fh.seek(0)
            self.data = fh.read()
        if len(raw) < count * 14:
            raise OduError("%s: truncated directory" % path)
        self.records = []
        for i in range(count):
            t, rid, gid, off, size = struct.unpack_from("<HHHII", raw, i * 14)
            self.records.append((t, rid, gid, off, size))

    def of_type(self, t):
        return [r for r in self.records if r[0] == t]

    def body(self, rec):
        return self.data[rec[3]:rec[3] + rec[4]]


def read_string(buf, off):
    """u16 length, then that many bytes, NUL padded. Same as the importer's."""
    n = struct.unpack_from("<H", buf, off)[0]
    s = buf[off + 2:off + 2 + n].split(b"\0")[0].decode("latin-1")
    return s, off + 2 + n


def filetime(v):
    """Windows FILETIME -> ISO date, or '' if it is not a plausible one."""
    if not 0x01B00000_00000000 < v < 0x01E00000_00000000:
        return ""
    import datetime
    return (datetime.datetime(1601, 1, 1)
            + datetime.timedelta(microseconds=v // 10)).strftime("%Y-%m-%d")


# ---------------------------------------------------------------------------
# the installed game: database ids, and the files behind them
# ---------------------------------------------------------------------------

class Game(object):
    """The installed game, indexed by database id.

    Each data folder holds a small text .db naming its own id and the folders it
    depends on; that is where the ids in every (record, database) pair come from.
    Nothing here is guessed - the file says `id 11` and Interface.odu is database
    11.
    """

    def __init__(self, root=GAME):
        self.root = root
        self.dirs = {}
        for path in glob.glob(os.path.join(root, "**", "*.db"), recursive=True):
            if path.lower().endswith("_level.db"):
                continue                       # per-level, not a resource db
            try:
                text = open(path, "rb").read().decode("latin-1")
            except IOError:
                continue
            m = re.search(r"^id (\d+)", text, re.M)
            if m:
                self.dirs.setdefault(int(m.group(1)), os.path.dirname(path))
        self._odu = {}
        self._str = {}

    def odu(self, db):
        if db not in self._odu:
            self._odu[db] = None
            folder = self.dirs.get(db)
            if folder:
                for f in glob.glob(os.path.join(folder, "*.odu")):
                    self._odu[db] = Database(f, self)
                    break
        return self._odu[db]

    def string(self, db, rid):
        """The English text of a 0x0400 record, or None.

        A string record is (2, 2), a FILETIME, an author, then exactly five
        localised strings - English, German, French, Italian, Spanish - then an
        8-byte trailer. That shape holds for all 778 string records in the game;
        only the first is read here.
        """
        if db not in self._str:
            self._str[db] = {}
            folder = self.dirs.get(db)
            for f in glob.glob(os.path.join(folder or "", "*.xdu")):
                try:
                    a = Archive(f)
                except (OduError, IOError):
                    continue
                for rec in a.of_type(T_STRING):
                    self._str[db][rec[1]] = a.body(rec)
        b = self._str[db].get(rid)
        if b is None:
            return None
        try:
            _, o = read_string(b, 16)          # skip (2, 2) and the FILETIME
            return read_string(b, o)[0]
        except struct.error:
            return None


# ---------------------------------------------------------------------------
# the records
# ---------------------------------------------------------------------------

class ClassRecord(object):
    __slots__ = ("db", "id", "name", "author", "saved", "model", "flags",
                 "related", "bounds", "class_id", "unknown_u16",
                 "n_properties", "n_slots", "slots")

    def __repr__(self):
        return "<%s #%d class %#x>" % (self.name, self.id, self.class_id)


def parse_record(body, rid, db):
    c = ClassRecord()
    c.db, c.id = db, rid
    c.name, o = read_string(body, 0)
    version, saved = struct.unpack_from("<IQ", body, o)
    o += 12
    if version != 2:
        raise OduError("%s: record version %d, expected 2" % (c.name, version))
    c.saved = saved
    c.author, o = read_string(body, o)
    mid, mdb, c.flags, rel_id, rel_db = struct.unpack_from("<2HI2H", body, o)
    o += 12
    c.model = (mid, mdb) if mid else None
    c.related = (rel_id, rel_db) if rel_id or rel_db else None
    nb, = struct.unpack_from("<H", body, o)
    o += 2
    c.bounds = struct.unpack_from("<16f", body, o) if nb else None
    o += nb * 64
    c.class_id, c.unknown_u16, c.n_properties, c.n_slots = \
        struct.unpack_from("<IHII", body, o)
    o += 14
    if len(body) - o != 4 * c.n_slots:
        raise OduError("%s: %d bytes of values, expected %d"
                       % (c.name, len(body) - o, 4 * c.n_slots))
    c.slots = struct.unpack_from("<%dI" % c.n_slots, body, o) if c.n_slots else ()
    return c


class Database(object):
    """One .odu, with its class records parsed on demand."""

    def __init__(self, path, game=None):
        self.path = path
        self.game = game or Game()
        self.archive = Archive(path)
        self.db_id = None
        for db, folder in self.game.dirs.items():
            if os.path.normcase(folder) == os.path.normcase(
                    os.path.dirname(os.path.abspath(path))):
                self.db_id = db
                break
        self._raw = dict((r[1], self.archive.body(r))
                         for r in self.archive.of_type(T_CLASS))
        self._cache = {}

    def ids(self):
        return sorted(self._raw)

    def has(self, rid):
        return rid in self._raw

    def record(self, rid):
        if rid not in self._cache:
            self._cache[rid] = parse_record(self._raw[rid], rid, self.db_id)
        return self._cache[rid]

    def embedded_schema(self):
        """The 0x0103 record: {class record id: [(type code, label)]}.

        The editor writes its own copy of the property list for every class
        record it saves, so that a database can be opened without the engine's
        ObjectDef table to hand. zlib, then one block per class record:

            u16 count | u16 class record id | count x (u32 type, u16+bytes label)

        Blocks are not in record-id order, hence the id in the header. This is
        the strongest check on the value block there is: the .odu states, in its
        own words, which properties a record has and in which order.
        """
        recs = self.archive.of_type(T_CLASS_SCHEMA)
        if not recs:
            return {}
        d = zlib.decompress(self.archive.body(recs[0]))
        out, o = {}, 0
        while o < len(d):
            count, rid = struct.unpack_from("<2H", d, o)
            o += 4
            entries = []
            for _ in range(count):
                code, = struct.unpack_from("<I", d, o)
                o += 4
                label, o = read_string(d, o)
                entries.append((code, label))
            out[rid] = entries
        return out

    def find(self, want):
        """A record by '#id' or by name, exact first then substring."""
        if want.startswith("#"):
            return [self.record(int(want[1:], 0))]
        hits = [self.record(i) for i in self.ids()
                if self.record(i).name.lower() == want.lower()]
        if not hits:
            hits = [self.record(i) for i in self.ids()
                    if want.lower() in self.record(i).name.lower()]
        return hits


# ---------------------------------------------------------------------------
# schema + record -> named values
# ---------------------------------------------------------------------------

class Schema(object):
    def __init__(self):
        self.img, self.classes = classdump.read()
        self.by_id = dict((c["id"], c) for c in self.classes)
        self.flat = dict((c["id"], [p for g in c["groups"] for p in g["properties"]])
                         for c in self.classes)

    def properties(self, class_id):
        return self.flat.get(class_id)

    def name(self, class_id):
        c = self.by_id.get(class_id)
        return c["name"] if c else None


def is_list(prop):
    return bool((prop["type_code"] >> 12) & LIST_BIT)


def values(rec, schema):
    """[(ordinal, property, value_or_list)] for one record.

    A list property yields a Python list of its heap slots; everything else
    yields its raw 32-bit slot. Raises if the record does not agree with the
    schema, because a value block that has drifted by one slot is worse than no
    value block at all.
    """
    props = schema.properties(rec.class_id)
    if props is None:
        raise OduError("%s: class %#x is not in the ObjectDef table"
                       % (rec.name, rec.class_id))
    if len(props) != rec.n_properties:
        raise OduError("%s: record says %d properties, schema says %d"
                       % (rec.name, rec.n_properties, len(props)))
    out = []
    for i, p in enumerate(props):
        if is_list(p):
            first, count = rec.slots[i] & 0xFFFF, rec.slots[i] >> 16
            if count and (first < rec.n_properties
                          or first + count > rec.n_slots):
                raise OduError("%s: list %s runs outside the heap"
                               % (rec.name, p["key"]))
            out.append((i, p, list(rec.slots[first:first + count])))
        else:
            out.append((i, p, rec.slots[i]))
    return out


def format_value(v, prop, game):
    """One slot, as text. Unresolvable references print as id@database."""
    base = prop["type_code"] & 0xFFF

    if base == 0x002:
        return repr(round(struct.unpack("<f", struct.pack("<I", v))[0], 6))
    if base in (0x001, 0x011):
        return str(struct.unpack("<i", struct.pack("<I", v))[0])
    if base == 0x008:
        return "none" if v == 0xFFFFFFFF else str(v)
    if base == 0x00F:
        return "#%06X" % (v & 0xFFFFFF)
    if base == 0x007:
        names = prop.get("values") or []
        return "%s (%d)" % (names[v], v) if v < len(names) else "%d (!)" % v
    if base == 0x013:
        return "-" if v == 0 else "level object %d" % v

    rid, rdb = v & 0xFFFF, v >> 16
    if v == 0:
        return "-"
    if base == 0x00A:
        s = game.string(rdb, rid) if game else None
        if s is not None:
            return '"%s"' % (s if len(s) <= 60 else s[:57] + "...")
        return "string %d@%d" % (rid, rdb)
    if base == 0x003:
        target = game.odu(rdb) if game else None
        if target is not None and target.has(rid):
            t = target.record(rid)
            return '"%s" %d@%d' % (t.name, rid, rdb)
        return "%d@%d (unresolved)" % (rid, rdb)
    return "%d@%d" % (rid, rdb)


# ---------------------------------------------------------------------------
# output
# ---------------------------------------------------------------------------

def print_record(rec, schema, game):
    cname = schema.name(rec.class_id) or "%#x" % rec.class_id
    print("%s   record %d   class %s (%#x)"
          % (rec.name, rec.id, cname, rec.class_id))
    bits = ["%d properties in %d slots" % (rec.n_properties, rec.n_slots)]
    if rec.author:
        bits.append("by %s" % rec.author)
    d = filetime(rec.saved)
    if d:
        bits.append(d)
    if rec.model:
        bits.append("model %d@%d" % rec.model)
    if rec.related:
        bits.append("related class %d@%d" % rec.related)
    bits.append("flags %#x" % rec.flags)
    bits.append("u16 %d" % rec.unknown_u16)
    print("  " + "   ".join(bits))

    vals = dict((i, (p, v)) for i, p, v in values(rec, schema))
    i = 0
    for group in schema.by_id[rec.class_id]["groups"]:
        print("\n  %s" % group["name"])
        for _ in group["properties"]:
            p, v = vals[i]
            if isinstance(v, list):
                text = ("[%s]" % ", ".join(
                    format_value(x, p, game) for x in v)) if v else "[]"
            else:
                text = format_value(v, p, game)
            print("    %3d  %-34s %-20s %-18s %s"
                  % (i, p["label"] or "", p["key"] or "", p["type"], text))
            i += 1


def record_json(rec, schema, game):
    out = {"id": rec.id, "name": rec.name, "author": rec.author,
           "saved": filetime(rec.saved), "class_id": rec.class_id,
           "class": schema.name(rec.class_id), "model": rec.model,
           "related_class": rec.related, "flags": rec.flags,
           "unknown_u16": rec.unknown_u16, "bounds": rec.bounds,
           "properties": []}
    for i, p, v in values(rec, schema):
        out["properties"].append({
            "ordinal": i, "label": p["label"], "key": p["key"],
            "type": p["type"], "type_code": p["type_code"],
            "raw": v,
            "value": ([format_value(x, p, game) for x in v]
                      if isinstance(v, list)
                      else format_value(v, p, game)),
        })
    return out


# ---------------------------------------------------------------------------
# verification - the claim in the docstring, recomputed
# ---------------------------------------------------------------------------

def verify(root=GAME, list_bit=LIST_BIT):
    """Walk every .odu and check the format holds, record by record.

    This is the evidence, not a test: the format is only credible because the
    value block adds up against a schema that is stored somewhere else entirely.
    Anything that does not add up is printed.
    """
    schema = Schema()
    game = Game(root)
    total = perfect = 0
    problems = []
    agree = {"types": 0, "type-differs": 0, "labels": 0, "label-differs": 0,
             "refs": 0, "ref-class-differs": 0, "ref-dangling": 0,
             "ref-accepts-unnamed": 0}
    by_name = dict((c["name"], c["id"]) for c in schema.classes)
    for path in sorted(glob.glob(os.path.join(root, "**", "*.odu"),
                                 recursive=True)):
        db = Database(path, game)
        try:
            embedded = db.embedded_schema()
        except Exception:                        # zlib or a short block
            embedded = {}
        for rid in db.ids():
            total += 1
            try:
                rec = db.record(rid)
                props = schema.properties(rec.class_id)
                if props is None:
                    raise OduError("class %#x not in the ObjectDef table"
                                   % rec.class_id)
                if len(props) != rec.n_properties:
                    raise OduError("%d properties, schema says %d"
                                   % (rec.n_properties, len(props)))
                # every heap slot claimed exactly once
                cover = [0] * rec.n_slots
                for i in range(rec.n_properties):
                    cover[i] += 1
                for i, p in enumerate(props):
                    if not ((p["type_code"] >> 12) & list_bit):
                        continue
                    first = rec.slots[i] & 0xFFFF
                    count = rec.slots[i] >> 16
                    if not count:
                        continue
                    if first < rec.n_properties or first + count > rec.n_slots:
                        raise OduError("list %s: %d..%d outside %d..%d"
                                       % (p["key"], first, first + count,
                                          rec.n_properties, rec.n_slots))
                    for k in range(first, first + count):
                        cover[k] += 1
                if any(c != 1 for c in cover):
                    raise OduError("heap not tiled: %d slots unclaimed, "
                                   "%d claimed twice"
                                   % (sum(1 for c in cover if c == 0),
                                      sum(1 for c in cover if c > 1)))
                # every enum inside its own value list, and every object
                # reference pointing at a class the schema will accept
                for i, p, v in values(rec, schema):
                    base = p["type_code"] & 0xFFF
                    for x in (v if isinstance(v, list) else [v]):
                        if base == 0x007:
                            n = len(p.get("values") or [])
                            if n and x >= n:
                                raise OduError("enum %s = %d, only %d values"
                                               % (p["key"], x, n))
                        elif base == 0x003 and x:
                            target = game.odu(x >> 16)
                            t = (target.record(x & 0xFFFF)
                                 if target and target.has(x & 0xFFFF)
                                 else None)
                            want = by_name.get(p.get("accepts"))
                            if t is None:
                                agree["ref-dangling"] += 1
                            elif want is None:
                                agree["ref-accepts-unnamed"] += 1
                            elif t.class_id == want:
                                agree["refs"] += 1
                            else:
                                agree["ref-class-differs"] += 1
                # the editor's own copy of the schema, if the file has one
                mine = embedded.get(rid)
                if mine is not None and len(mine) == len(props):
                    agree["types" if all(e[0] == p["type_code"]
                                         for e, p in zip(mine, props))
                          else "type-differs"] += 1
                    agree["labels" if all(e[1] == p["label"]
                                          for e, p in zip(mine, props))
                          else "label-differs"] += 1
                perfect += 1
            except (OduError, struct.error, IndexError) as e:
                problems.append((os.path.basename(path), rid, str(e)))
    print("%d class records in %d databases" % (total, len(game.dirs)))
    print("%d parse, tile their heap exactly and hold no out-of-range enum"
          % perfect)
    print("%d agree with the .odu's own embedded property list, type for type"
          " (%d do not)" % (agree["types"], agree["type-differs"]))
    print("%d agree on the labels too (%d do not - editor truncation at 64"
          " characters)" % (agree["labels"], agree["label-differs"]))
    print("%d object references land on a record of exactly the class the"
          " schema accepts" % agree["refs"])
    print("  %d land on some other class, %d are dangling, %d name a class the"
          " ObjectDef table does not"
          % (agree["ref-class-differs"], agree["ref-dangling"],
             agree["ref-accepts-unnamed"]))
    if problems:
        print("\n%d that do not:" % len(problems))
        for p in problems[:40]:
            print("  %-24s #%-6d %s" % p)
    return 0 if not problems else 1


def main(argv):
    args = argv[1:]
    as_json = False
    if args and args[0] == "--json":
        as_json, args = True, args[1:]
    if args and args[0] == "--verify":
        args = args[1:]
        return verify(args[0] if args else GAME)
    if not args:
        return verify()

    path = args[0]
    if not os.path.isfile(path):
        raise SystemExit("no such file: %s" % path)
    schema = Schema()
    game = Game()
    db = Database(path, game)

    if as_json:
        out = []
        for rid in db.ids():
            try:
                out.append(record_json(db.record(rid), schema, game))
            except (OduError, struct.error) as e:
                out.append({"id": rid, "error": str(e)})
        json.dump(out, sys.stdout, indent=1)
        return 0

    if len(args) > 1:
        hits = db.find(args[1])
        if not hits:
            raise SystemExit("no record matching %r in %s"
                             % (args[1], os.path.basename(path)))
        for i, rec in enumerate(hits):
            if i:
                print()
            print_record(rec, schema, game)
        return 0

    print("%s   database %s   %d class records"
          % (os.path.basename(path),
             db.db_id if db.db_id is not None else "?", len(db.ids())))
    for rid in db.ids():
        try:
            rec = db.record(rid)
            print("  %-6d %-40s %-28s %d properties"
                  % (rid, rec.name,
                     schema.name(rec.class_id) or "%#x" % rec.class_id,
                     rec.n_properties))
        except (OduError, struct.error) as e:
            print("  %-6d %s" % (rid, e))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
