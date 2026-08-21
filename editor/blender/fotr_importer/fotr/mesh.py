"""Writing side of the model (*.mdu) records.

Three records describe a drawable mesh and all three round-trip byte-exactly
through the encoders here - 1592 of them across the retail archives, which is
every 0x0203 and 0x0204 the game ships:

  0x0203  uint16 count, then count vec3f positions, then count packed normals
  0x0204  uint16 count, then the polygons, then a uint16, then one UV pair per
          polygon corner
  0x0207  bounding sphere and an axis-aligned box, 96 bytes

A packed normal is three signed bytes scaled by 1/127 with 0x7f in the top
byte; a vertex no polygon references stores four zero bytes instead.  The
normals point the way the engine's clockwise-front winding implies, which is
the opposite of the counter-clockwise cross product, so they are negated on the
way out exactly as the importer negates them on the way in.

That trailing uint16 in 0x0204 is the interesting one.  It is not a separator:
it counts the polygons that are *not* covered by the model's 0x0211 record, and
it is zero for the 771 models whose 0x0211 covers everything.  The 25 that ship
without a complete 0x0211 - Far Mountains, Treasure_Pile, the torch warps, the
sun and moon billboards - set it to their full polygon count instead.  So a
mesh whose topology has changed can be written the same way the game itself
writes an un-cached model: drop 0x0211 and set this field to the polygon count.
"""

import math
import struct


class MeshWriteError(Exception):
    pass


# The two volumes occupy a fixed 72-byte prefix.  What follows varies: an
# optional list of sub-volumes that makes the record anything from 74 to 724
# bytes in the retail data.  None of it depends on the vertices, so it is
# copied through untouched.
BOUNDS_PREFIX = 72


# ---------------------------------------------------------------------------
# 0x0203 - vertices
# ---------------------------------------------------------------------------

def encode_vertices(positions, normals=None):
    """positions: [(x, y, z)]; normals: [(x, y, z)] or None for a zero normal."""
    n = len(positions)
    if n > 0xFFFF:
        raise MeshWriteError('a model record holds at most 65535 vertices, got %d' % n)
    out = bytearray(struct.pack('<H', n))
    for p in positions:
        out += struct.pack('<3f', float(p[0]), float(p[1]), float(p[2]))
    for i in range(n):
        out += struct.pack('<I', pack_normal(normals[i] if normals else None))
    return bytes(out)


def pack_normal(v):
    """Three signed bytes over 127, with 0x7f padding; zero when unreferenced."""
    if v is None:
        return 0
    x, y, z = float(v[0]), float(v[1]), float(v[2])
    length = math.sqrt(x * x + y * y + z * z)
    if length < 1e-9:
        return 0
    b = []
    for c in (x / length, y / length, z / length):
        i = int(round(c * 127.0))
        b.append(max(-127, min(127, i)) & 0xFF)
    return b[0] | (b[1] << 8) | (b[2] << 16) | (0x7F << 24)


def decode_normal(word):
    if word == 0:
        return None
    out = []
    for shift in (0, 8, 16):
        v = (word >> shift) & 0xFF
        out.append((v - 256 if v > 127 else v) / 127.0)
    return tuple(out)


def smooth_normals(positions, faces):
    """Area-weighted vertex normals in the engine's winding.

    Only used when the caller has nothing better; a Blender export passes the
    mesh's own normals instead, which respect its shading and custom splits.
    """
    acc = [[0.0, 0.0, 0.0] for _ in positions]
    for idx in faces:
        if len(idx) < 3:
            continue
        a, b, c = positions[idx[0]], positions[idx[1]], positions[idx[2]]
        u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        w = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        # negated cross product: the engine winds clockwise-front
        cr = (u[2] * w[1] - u[1] * w[2],
              u[0] * w[2] - u[2] * w[0],
              u[1] * w[0] - u[0] * w[1])
        for i in idx:
            for k in range(3):
                acc[i][k] += cr[k]
    return [tuple(v) for v in acc]


# ---------------------------------------------------------------------------
# 0x0204 - polygons
# ---------------------------------------------------------------------------

def encode_polygons(polygons, uncached=0):
    """polygons: [(flags, texture_slot, [vertex indices], [(u, v) per corner])].

    `uncached` is that trailing uint16: the number of these polygons that the
    model's 0x0211 strip record does NOT draw.  Zero when the strips cover the
    whole mesh, which is what this add-on always writes, and the full polygon
    count for a model with no 0x0211 at all.

    The retail data pins the meaning down exactly.  Two models draw only part of
    their face list from strips - Hill Troll Hammer draws 379 of 458 and Boat
    draws 444 of 460 - and their trailing counts are 79 and 16, the undrawn
    remainder in both cases.
    """
    n = len(polygons)
    if n > 0xFFFF:
        raise MeshWriteError('a model record holds at most 65535 polygons, got %d' % n)
    out = bytearray(struct.pack('<H', n))
    corners = []
    for flags, texture, indices, uvs in polygons:
        vc = len(indices)
        if vc < 3:
            raise MeshWriteError('polygon with %d corners' % vc)
        if len(uvs) != vc:
            raise MeshWriteError('polygon has %d corners but %d UVs' % (vc, len(uvs)))
        out += struct.pack('<3H', flags & 0xFFFF, vc, texture & 0xFFFF)
        for i in indices:
            if not 0 <= i <= 0xFFFF:
                raise MeshWriteError('vertex index %d out of range' % i)
        out += struct.pack('<%dH' % vc, *indices)
        corners.extend(uvs)
    out += struct.pack('<H', n if uncached is None else (uncached & 0xFFFF))
    for u, v in corners:
        out += struct.pack('<2f', float(u), float(v))
    return bytes(out)


