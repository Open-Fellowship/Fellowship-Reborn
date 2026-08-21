"""Texture (*.tdu) reader plus a dependency-aware texture resolver.

Fellowship stores textures uncompressed, unlike Drakan which zlib-compresses them.
An 8-bit record is an 80-byte header, then a BGRA palette, then the pixels.  The
palette is usually 256 entries but foliage textures often use 128, and the header
field that looks like an entry count is unreliable - so the pixels are read from
the end of the record and the palette is whatever sits in the gap.

The palette's alpha byte only carries meaning when the header declares alpha bits;
otherwise every entry holds a meaningless 0x7f.  Foliage uses 1-bit cutout alpha,
cobwebs and glass use 8-bit.
"""

import os
import re
import struct
import zlib
from . import srsc
from .srsc import read_string


HEADER_SIZE = 80


# How a texture's transparency should be rendered.  The distinction matters:
# a cutout is binary and must be clipped, while genuinely graduated alpha has to
# be blended - and blending a cutout produces depth-sorting artefacts on dense
# overlapping geometry like hair, beards and foliage.
ALPHA_NONE, ALPHA_CLIP, ALPHA_BLEND = 'none', 'clip', 'blend'


class Texture(object):
    __slots__ = ('id', 'name', 'source', 'width', 'height', 'rgba',
                 'has_alpha', 'alpha_mode')

    def __init__(self):
        self.id = 0
        self.name = ''
        self.source = ''
        self.width = 0
        self.height = 0
        self.rgba = b''         # top-down RGBA8
        self.has_alpha = False
        self.alpha_mode = ALPHA_NONE


def _decode_16(raw, width, height, pitch, alpha_bits):
    """16-bit pixels: 1555 when one alpha bit is declared, 4444 when four,
    plain 565 when none."""
    out = bytearray(width * height * 4)
    for y in range(height):
        src, base = y * pitch, y * width * 4
        for x in range(width):
            v = raw[src + x * 2] | (raw[src + x * 2 + 1] << 8)
            if alpha_bits == 4:
                a = ((v >> 12) & 15) * 17
                r = ((v >> 8) & 15) * 17
                g = ((v >> 4) & 15) * 17
                bl = (v & 15) * 17
            elif alpha_bits == 1:
                a = 255 if v & 0x8000 else 0
                r = _E5[(v >> 10) & 31]
                g = _E5[(v >> 5) & 31]
                bl = _E5[v & 31]
            else:
                a = 255
                r = _E5[(v >> 11) & 31]
                g = _E6[(v >> 5) & 63]
                bl = _E5[v & 31]
            out[base + x * 4:base + x * 4 + 4] = bytes((r, g, bl, a))
    return out


