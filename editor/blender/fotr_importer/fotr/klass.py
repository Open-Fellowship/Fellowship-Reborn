"""Class database (*.odu) reader.

A level places *classes*, not models: each placed object names a class, and the
class says which model to draw.  Non-visual classes (sound emitters, triggers,
AI nodes, cameras) either reference no model at all or reference a marker model.

Only the head of the 0x0102 record is decoded here, which is all that is needed
to reach the model.  The trailing field-definition block is left alone.
"""

import struct
from . import srsc
from .srsc import read_string

T_CLASS = 0x0102
T_CLASS_GROUP = 0x0103


class GameClass(object):
    __slots__ = ('id', 'name', 'author', 'model', 'db')

    def __init__(self):
        self.id = 0
        self.name = ''
        self.author = ''
        self.model = None       # (model_id, database_id) or None
        self.db = 0

    def __repr__(self):
        return '<Class %d %r model=%s>' % (self.id, self.name, self.model)


class ClassDatabase(object):
    def __init__(self, path):
        self.srsc = srsc.SRSC(path)
        self.path = path
        self.records = {r.id: r for r in self.srsc.of_type(T_CLASS)}
        self.ids = sorted(self.records)
        self._cache = {}

    def load(self, class_id):
        if class_id in self._cache:
            return self._cache[class_id]
        rec = self.records.get(class_id)
        c = None
        if rec is not None:
            b = self.srsc.body(rec)
            try:
                c = GameClass()
                c.id = class_id
                c.name, o = read_string(b, 0)
                o += 4                       # unknown, always 2
                o += 8                       # Windows FILETIME
                c.author, o = read_string(b, o)
                model_id, model_db = struct.unpack_from('<2H', b, o)
                c.model = (model_id, model_db) if model_id else None
            except (struct.error, IndexError):
                c = None
        self._cache[class_id] = c
        return c

    def listing(self):
        return [(cid, (self.load(cid).name if self.load(cid) else '?'))
                for cid in self.ids]
