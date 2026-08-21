"""Database index - resolves the engine's absolute database ids to files on disk.

Every cross-reference in the game data is a pair `(record_id, database_id)`, where
the database id is a global number declared in that database's plaintext `*.db`
manifest.  Hobbiton is 36, NPCs is 5, World Common is 6, and so on.  A level's
objects reference classes in a dozen different databases, so importing a map means
resolving all of them.

Databases are discovered by walking manifests: each `*.db` lists its dependencies as
paths relative to itself, so starting from one file the whole reachable graph can be
found without knowing the install layout.
"""

import os
import re

from . import model as _model
from . import texture as _texture
from . import klass as _klass


class DatabaseIndex(object):
    def __init__(self, start_path, extra_roots=()):
        self.root = os.path.dirname(os.path.abspath(start_path))
        self.by_id = {}          # database id -> path stem, no extension
        self.local_id = None
        self._models = {}
        self._textures = {}
        self._classes = {}
        seen = set()
        self._walk(self.root, seen, 0)
        for extra in extra_roots:
            path = os.path.normpath(os.path.join(self.root, extra))
            folder = path if os.path.isdir(path) else os.path.dirname(path)
            self._walk(folder, seen, 1)

    # -- discovery -------------------------------------------------------
    def _walk(self, folder, seen, depth):
        folder = os.path.normpath(folder)
        if depth > 6 or folder in seen or not os.path.isdir(folder):
            return
        seen.add(folder)
        for name in _listdir(folder):
            if not name.lower().endswith('.db'):
                continue
            path = os.path.join(folder, name)
            db_id, deps = _read_manifest(path)
            if db_id is None:
                continue
            stem = os.path.splitext(path)[0]
            self.by_id.setdefault(db_id, stem)
            if depth == 0 and self.local_id is None:
                self.local_id = db_id
            for dep in deps:
                self._walk(os.path.join(folder, os.path.dirname(dep)), seen, depth + 1)

    def stem(self, db_id):
        return self.by_id.get(db_id)

    # -- typed accessors, all lazy and cached ----------------------------
    def models(self, db_id):
        return self._open(db_id, '.mdu', self._models, _model.ModelDatabase)

    def textures(self, db_id):
        return self._open(db_id, '.tdu', self._textures, _texture.TextureDatabase)

    def classes(self, db_id):
        return self._open(db_id, '.odu', self._classes, _klass.ClassDatabase)

    def _open(self, db_id, ext, cache, factory):
        if db_id in cache:
            return cache[db_id]
        stem = self.by_id.get(db_id)
        obj = None
        if stem:
            path = _with_ext(stem, ext)
            if path:
                try:
                    obj = factory(path)
                except Exception:
                    obj = None
        cache[db_id] = obj
        return obj

    # -- convenience -----------------------------------------------------
    def load_texture(self, tex_id, db_id):
        db = self.textures(db_id)
        if db is not None:
            tex = db.load(tex_id)
            if tex is not None:
                return tex
        if self.local_id is not None and db_id != self.local_id:
            db = self.textures(self.local_id)
            if db is not None:
                return db.load(tex_id)
        return None

    # same signature as TextureResolver.load, so either can be handed to the
    # material cache
    load = load_texture

    def load_class(self, class_id, db_id):
        db = self.classes(db_id)
        return db.load(class_id) if db is not None else None

    def load_model(self, model_id, db_id):
        db = self.models(db_id)
        if db is None or model_id not in db.ids:
            return None
        return db.load(model_id)

    def describe(self):
        return sorted((i, os.path.basename(p)) for i, p in self.by_id.items())


def _listdir(folder):
    try:
        return sorted(os.listdir(folder))
    except OSError:
        return []


def _with_ext(stem, ext):
    """Match a sibling file case-insensitively; game data mixes .MDU and .mdu."""
    direct = stem + ext
    if os.path.exists(direct):
        return direct
    folder = os.path.dirname(stem)
    want = os.path.basename(stem).lower() + ext
    for name in _listdir(folder):
        if name.lower() == want:
            return os.path.join(folder, name)
    return None


def _read_manifest(path):
    """A *.db manifest is plaintext: version / id / dependencies / paths."""
    try:
        with open(path, 'r', errors='replace') as f:
            lines = [l.strip() for l in f if l.strip()]
    except OSError:
        return None, []
    db_id, deps = None, []
    for i, line in enumerate(lines):
        m = re.match(r'^id\s+(\d+)$', line, re.I)
        if m:
            db_id = int(m.group(1))
        m = re.match(r'^dependencies\s+(\d+)$', line, re.I)
        if m:
            deps = lines[i + 1:i + 1 + int(m.group(1))]
    return db_id, [d.replace('\\', os.sep) for d in deps]
