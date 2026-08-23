"""Record 0x0211: the triangle strips the engine actually draws.

Transcribed from Fellowship.exe, then checked against every record in the
retail data. The model loader is a dispatch table at 0x477800; 0x0211's handler
is FUN_004782d0, which reads a count and that many entries through FUN_00478950,
with FUN_004784b0 reading a group and FUN_00478760 reading an extra:

    record   u32 n;  n x ENTRY                      one entry per LOD
    ENTRY    u32 n;  n x (u32 first, u32 count)     vertex spans to upload
             u32 n;  n x GROUP
             u32 n;  n x EXTRA
    GROUP    u32 texture_slot
             u32 n;  n x (u8 span, u8 base)         where each span lands
             u32 triangle_count
             u32 n;  n bytes                        strips, 1-based, 0 separates
             u32 n;  n x (f32 u, f32 v)             one UV per strip byte
    EXTRA    u32
             u32 n;  n x (u32, u32)
             u32 n;  n x u32

The engine keeps a small window of vertices - a hardware vertex buffer - and a
group's (span, base) pairs say which spans it copies in and where. Strip byte b
addresses window slot b-1, which is span_first + (b - 1 - base). The window
PERSISTS across the groups of an entry, so a group only lists the spans it
needs to upload and may index slots an earlier group left resident.

What the retail data says about all of that:

  * 735 of 735 records parse to their exact final byte, and re-encode
    byte-for-byte
  * 735 of 735 have sum(len(strip) - 2) equal to the triangle count they store
  * 735 of 735 have exactly one entry per LOD
  * 2678 of 2678 groups decode - through the persistent window - to triangles
    that all exist in the model's own 0x0204 polygon list, none invented
  * 733 of 735 records draw every polygon in that list (the other two draw 82%
    and 96% of it)
  * 2490 of 2490 single-texture groups have group.texture equal to the texture
    slot their faces use, which is what identifies that first field

The strips are 1-based with 0 as the separator, and a run of k indices is k-2
triangles with alternating winding. Runs may repeat an index; those degenerate
triangles are how the artists' tool stitched several strips into one run, and
they draw nothing.
"""

import struct


class StripError(Exception):
    pass


PAD = 64          # retail pads the strip and UV arrays to a multiple of this

# The heaviest window in the retail data is 224 slots. A strip index is one
# byte and 0 is the separator, so 255 is the structural ceiling; staying at
# what the game itself does is the safer of the two.
MAX_WINDOW = 224


# ---------------------------------------------------------------------------
# reading
# ---------------------------------------------------------------------------

class _Reader(object):
    def __init__(self, body):
        self.b = body
        self.o = 0

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.o)[0]
        self.o += 4
        return v

    def take(self, n):
        out = self.b[self.o:self.o + n]
        self.o += n
        return out


class Group(object):
    """One draw call: a set of vertex spans, and strips indexing them."""

    __slots__ = ('texture', 'pair', 'triangles', 'indices', 'uvs')

    def __init__(self, texture, pair, triangles, indices, uvs):
        self.texture = texture        # texture slot, as used by 0x0204
        self.pair = pair              # bytes, (span index, window base) x n
        self.triangles = triangles
        self.indices = indices        # raw strip bytes, 1-based, 0 separates
        self.uvs = uvs                # one per strip byte

    def spans(self):
        return [(self.pair[i], self.pair[i + 1])
                for i in range(0, len(self.pair), 2)]

    def strips(self):
        out, cur = [], []
        for b in self.indices:
            if b == 0:
                if cur:
                    out.append(cur)
                cur = []
            else:
                cur.append(b)
        if cur:
            out.append(cur)
        return out


