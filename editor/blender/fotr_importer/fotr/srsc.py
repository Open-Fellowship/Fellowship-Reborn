"""SRSC container reader - Surreal Software Riot Engine resource archive.

Header (12 bytes) + raw record bodies + a directory table at the end.
Fellowship of the Ring uses version 0x0101; Drakan used 0x0100.
"""

import struct
import os

# record types we care about
T_TEXTURE      = 0x0040
T_TEXTURE_INFO = 0x0041
T_MODEL_NAME   = 0x0200
T_MODEL_VERTS  = 0x0203
T_MODEL_POLYS  = 0x0204
T_MODEL_BOUNDS = 0x0207
T_MODEL_CHAR   = 0x0208
T_MODEL_UNK11  = 0x0211
T_ANIM_INFO    = 0x0501
T_ANIM_FRAMES  = 0x0502
T_ANIM_EVENTS  = 0x0505

TYPE_NAMES = {
    0x0052: 'terrain lighting', 0x0060: 'procedural texture', 0x0080: 'group name',
    T_TEXTURE: 'texture', T_TEXTURE_INFO: 'texture info',
    0x0102: 'class', 0x0103: 'class group',
    T_MODEL_NAME: 'model header', T_MODEL_VERTS: 'vertices', T_MODEL_POLYS: 'polygons',
    T_MODEL_BOUNDS: 'bounds', T_MODEL_CHAR: 'character/skeleton', T_MODEL_UNK11: 'unknown 0x0211',
    0x0301: 'sound group', 0x0302: 'sound', 0x0311: 'sequence',
    0x0342: 'voice info', 0x0343: 'voice data',
    0x0400: 'string', 0x0401: 'string (plain)', 0x0402: 'version',
    T_ANIM_INFO: 'animation info', T_ANIM_FRAMES: 'animation frames',
    T_ANIM_EVENTS: 'animation events',
}


class SRSCError(Exception):
    pass


class SRSC(object):
    """Random-access reader over an SRSC archive."""

    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            head = f.read(12)
            if len(head) < 12 or head[:4] != b'SRSC':
                raise SRSCError('%s is not an SRSC archive' % os.path.basename(path))
            self.version, dir_offset, count = struct.unpack('<HIH', head[4:12])
            f.seek(dir_offset)
            raw = f.read(count * 14)
            self._data = None
            self._path = path
        if len(raw) < count * 14:
            raise SRSCError('truncated directory in %s' % os.path.basename(path))

        self.records = []
        for i in range(count):
            t, rid, gid, off, size = struct.unpack_from('<HHHII', raw, i * 14)
            self.records.append(Record(i, t, rid, gid, off, size))

        self._by_type = {}
        for r in self.records:
            self._by_type.setdefault(r.type, []).append(r)

    # -- access ---------------------------------------------------------
    def _blob(self):
        if self._data is None:
            with open(self._path, 'rb') as f:
                self._data = f.read()
        return self._data

    def body(self, rec):
        d = self._blob()
        return d[rec.offset:rec.offset + rec.size]

    def of_type(self, t):
        return self._by_type.get(t, [])

    def census(self):
        return sorted((t, len(v)) for t, v in self._by_type.items())


class Record(object):
    __slots__ = ('index', 'type', 'id', 'group', 'offset', 'size')

    def __init__(self, index, type_, id_, group, offset, size):
        self.index, self.type, self.id = index, type_, id_
        self.group, self.offset, self.size = group, offset, size

    def __repr__(self):
        return '<rec %d type=0x%04x id=%d size=%d>' % (self.index, self.type, self.id, self.size)


# -- primitives used by every record parser -----------------------------

def read_string(buf, off):
    """uint16 length prefix, ASCII, NUL padded to an even length."""
    n = struct.unpack_from('<H', buf, off)[0]
    s = buf[off + 2:off + 2 + n].split(b'\x00')[0].decode('latin-1')
    return s, off + 2 + n
