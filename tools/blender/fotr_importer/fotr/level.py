"""Level (*.lvl) reader - terrain layers and object placements.

A *.lvl is an SRSC archive holding the map itself:

    0x0000  level name and the list of databases it depends on
    0x0001  terrain layers: heightmap grids with per-cell textures
    0x0002  layer groups (editor only)
    0x0020  object placements: what stands where, referenced by class
    0x0026  fixed nodes
    0x0027  unknown

The models are not in here.  Each placement names a class in some database; the
class names a model in some database.  See database.py and klass.py.

Both the object and layer records differ from the Drakan-era format documented by
OpenDrakan; the differences are noted against each structure below.
"""

import struct
import zlib

from . import srsc
from .srsc import read_string

T_HEADER = 0x0000
T_LAYERS = 0x0001
T_OBJECTS = 0x0020

TYPE_FLOOR, TYPE_CEILING, TYPE_BETWEEN = 0, 1, 2
TYPE_NAMES = {0: 'floor', 1: 'ceiling', 2: 'between'}

NO_TEXTURE = (0, 0)

# object flags
FLAG_VISIBLE = 0x0001
FLAG_EXTRA_FLOAT = 0x0010       # adds one float right after the flag word
FLAG_SCALED = 0x0100            # adds three scale floats

# 2048 world units make one unit of model space, so terrain and models share a
# scale once positions are divided through by this.
WORLD_UNIT = 2048.0

VERTEX_STRIDE = 6               # Drakan used 4
FACE_STRIDE = 22                # Drakan used 26 with two textures per cell
FLAG_FLAT_VERTEX_COLOR = 0x10


# ---------------------------------------------------------------------------
# data model
# ---------------------------------------------------------------------------

class LevelObject(object):
    __slots__ = ('id', 'class_id', 'class_db', 'layer', 'position', 'rotation',
                 'scale', 'flags', 'links')

    def __init__(self):
        self.id = 0
        self.class_id = 0
        self.class_db = 0
        self.layer = 0
        self.position = (0.0, 0.0, 0.0)     # world units, Y up
        self.rotation = (0, 0, 0)           # degrees
        self.scale = (1.0, 1.0, 1.0)
        self.flags = 0
        self.links = ()

    @property
    def visible(self):
        return bool(self.flags & FLAG_VISIBLE)

    def __repr__(self):
        return '<Object %d class=%d/%d at %s>' % (self.id, self.class_id,
                                                  self.class_db, self.position)


class Vertex(object):
    __slots__ = ('type', 'unknown', 'height')

    def __init__(self, type_, unknown, height):
        self.type = type_
        self.unknown = unknown
        self.height = height                # world units, relative to the layer


class Face(object):
    """One terrain cell: a quad split into two triangles.

    ::

        a---b     a = (col,     row)
        |   |     b = (col + 1, row)
        c---d     c = (col,     row + 1)
                  d = (col + 1, row + 1)
    """
    __slots__ = ('flags', 'corner_mask', 'texture', 'uv')

    def __init__(self, flags, corner_mask, texture, uv):
        self.flags = flags
        self.corner_mask = corner_mask      # high flag byte, purpose unknown
        self.texture = texture              # one per cell; Drakan had one per triangle
        self.uv = uv                        # {'a':(u,v), 'b':.., 'c':.., 'd':..}

    @property
    def division(self):
        """Which diagonal splits the quad: 0 is b..c, 1 is a..d."""
        return self.flags & 1

    @property
    def is_hole(self):
        return self.texture == NO_TEXTURE

    def triangles(self):
        if self.division:
            return (('a', 'b', 'd'), ('a', 'd', 'c'))
        return (('a', 'b', 'c'), ('b', 'd', 'c'))


class Layer(object):
    __slots__ = ('id', 'width', 'height', 'type', 'origin_x', 'origin_z',
                 'world_height', 'name', 'flags', 'light_direction',
                 'light_ascension', 'light_color', 'ambient_color',
                 'light_dropoff_type', 'visible_layers', 'lightmap_ref',
                 'unknown_ref', 'vertices', 'faces')

    def __init__(self):
        self.vertices = []
        self.faces = []

    @property
    def type_name(self):
        return TYPE_NAMES.get(self.type, 'unknown')

    @property
    def vertex_count(self):
        return (self.width + 1) * (self.height + 1)

    @property
    def face_count(self):
        return self.width * self.height

    @property
    def row_stride(self):
        return self.width + 1

    def height_at(self, col, row):
        return self.world_height + self.vertices[row * self.row_stride + col].height

    def __repr__(self):
        return '<Layer %d %r %s %dx%d @(%d,%d)>' % (
            self.id, self.name, self.type_name, self.width, self.height,
            self.origin_x, self.origin_z)


class Level(object):
    def __init__(self, path, with_terrain=True, with_objects=True):
        self.path = path
        self.srsc = srsc.SRSC(path)
        self.name = ''
        self.grid_width = 0
        self.grid_height = 0
        self.dependencies = []
        self.layers = []
        self.objects = []
        self._read_header()
        if with_terrain:
            self.layers = _read_layers(self.srsc)
        if with_objects:
            self.objects = _read_objects(self.srsc)

    def _read_header(self):
        recs = self.srsc.of_type(T_HEADER)
        if not recs:
            return
        b = self.srsc.body(recs[0])
        self.name, o = read_string(b, 0)
        self.grid_width, self.grid_height = struct.unpack_from('<2I', b, o)
        o += 8
        count = struct.unpack_from('<I', b, o)[0]
        o += 4
        for _ in range(count):
            path, o = read_string(b, o)
            self.dependencies.append(path)

    def __repr__(self):
        return '<Level %r %d layers %d objects>' % (self.name, len(self.layers),
                                                    len(self.objects))