class Entry(object):
    """One LOD."""

    __slots__ = ('ranges', 'groups', 'extras')

    def __init__(self, ranges, groups, extras):
        self.ranges = ranges          # (first vertex, count) spans
        self.groups = groups
        self.extras = extras

    def triangles(self):
        """Decode to (a, b, c) model vertex indices, in draw order.

        Degenerate triangles - the stitches between strips - are skipped, which
        is what the hardware does with them anyway.
        """
        window = {}
        out = []
        for g in self.groups:
            for span, base in g.spans():
                first, count = self.ranges[span]
                for j in range(count):
                    window[base + j] = first + j
            for st in g.strips():
                for k in range(len(st) - 2):
                    x, y, z = st[k], st[k + 1], st[k + 2]
                    if len({x, y, z}) < 3:
                        continue
                    tri = (x, z, y) if k % 2 else (x, y, z)
                    try:
                        out.append(tuple(window[i - 1] for i in tri))
                    except KeyError:
                        raise StripError('strip index %d is outside the window'
                                         % max(tri))
        return out


def parse(body):
    r = _Reader(body)
    entries = []
    for _ in range(r.u32()):
        ranges = [(r.u32(), r.u32()) for _ in range(r.u32())]
        groups = []
        for _ in range(r.u32()):
            texture = r.u32()
            pair = r.take(r.u32() * 2)
            triangles = r.u32()
            indices = r.take(r.u32())
            n = r.u32()
            uvs = [struct.unpack_from('<2f', body, r.o + k * 8) for k in range(n)]
            r.o += n * 8
            groups.append(Group(texture, pair, triangles, indices, uvs))
        extras = []
        for _ in range(r.u32()):
            a = r.u32()
            b = [(r.u32(), r.u32()) for _ in range(r.u32())]
            c = [r.u32() for _ in range(r.u32())]
            extras.append((a, b, c))
        entries.append(Entry(ranges, groups, extras))
    if r.o != len(body):
        raise StripError('consumed %d of %d bytes' % (r.o, len(body)))
    return entries


# ---------------------------------------------------------------------------
# writing
# ---------------------------------------------------------------------------

