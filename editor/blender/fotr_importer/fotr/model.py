"""Model (*.mdu) reader for Fellowship of the Ring (SRSC v0x0101).

Differences from the Drakan-era format documented by OpenDrakan:
  * 0x0200 carries the texture list (Drakan used a separate 0x0206 record)
  * 0x0203 stores positions and packed normals in two planar blocks, not interleaved
  * 0x0204 stores UVs in a trailing block instead of inline with the indices
  * 0x0208 joint transforms are stored translation-first
"""

import struct
from . import srsc
from .srsc import read_string


class Model(object):
    def __init__(self):
        self.id = 0
        self.name = ''
        self.source = ''
        self.author = ''
        self.shading = 0
        self.textures = []      # [(texture_id, database_id)]
        self.vertices = []      # [(x, y, z)]
        self.polygons = []      # [Polygon]
        self.lods = []          # [Lod]
        self.skeleton = None    # Skeleton or None
        self.animation_refs = []

    @property
    def has_mesh(self):
        return bool(self.vertices) and bool(self.polygons)

    def lod_mesh(self, index=0):
        """(vertices, polygons) for one LOD, with indices rebased to 0."""
        if not self.lods:
            return self.vertices, self.polygons
        lod = self.lods[max(0, min(index, len(self.lods) - 1))]
        return (self.vertices[lod.first_vertex:lod.first_vertex + lod.num_vertices],
                self.polygons[lod.first_poly:lod.first_poly + lod.num_polys])


class Polygon(object):
    __slots__ = ('flags', 'texture', 'indices', 'uvs')

    def __init__(self, flags, texture, indices, uvs):
        self.flags, self.texture, self.indices, self.uvs = flags, texture, indices, uvs

    @property
    def double_sided(self):
        return bool(self.flags & 0x0002)


class Lod(object):
    __slots__ = ('distance', 'first_vertex', 'num_vertices', 'first_poly', 'num_polys')

    def __init__(self, distance, fv, nv, fp, np_):
        self.distance, self.first_vertex, self.num_vertices = distance, fv, nv
        self.first_poly, self.num_polys = fp, np_


class Joint(object):
    __slots__ = ('index', 'name', 'rot', 'trans', 'mesh', 'first_child',
                 'next_sibling', 'parent', 'weights')

    def __init__(self):
        self.index = 0
        self.name = ''
        self.rot = ((1, 0, 0), (0, 1, 0), (0, 0, 1))
        self.trans = (0.0, 0.0, 0.0)
        self.mesh = -1
        self.first_child = 0
        self.next_sibling = 0
        self.parent = -1
        self.weights = []       # per LOD: [(vertex_index_in_lod, weight)]

    def rest_matrix(self):
        """Model-space rest transform as (3x3 rows, translation).

        The record stores the inverse bind transform, and stores its 3x3
        column-major.  Reading the nine floats as rows therefore already gives
        the transpose, so the bind rotation is that matrix as-is and the bind
        position is -M*t rather than -M^T*t.

        This is easy to get wrong and hard to notice: a character standing in a
        T-pose has axis-aligned joint rotations, which are symmetric, so both
        readings agree.  It only diverges on joints rotated off-axis - fingers,
        shoulders, anything on a crouched model - where the wrong reading throws
        joints metres away from the vertices they weight.
        """
        r = self.rot
        t = self.trans
        pos = tuple(-(r[i][0] * t[0] + r[i][1] * t[1] + r[i][2] * t[2]) for i in range(3))
        return r, pos


class Skeleton(object):
    def __init__(self):
        self.node_names = []    # every node in the original rig, joints and not
        self.joints = []        # [Joint], indices match the animation node indices

    def roots(self):
        return [j for j in self.joints if j.parent < 0]