# ---------------------------------------------------------------------------
# 0x0020 object placements
# ---------------------------------------------------------------------------

def _read_objects(archive):
    """Fellowship moved the object id and class reference into a leading table;
    Drakan stored them at the head of each object body."""
    recs = archive.of_type(T_OBJECTS)
    if not recs:
        return []
    b = archive.body(recs[0])
    n = struct.unpack_from('<H', b, 0)[0]
    table = [struct.unpack_from('<IHH', b, 2 + i * 8) for i in range(n)]
    o = 2 + n * 8
    out = []
    for i in range(n):
        obj = LevelObject()
        obj.id, obj.class_id, obj.class_db = table[i]
        obj.layer = struct.unpack_from('<I', b, o)[0]
        o += 4
        obj.position = struct.unpack_from('<3f', b, o)
        o += 12
        obj.flags = struct.unpack_from('<I', b, o)[0]
        o += 4
        if obj.flags & FLAG_EXTRA_FLOAT:
            o += 4
        nlink = struct.unpack_from('<H', b, o)[0]
        o += 2
        if nlink:
            obj.links = struct.unpack_from('<%dI' % nlink, b, o)
            o += 4 * nlink
        obj.rotation = tuple(v % 360 for v in struct.unpack_from('<3H', b, o))
        o += 6
        if obj.flags & FLAG_SCALED:
            obj.scale = struct.unpack_from('<3f', b, o)
            o += 12
        data_dwords, param_count = struct.unpack_from('<2I', b, o)
        o += 8 + 4 * data_dwords + 2 * param_count
        for _ in range(param_count):        # overridden class fields
            o += 4                          # field type
            _name, o = read_string(b, o)
        o += 8                              # two trailing references, unidentified
        out.append(obj)
    return out


# ---------------------------------------------------------------------------
# 0x0001 terrain layers
# ---------------------------------------------------------------------------

class _R(object):
    __slots__ = ('b', 'o')

    def __init__(self, b, o=0):
        self.b, self.o = b, o

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.o)[0]; self.o += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.o)[0]; self.o += 4; return v

    def f32(self):
        v = struct.unpack_from('<f', self.b, self.o)[0]; self.o += 4; return v

    def raw(self, n):
        v = self.b[self.o:self.o + n]; self.o += n; return v

    def string(self):
        n = self.u16()
        s = self.b[self.o:self.o + n]
        self.o += n
        return s.split(b'\x00')[0].decode('latin-1')

    def ref(self):
        return (self.u16(), self.u16())


def _read_layers(archive):
    recs = archive.of_type(T_LAYERS)
    if not recs:
        return []
    body = archive.body(recs[0])
    r = _R(body)
    count = r.u32()
    layers = [_read_layer_def(r) for _ in range(count)]
    r.u32()                                 # compression level, always 1
    for layer in layers:
        size = r.u32()
        raw = r.raw(size)
        try:
            _read_poly_data(layer, zlib.decompress(raw))
        except (zlib.error, ValueError):
            layer.vertices, layer.faces = [], []
    return layers


def _read_layer_def(r):
    L = Layer()
    L.id = r.u32()
    L.width = r.u32()                       # in cells; vertices are width + 1
    L.height = r.u32()
    L.type = r.u32()
    L.origin_x = r.u32()
    L.origin_z = r.u32()
    L.world_height = r.f32()
    L.name = r.string()
    L.flags = r.u32()
    L.light_direction = r.f32()
    L.light_ascension = r.f32()
    b, g, red, _ = r.raw(4)
    L.light_color = (red, g, b)
    b, g, red, _ = r.raw(4)
    L.ambient_color = (red, g, b)
    L.light_dropoff_type = r.u32()
    L.visible_layers = [r.u32() for _ in range(r.u32())]

    # lightmap / texture-blend section, a Fellowship addition
    L.lightmap_ref = r.ref()
    if L.lightmap_ref != (0, 0):
        r.u32()                             # page size
        r.u32(); r.u32(); r.u32(); r.u32()  # atlas rectangle
        for _ in range(r.u32()):
            r.u32(); r.u32(); r.u32(); r.u32()
            for _ in range(r.u32()):
                r.ref()
            w = r.u32(); h = r.u32()
            for _ in range(r.u32()):
                r.raw(w * h)

    r.raw(4 * (1 if L.flags & FLAG_FLAT_VERTEX_COLOR else L.vertex_count))
    L.unknown_ref = r.ref()
    return L


def _read_poly_data(L, buf):
    nv, nf = L.vertex_count, L.face_count
    if len(buf) != nv * VERTEX_STRIDE + nf * FACE_STRIDE:
        raise ValueError('layer %d: unexpected poly block size' % L.id)
    o = 0
    verts = []
    for _ in range(nv):
        t, unk, unk2, hb = struct.unpack_from('<BBHH', buf, o)
        o += VERTEX_STRIDE
        verts.append(Vertex(t, (unk, unk2), (hb - 0x8000) * 2))
    L.vertices = verts

    faces = []
    inv = 1.0 / 65535.0
    for _ in range(nf):
        (flags, cmask, tex, tdb,
         uc, ud, ub, ua, vc, vd, vb, va) = struct.unpack_from('<2B10H', buf, o)
        o += FACE_STRIDE
        faces.append(Face(flags, cmask, (tex, tdb),
                          {'a': (ua * inv, va * inv), 'b': (ub * inv, vb * inv),
                           'c': (uc * inv, vc * inv), 'd': (ud * inv, vd * inv)}))
    L.faces = faces