def encode(entries):
    out = bytearray(struct.pack('<I', len(entries)))
    for e in entries:
        out += struct.pack('<I', len(e.ranges))
        for a, b in e.ranges:
            out += struct.pack('<2I', a, b)
        out += struct.pack('<I', len(e.groups))
        for g in e.groups:
            out += struct.pack('<I', g.texture)
            out += struct.pack('<I', len(g.pair) // 2)
            out += bytes(g.pair)
            out += struct.pack('<I', g.triangles)
            out += struct.pack('<I', len(g.indices))
            out += bytes(g.indices)
            out += struct.pack('<I', len(g.uvs))
            for u, v in g.uvs:
                out += struct.pack('<2f', u, v)
        out += struct.pack('<I', len(e.extras))
        for a, pairs, singles in e.extras:
            out += struct.pack('<I', a)
            out += struct.pack('<I', len(pairs))
            for p, q in pairs:
                out += struct.pack('<2I', p, q)
            out += struct.pack('<I', len(singles))
            for s in singles:
                out += struct.pack('<I', s)
    return bytes(out)


# ---------------------------------------------------------------------------
# building one from a mesh
# ---------------------------------------------------------------------------

GAP = 8           # bridge spans this far apart rather than start a new one


def _runs(vertices, gap=GAP):
    """(first, count) spans covering a sorted set of vertices.

    Vertices within `gap` of the previous span are folded into it, carrying the
    unused slots with them. The window has room to spare on a mesh of any
    sensible size, and both the span index and the window base are single
    bytes, so spending slots to keep the span count down is the right trade -
    it is also what the retail data does, its spans starting on multiples of 4.
    """
    out = []
    for v in vertices:
        if out and v - (out[-1][0] + out[-1][1]) <= gap:
            out[-1] = (out[-1][0], v - out[-1][0] + 1)
        else:
            out.append((v, 1))
    return out


def _window_cost(vertices):
    """How many window slots a set of vertices needs, as spans."""
    return sum(c for _f, c in _runs(sorted(vertices)))


def _chunk(triangles, limit):
    """Split triangles into runs that each fit in `limit` window slots.

    Greedy and order-preserving: keep adding triangles until the spans they
    need would overflow the window, then start a new group. Faces that share
    vertices land together, which is what keeps the count of groups near what
    the artists' tool produced.
    """
    out, cur, used = [], [], set()
    for tri in triangles:
        trial = used | set(tri)
        if cur and _window_cost(trial) > limit:
            out.append(cur)
            cur, used = [], set()
            trial = set(tri)
            if _window_cost(trial) > limit:
                raise StripError('a single triangle spans more than %d vertices, '
                                 'which cannot happen unless the face list is '
                                 'corrupt' % limit)
        cur.append(tri)
        used = trial
    if cur:
        out.append(cur)
    return out


def build(triangles, uv_of_corner, texture_of, vertex_count=None,
          limit=MAX_WINDOW):
    """Make a 0x0211 entry for one LOD.

    `triangles`      list of (i, j, k) model vertex indices, in the engine's
                     winding and in the order they appear in 0x0204
    `uv_of_corner`   called with (triangle index, corner 0-2), returns (u, v)
    `texture_of`     called with a triangle index, returns its texture slot

    Faces are grouped by texture and then cut into windows of at most `limit`
    vertices, because a strip index is a single byte. Each triangle is emitted
    as its own three-index strip rather than merged into long runs: a run of
    three is exactly one triangle, so the engine's arithmetic is identical
    either way, and the only thing longer runs buy is fewer vertices pushed
    across the bus - which mattered in 2002 and does not now.
    """
    # Sort by texture first - a group draws one texture - and within a texture
    # by lowest vertex index. That second key is what keeps the spans few: a
    # modeller's vertex numbering follows the parts of the mesh, so faces that
    # sort together reference vertices that sit together, and a group ends up
    # needing two or three spans instead of fifteen.
    order = sorted(range(len(triangles)),
                   key=lambda t: (texture_of(t), min(triangles[t]), triangles[t]))
    ranges = []
    range_index = {}
    groups = []

    at = 0
    while at < len(order):
        tex = texture_of(order[at])
        run = []
        while at < len(order) and texture_of(order[at]) == tex:
            run.append(order[at])
            at += 1
        cut = 0
        for chunk in _chunk([triangles[t] for t in run], limit):
            take = run[cut:cut + len(chunk)]
            cut += len(chunk)
            used = set()
            for tri in chunk:
                used |= set(tri)
            spans = _runs(sorted(used))
            local = {}
            pair = bytearray()
            base = 0
            for span in spans:
                if span not in range_index:
                    range_index[span] = len(ranges)
                    ranges.append(span)
                pair.append(range_index[span])
                pair.append(base)
                for j in range(span[1]):
                    local[span[0] + j] = base + j
                base += span[1]
            if base > 255:
                raise StripError('window of %d vertices does not fit a byte index'
                                 % base)
            if max(range_index[s] for s in spans) > 255:
                raise StripError('more than 256 vertex spans in one LOD; '
                                 'the mesh is too fragmented to strip')

            idx = bytearray()
            uvs = []
            for t, tri in zip(take, chunk):
                for c in range(3):
                    idx.append(local[tri[c]] + 1)     # 1-based
                    uvs.append(uv_of_corner(t, c))
                idx.append(0)                         # end of strip
                uvs.append((0.0, 0.0))
            while len(idx) % PAD:
                idx.append(0)
                uvs.append((0.0, 0.0))
            groups.append(Group(tex, bytes(pair), len(chunk), bytes(idx), uvs))

    if vertex_count is not None:
        top = max((f + c for f, c in ranges), default=0)
        if top > vertex_count:
            raise StripError('a face references vertex %d of %d'
                             % (top - 1, vertex_count))
    return Entry(ranges, groups, [])
