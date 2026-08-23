"""Animation (*.adu) reader.

0x0501  info    - name, source, author, duration, loop flag, node/channel counts
0x0502  frames  - a per-joint table followed by a flat keyframe area
0x0505  events  - timed sound/effect references (footsteps and the like)

Keyframe area layout (this is where Fellowship diverges from Drakan, which stored
raw 3x4 matrices):

    table entry, 24 bytes per joint:
        uint32 byte_offset          into the keyframe area
        uint32 keyframe_count
        float  flag                 negative zero => keyframes carry a translation
        float  rest_x, rest_y, rest_z

    keyframe:
        float time
        float quat_x, quat_y, quat_z, quat_w
        float trans_x, trans_y, trans_z     only when the joint's flag is set

Joint index in the table is the model's joint index, so it lines up directly with
the joints parsed out of the model's 0x0208 record.
"""

import math
import struct
from . import srsc
from .srsc import read_string


class Keyframe(object):
    __slots__ = ('time', 'rotation', 'translation')

    def __init__(self, time, rotation, translation):
        self.time = time
        self.rotation = rotation            # (x, y, z, w), normalised
        self.translation = translation      # (x, y, z) or None


class JointTrack(object):
    __slots__ = ('joint', 'rest', 'keyframes')

    def __init__(self, joint, rest, keyframes):
        self.joint = joint
        self.rest = rest                    # local rest translation
        self.keyframes = keyframes


class Animation(object):
    def __init__(self):
        self.id = 0
        self.name = ''
        self.source = ''
        self.author = ''
        self.duration = 0.0
        self.looping = True
        self.node_count = 0
        self.channel_count = 0
        self.tracks = []                    # [JointTrack], one per joint index

    def __repr__(self):
        return '<Animation %s %.2fs %d joints>' % (self.name, self.duration, len(self.tracks))


class AnimationDatabase(object):
    def __init__(self, path):
        self.srsc = srsc.SRSC(path)
        self.path = path
        self.infos = {r.id: r for r in self.srsc.of_type(srsc.T_ANIM_INFO)}
        self.frames = {r.id: r for r in self.srsc.of_type(srsc.T_ANIM_FRAMES)}
        self.ids = sorted(self.infos)
        self._names = {}

    def name_of(self, aid):
        if aid not in self._names:
            rec = self.infos.get(aid)
            self._names[aid] = read_string(self.srsc.body(rec), 0)[0] if rec else ''
        return self._names[aid]

    def listing(self):
        return [(aid, self.name_of(aid)) for aid in self.ids]

    def load(self, aid):
        info = self.infos.get(aid)
        if info is None:
            return None
        a = Animation()
        a.id = aid
        b = self.srsc.body(info)
        a.name, o = read_string(b, 0)
        a.source, o = read_string(b, o)
        o += 8
        a.author, o = read_string(b, o)
        o += 4                                          # model/skeleton reference
        if o + 16 <= len(b):
            a.duration, flags, a.node_count, a.channel_count = struct.unpack_from('<f3I', b, o)
            a.looping = not (flags & 0x01)

        frec = self.frames.get(aid)
        if frec is not None:
            a.tracks = _read_frames(self.srsc.body(frec))
        return a


def _read_frames(b):
    if len(b) < 4:
        return []
    count = struct.unpack_from('<I', b, 0)[0]
    table_end = 4 + count * 24
    if table_end + 4 > len(b):
        return []
    base = table_end + 4
    tracks = []
    for i in range(count):
        off, n, flag, rx, ry, rz = struct.unpack_from('<2I4f', b, 4 + i * 24)
        has_translation = math.copysign(1.0, flag) < 0
        stride = 32 if has_translation else 20
        keys = []
        for k in range(n):
            p = base + off + k * stride
            if p + stride > len(b):
                break
            if has_translation:
                d = struct.unpack_from('<8f', b, p)
                keys.append(Keyframe(d[0], _unit(d[1:5]), d[5:8]))
            else:
                d = struct.unpack_from('<5f', b, p)
                keys.append(Keyframe(d[0], _unit(d[1:5]), None))
        tracks.append(JointTrack(i, (rx, ry, rz), keys))
    return tracks


def _unit(q):
    n = math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3])
    if n < 1e-8:
        return (0.0, 0.0, 0.0, 1.0)
    return (q[0] / n, q[1] / n, q[2] / n, q[3] / n)


def find_database(mdu_path):
    """The *.adu that sits next to a *.mdu, if there is one."""
    import os
    base = os.path.splitext(mdu_path)[0]
    for candidate in (base + '.adu', base + 's.adu'):
        if os.path.exists(candidate):
            return candidate
    folder = os.path.dirname(mdu_path)
    try:
        for n in sorted(os.listdir(folder)):
            if n.lower().endswith('.adu'):
                return os.path.join(folder, n)
    except OSError:
        pass
    return None