def polygon_uncached_count(body):
    """Read that trailing uint16 back out of an existing record."""
    n = struct.unpack_from('<H', body, 0)[0]
    o = 2
    for _ in range(n):
        _flags, vc, _tex = struct.unpack_from('<3H', body, o)
        o += 6 + 2 * vc
    return struct.unpack_from('<H', body, o)[0]


# ---------------------------------------------------------------------------
# 0x0207 - bounds
# ---------------------------------------------------------------------------

def encode_bounds(original_body, positions):
    """Refresh the sphere and the box, leaving every other field alone.

    The record carries more than the two volumes - an id, a flag and a list of
    sub-volumes - and none of that follows the vertices, so the original bytes
    are kept and only the sixteen floats describing the two volumes change.
    """
    if len(original_body) < BOUNDS_PREFIX:
        raise MeshWriteError('bounds record is %d bytes, expected at least %d'
                             % (len(original_body), BOUNDS_PREFIX))
    if not positions:
        return bytes(original_body)
    out = bytearray(original_body)

    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    centre = [sum(p[k] for p in positions) / float(len(positions)) for k in range(3)]
    radius = 0.0
    for p in positions:
        d = math.sqrt(sum((p[k] - centre[k]) ** 2 for k in range(3)))
        if d > radius:
            radius = d

    struct.pack_into('<4f', out, 8, centre[0], centre[1], centre[2], radius)
    struct.pack_into('<3f', out, 24, lo[0], lo[1], lo[2])
    struct.pack_into('<9f', out, 36,
                     hi[0] - lo[0], 0.0, 0.0,
                     0.0, hi[1] - lo[1], 0.0,
                     0.0, 0.0, hi[2] - lo[2])
    return bytes(out)


# ---------------------------------------------------------------------------
# 0x0200 texture list
# ---------------------------------------------------------------------------

# Only used to tell a real texture count from a stray dword while probing for
# the list. The most any retail model declares is 14; the ceiling here is high
# enough not to reject a model built in Blender with many materials, and still
# far below the garbage value (1193279488) the six odd headers carry.
MAX_TEXTURES = 255
RETAIL_MAX_TEXTURES = 14


def texture_list_offset(body):
    """Byte offset of the texture count in a 0x0200 record, or None.

    The fixed part is name, source path, FILETIME, author, then shading and two
    unidentified fields.  Six of the 796 shipped models - Lembas, Cram, Mushroom
    among them - carry one extra dword there, a float that reads as 40960.0, and
    on those the count sits four bytes later.  Rather than guess, each candidate
    offset is tested for a count that is actually plausible: small, and with room
    in the record for that many pairs.  Every one of the 796 resolves to exactly
    one candidate.
    """
    from .srsc import read_string
    _name, o = read_string(body, 0)
    _source, o = read_string(body, o)
    o += 8                                       # Windows FILETIME
    _author, o = read_string(body, o)
    if o + 12 > len(body):
        return None
    for at in (o + 8, o + 12):
        if at + 4 > len(body):
            break
        count = struct.unpack_from('<I', body, at)[0]
        if count <= MAX_TEXTURES and at + 4 + count * 4 <= len(body):
            return at
    return None


def set_texture_list(body, refs):
    """Rewrite a 0x0200 record's texture list, keeping everything around it.

    `refs` is [(texture_id, database_id)].  The trailing fields after the list -
    unidentified, but preserved by every other writer here - are carried over
    unchanged, which is what makes this safe to do to a shipped record.
    """
    at = texture_list_offset(body)
    if at is None:
        raise MeshWriteError('this model header has no texture list')
    old = struct.unpack_from('<I', body, at)[0]
    old = min(old, max(0, (len(body) - at - 4) // 4))
    tail = body[at + 4 + old * 4:]
    out = bytearray(body[:at])
    out += struct.pack('<I', len(refs))
    for tid, db_id in refs:
        if not (0 <= tid <= 0xFFFF and 0 <= db_id <= 0xFFFF):
            raise MeshWriteError('texture reference (%r, %r) is out of range'
                                 % (tid, db_id))
        out += struct.pack('<2H', tid, db_id)
    return bytes(out) + tail