_E5 = [(i * 255 + 15) // 31 for i in range(32)]
_E6 = [(i * 255 + 31) // 63 for i in range(64)]


NO_COLOUR_KEY = 0xFFFFFFFF


def _palette_lut(pal, entries, alpha_bits, colour_key):
    """Palette is BGRA.

    The alpha byte only means anything when the header declares alpha bits;
    otherwise every entry carries a meaningless 0x7f.  One-bit alpha is a cutout
    mask, so it is thresholded rather than blended.

    Separately, a colour key names a palette *index* to punch out.  This is how
    all the foliage works: leaf cards are drawn on palette index 0 and keyed, so
    without this every tree renders as a black rectangle.
    """
    lut = bytearray(1024)
    for i in range(min(entries, 256)):
        bl, g, r, a = pal[i * 4:i * 4 + 4]
        if alpha_bits == 0:
            a = 255
        elif alpha_bits == 1:
            a = 255 if a >= 128 else 0
        if colour_key != NO_COLOUR_KEY and i == colour_key:
            a = 0
        lut[i * 4:i * 4 + 4] = bytes((r, g, bl, a))
    return lut


class TextureDatabase(object):
    def __init__(self, path):
        self.srsc = srsc.SRSC(path)
        self.path = path
        self.pixels = {r.id: r for r in self.srsc.of_type(srsc.T_TEXTURE)}
        self.infos = {r.id: r for r in self.srsc.of_type(srsc.T_TEXTURE_INFO)}
        self.ids = sorted(self.pixels)

    def name_of(self, tid):
        rec = self.infos.get(tid)
        if rec is None:
            return 'texture_%d' % tid
        return read_string(self.srsc.body(rec), 0)[0]

    def load(self, tid):
        """Decode a 0x0040 texture record.

        The 80-byte header is followed, for 8-bit images, by the palette, and
        then by the pixels.  Reading the pixels from the END of the record and
        the palette from the gap in between is exact and self-checking: the
        palette is not always 256 entries (foliage often uses 128), and the
        header field that looks like an entry count is unreliable.
        """
        rec = self.pixels.get(tid)
        if rec is None:
            return None
        b = self.srsc.body(rec)
        if len(b) < HEADER_SIZE:
            return None
        width, height, pitch, bpp = struct.unpack_from('<4I', b, 0)
        alpha_bits, colour_key = struct.unpack_from('<2I', b, 16)
        if not width or not height:
            return None
        span = pitch * height
        if span <= 0 or span > len(b):
            return None
        raw = b[len(b) - span:]

        if bpp == 8:
            pal = b[HEADER_SIZE:len(b) - span]
            entries = len(pal) // 4
            if entries < 2:
                return None
            lut = _palette_lut(pal, entries, alpha_bits, colour_key)
            out = bytearray(width * height * 4)
            for y in range(height):
                row = raw[y * pitch:y * pitch + width]
                base = y * width * 4
                for x in range(width):
                    idx = row[x] * 4
                    out[base + x * 4:base + x * 4 + 4] = lut[idx:idx + 4]
        elif bpp == 32:
            out = bytearray(width * height * 4)
            opaque = alpha_bits == 0
            for y in range(height):
                src, base = y * pitch, y * width * 4
                for x in range(width):
                    bl, g, r, a = raw[src + x * 4:src + x * 4 + 4]
                    out[base + x * 4:base + x * 4 + 4] = bytes(
                        (r, g, bl, 255 if opaque else a))
        elif bpp == 16:
            out = _decode_16(raw, width, height, pitch, alpha_bits)
        elif bpp == 24:
            out = bytearray(width * height * 4)
            for y in range(height):
                src, base = y * pitch, y * width * 4
                for x in range(width):
                    bl, g, r = raw[src + x * 3:src + x * 3 + 3]
                    out[base + x * 4:base + x * 4 + 4] = bytes((r, g, bl, 255))
        else:
            return None

        t = Texture()
        t.id = tid
        t.name = self.name_of(tid)
        t.width, t.height = width, height
        t.rgba = bytes(out)
        keyed = bpp == 8 and colour_key != NO_COLOUR_KEY
        if alpha_bits >= 4:
            t.alpha_mode = ALPHA_BLEND      # 4- or 8-bit graduated alpha
        elif alpha_bits == 1 or keyed:
            t.alpha_mode = ALPHA_CLIP       # binary cutout
        t.has_alpha = t.alpha_mode != ALPHA_NONE
        return t


class TextureResolver(object):
    """Maps a model's (texture_id, database_id) references onto real *.tdu files.

    A model normally references textures in its own folder, but can point at a
    sibling database listed in the folder's *.db manifest.  Databases are found by
    walking the manifests, so importing a level model picks up shared world textures.
    """

    def __init__(self, mdu_path):
        self.root = os.path.dirname(os.path.abspath(mdu_path))
        self.self_db_id = None
        self._by_db_id = {}
        self._open = {}
        self._scan(self.root, depth=0)

    # -- manifest walking -----------------------------------------------
    def _scan(self, folder, depth):
        if depth > 3 or not os.path.isdir(folder):
            return
        for db in _db_files(folder):
            try:
                with open(db, 'r', errors='replace') as f:
                    text = f.read()
            except OSError:
                continue
            db_id, deps = _parse_manifest(text)
            base = os.path.splitext(db)[0]
            tdu = base + '.tdu'
            if db_id is not None and os.path.exists(tdu):
                self._by_db_id.setdefault(db_id, tdu)
                if depth == 0 and self.self_db_id is None:
                    self.self_db_id = db_id
            for dep in deps:
                nxt = os.path.normpath(os.path.join(folder, os.path.dirname(dep)))
                if nxt != folder:
                    self._scan(nxt, depth + 1)

    # -- lookup ---------------------------------------------------------
    def database_for(self, db_id):
        path = self._by_db_id.get(db_id)
        if path is None:
            return None
        if path not in self._open:
            try:
                self._open[path] = TextureDatabase(path)
            except Exception:
                self._open[path] = None
        return self._open[path]

    def load(self, tex_id, db_id):
        for candidate in (db_id, self.self_db_id):
            db = self.database_for(candidate) if candidate is not None else None
            if db is not None:
                t = db.load(tex_id)
                if t is not None:
                    return t
        for db in list(self._by_db_id):          # last resort: brute force
            d = self.database_for(db)
            if d is not None:
                t = d.load(tex_id)
                if t is not None:
                    return t
        return None


def _db_files(folder):
    try:
        return [os.path.join(folder, n) for n in sorted(os.listdir(folder))
                if n.lower().endswith('.db')]
    except OSError:
        return []


def _parse_manifest(text):
    """*.db manifests are plaintext: version / id / dependencies / relative paths."""
    db_id = None
    deps = []
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    for i, line in enumerate(lines):
        m = re.match(r'^id\s+(\d+)$', line, re.I)
        if m:
            db_id = int(m.group(1))
        m = re.match(r'^dependencies\s+(\d+)$', line, re.I)
        if m:
            deps = lines[i + 1:i + 1 + int(m.group(1))]
    return db_id, [d.replace('\\', os.sep) for d in deps]


# -- PNG output (Blender ships zlib but no image encoder we can rely on) --

def write_png(path, width, height, rgba):
    """Minimal RGBA8 PNG writer."""
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)                                   # filter type: none
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(bytes(raw), 6))
           + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(png)
    return path