class ModelDatabase(object):
    """All models in one *.mdu."""

    def __init__(self, path):
        self.srsc = srsc.SRSC(path)
        self.path = path
        self._recs = {}
        for r in self.srsc.records:
            if 0x0200 <= r.type <= 0x0220:
                self._recs.setdefault(r.id, {})[r.type] = r
        self.ids = sorted(i for i, v in self._recs.items() if srsc.T_MODEL_NAME in v)
        self._names = {}

    # -- listing --------------------------------------------------------
    def name_of(self, mid):
        if mid not in self._names:
            b = self.srsc.body(self._recs[mid][srsc.T_MODEL_NAME])
            self._names[mid] = read_string(b, 0)[0]
        return self._names[mid]

    def listing(self):
        return [(mid, self.name_of(mid)) for mid in self.ids]

    def find(self, needle):
        n = needle.lower()
        return [mid for mid in self.ids if n in self.name_of(mid).lower()]

    # -- loading --------------------------------------------------------
    def load(self, mid):
        recs = self._recs[mid]
        m = Model()
        m.id = mid
        self._read_header(m, self.srsc.body(recs[srsc.T_MODEL_NAME]))
        if srsc.T_MODEL_VERTS in recs:
            m.vertices = _read_vertices(self.srsc.body(recs[srsc.T_MODEL_VERTS]))
        if srsc.T_MODEL_POLYS in recs:
            m.polygons = _read_polygons(self.srsc.body(recs[srsc.T_MODEL_POLYS]))
        if srsc.T_MODEL_CHAR in recs:
            _read_character(m, self.srsc.body(recs[srsc.T_MODEL_CHAR]))
        if not m.lods:
            m.lods = [Lod(0.0, 0, len(m.vertices), 0, len(m.polygons))]
        return m

    @staticmethod
    def _read_header(m, b):
        m.name, o = read_string(b, 0)
        m.source, o = read_string(b, o)
        o += 8                                          # Windows FILETIME
        m.author, o = read_string(b, o)
        if o + 12 > len(b):
            return
        m.shading = struct.unpack_from('<H', b, o)[0]
        # The texture list is not always at a fixed offset - six shipped models
        # carry an extra dword before it - so the offset is probed rather than
        # assumed. Reading it wrong gives a model a list of garbage references.
        from .mesh import texture_list_offset
        at = texture_list_offset(b)
        if at is None:
            return
        count = struct.unpack_from('<I', b, at)[0]
        at += 4
        count = min(count, max(0, (len(b) - at) // 4))
        m.textures = [struct.unpack_from('<HH', b, at + i * 4) for i in range(count)]


def _read_vertices(b):
    """uint16 count; vec3f positions[count]; uint32 packed_normal[count]."""
    if len(b) < 2:
        return []
    n = struct.unpack_from('<H', b, 0)[0]
    if n == 0 or 2 + n * 12 > len(b):
        return []
    return [struct.unpack_from('<3f', b, 2 + i * 12) for i in range(n)]


def _read_polygons(b):
    """Index block, then a trailing block holding one UV pair per polygon corner."""
    if len(b) < 2:
        return []
    n = struct.unpack_from('<H', b, 0)[0]
    o = 2
    out = []
    corners = 0
    for _ in range(n):
        flags, vcount, tex = struct.unpack_from('<3H', b, o)
        o += 6
        idx = struct.unpack_from('<%dH' % vcount, b, o)
        o += 2 * vcount
        corners += vcount
        out.append(Polygon(flags, tex, list(idx), []))
    o += 2                                              # separator
    if o + corners * 8 <= len(b):
        for p in out:
            for _ in p.indices:
                p.uvs.append(struct.unpack_from('<2f', b, o))
                o += 8
    return out


def _read_character(m, b):
    """0x0208: bind pose skeleton, vertex weights and the LOD table."""
    o = 64                                              # bounding sphere + OBB
    num_lods = struct.unpack_from('<I', b, o)[0]
    o += 4
    for _ in range(max(0, num_lods - 1)):
        _, o = read_string(b, o)                        # source path of each lower LOD

    skel = Skeleton()
    node_count = struct.unpack_from('<I', b, o)[0]
    o += 4
    node_joint = []
    for i in range(node_count):
        name = b[o:o + 32].split(b'\x00')[0].decode('latin-1')
        ji = struct.unpack_from('<i', b, o + 32)[0]
        skel.node_names.append(name)
        node_joint.append(ji)
        o += 36

    o += 4                                              # total vertex count
    joint_count = struct.unpack_from('<I', b, o)[0]
    o += 4
    for j in range(joint_count):
        jt = Joint()
        jt.index = j
        jt.trans = struct.unpack_from('<3f', b, o)
        f = struct.unpack_from('<9f', b, o + 12)
        jt.rot = (f[0:3], f[3:6], f[6:9])
        o += 48
        jt.mesh, jt.first_child, jt.next_sibling = struct.unpack_from('<3i', b, o)
        o += 12
        for _ in range(num_lods):
            c = struct.unpack_from('<H', b, o)[0]
            o += 2
            jt.weights.append([struct.unpack_from('<If', b, o + k * 8) for k in range(c)])
            o += c * 8
        skel.joints.append(jt)

    for i, ji in enumerate(node_joint):                 # give joints their rig names
        if 0 <= ji < len(skel.joints) and not skel.joints[ji].name:
            skel.joints[ji].name = skel.node_names[i]
    for j, jt in enumerate(skel.joints):
        if not jt.name:
            jt.name = 'joint_%d' % j

    _link_parents(skel)
    m.skeleton = skel

    for _ in range(num_lods):                           # LOD table: uint16 + 28 bytes
        d, _usage, _node, fv, nv, fp, np_ = struct.unpack_from('<f6I', b, o + 2)
        m.lods.append(Lod(d, fv, nv, fp, np_))
        o += 30

    if o + 2 <= len(b):                                 # animation references
        na = struct.unpack_from('<H', b, o)[0]
        o += 2
        if o + na * 4 <= len(b):
            m.animation_refs = [struct.unpack_from('<2H', b, o + i * 4) for i in range(na)]


def _link_parents(skel):
    """first_child / next_sibling indices into a parent map (0 means 'none')."""
    joints = skel.joints
    if not joints:
        return
    seen = set()
    stack = [(0, -1)]
    while stack:
        j, parent = stack.pop()
        if j in seen or j >= len(joints):
            continue
        seen.add(j)
        joints[j].parent = parent
        child = joints[j].first_child
        while child and child < len(joints) and child not in seen:
            stack.append((child, j))
            nxt = joints[child].next_sibling
            if nxt == child:
                break
            child = nxt
    for j, jt in enumerate(joints):                     # orphans become roots
        if j not in seen:
            jt.parent = -1
