"""Writing side of the Riot Engine formats: SRSC archives and texture records.

The container is rebuilt rather than patched, because a re-encoded record can
change size.  Every record the caller does not replace is copied through
verbatim, so a rebuild with no replacements produces a byte-identical file -
which is the first thing the test suite checks.

Textures are re-encoded into the original record's format: same bit depth, same
palette size, same colour key, same flags.  Only the palette and the pixels
change, so a same-size edit leaves the record length untouched and the rest of
the archive byte-for-byte as it was.
"""

import struct

from . import srsc
from .texture import HEADER_SIZE, NO_COLOUR_KEY


class WriteError(Exception):
    pass


# ---------------------------------------------------------------------------
# container
# ---------------------------------------------------------------------------

ALIGNMENT = 8      # every record body in the retail data starts on an 8-byte boundary


def rebuild_archive(path, replacements, out_path=None, drop=(), add=()):
    """Write `path` again with record bodies replaced, removed or added.

    `replacements` maps a record index (its position in the directory) to new
    bytes; `drop` is a set of indices to leave out entirely; `add` is a list of
    (type, id, group, body) to append, each of which lands in the directory
    directly after the last surviving record with the same id, so a model's
    records stay together in the order the engine wrote them.  Record order,
    types, ids and groups are otherwise preserved exactly, existing bodies are
    written in their original order, and each is aligned the way the engine
    aligns them - so rebuilding with no changes reproduces the input file byte
    for byte.
    """
    archive = srsc.SRSC(path)
    drop = set(drop)
    keep = [i for i in range(len(archive.records)) if i not in drop]
    order = sorted(keep, key=lambda i: archive.records[i].offset)
    bodies = {}
    for i in keep:
        bodies[i] = replacements.get(i, archive.body(archive.records[i]))

    # directory entries as (type, id, group, body), in their final order
    entries = [(archive.records[i].type, archive.records[i].id,
                archive.records[i].group, i) for i in keep]
    for kind, rid, group, body in add:
        after = max((k for k, e in enumerate(entries) if e[1] == rid), default=None)
        item = (kind, rid, group, body)
        if after is None:
            entries.append(item)
        else:
            entries.insert(after + 1, item)

    out = bytearray(b'SRSC')
    out += struct.pack('<HIH', archive.version, 0, len(entries))
    _pad(out)

    offsets = {}
    for i in order:
        offsets[i] = len(out)
        out += bodies[i]
        _pad(out)
    new_at = {}
    for k, (_t, _i, _g, body) in enumerate(entries):
        if not isinstance(body, int):
            new_at[k] = len(out)
            out += body
            _pad(out)

    directory_offset = len(out)
    for k, (kind, rid, group, body) in enumerate(entries):
        if isinstance(body, int):
            at, size = offsets[body], len(bodies[body])
        else:
            at, size = new_at[k], len(body)
        out += struct.pack('<HHHII', kind, rid, group, at, size)

    struct.pack_into('<I', out, 6, directory_offset)

    target = out_path or path
    with open(target, 'wb') as f:
        f.write(bytes(out))
    return target, len(out)


def _pad(buf):
    while len(buf) % ALIGNMENT:
        buf.append(0)


def patch_records(path, replacements, out_path=None):
    """Overwrite record bodies in place, leaving every other byte alone.

    Only valid when each replacement is exactly the size of the record it
    replaces, which is the normal case for a texture edited at its original
    resolution.  Nothing moves, so the directory and every untouched record stay
    bit-identical.  Returns False if any size differs.
    """
    archive = srsc.SRSC(path)
    for i, body in replacements.items():
        if len(body) != archive.records[i].size:
            return False
    data = bytearray(open(path, 'rb').read())
    for i, body in replacements.items():
        rec = archive.records[i]
        data[rec.offset:rec.offset + rec.size] = body
    target = out_path or path
    with open(target, 'wb') as f:
        f.write(bytes(data))
    return True


# ---------------------------------------------------------------------------
# palette quantisation
# ---------------------------------------------------------------------------

def quantise(pixels, max_colours, reserved_index=None, reserved_colour=None):
    """Reduce a list of RGBA tuples to a palette plus indices.

    Quantisation runs on all four channels, not just colour.  A palette that
    ignores alpha merges a texture's transparent black with its opaque black,
    which silently fills in every cut-out hole - the exact failure that makes
    foliage and hair render as solid cards.
    """
    counts = {}
    for px in pixels:
        counts[px] = counts.get(px, 0) + 1

    slots = max_colours - (1 if reserved_index is not None else 0)
    if len(counts) <= slots:
        palette = sorted(counts, key=lambda c: -counts[c])
    else:
        palette = _median_cut(counts, slots)

    if reserved_index is not None:
        palette = [c for c in palette if c != reserved_colour]
        # the reserved slot has to land on its exact index, so pad up to it
        # first rather than inserting into a list that is still too short
        while len(palette) < reserved_index:
            palette.append((0, 0, 0, 255))
        palette.insert(reserved_index, reserved_colour)
    while len(palette) < max_colours:
        palette.append((0, 0, 0, 255))
    palette = palette[:max_colours]

    lookup = {c: i for i, c in enumerate(palette)}
    indices = bytearray(len(pixels))
    cache = {}
    for p, px in enumerate(pixels):
        hit = lookup.get(px)
        if hit is None:
            hit = cache.get(px)
            if hit is None:
                hit = _nearest(px, palette)
                cache[px] = hit
        indices[p] = hit
    return palette, indices


def _median_cut(counts, want):
    """Median cut over RGBA; alpha is weighted so it never merges away."""
    boxes = [list(counts.items())]
    while len(boxes) < want:
        target, axis, spread = None, 0, -1
        for box in boxes:
            if len(box) < 2:
                continue
            for a in range(4):
                lo = min(c[a] for c, _n in box)
                hi = max(c[a] for c, _n in box)
                width = (hi - lo) * (4 if a == 3 else 1)
                if width > spread:
                    target, axis, spread = box, a, width
        if target is None:
            break
        target.sort(key=lambda item: item[0][axis])
        half = _weighted_middle(target)
        boxes.remove(target)
        boxes.append(target[:half])
        boxes.append(target[half:])

    palette = []
    for box in boxes:
        total = sum(n for _c, n in box) or 1
        palette.append(tuple(
            min(255, max(0, int(round(sum(c[a] * n for c, n in box) / total))))
            for a in range(4)))
    return palette


def _weighted_middle(box):
    total = sum(n for _c, n in box)
    run = 0
    for i, (_c, n) in enumerate(box):
        run += n
        if run * 2 >= total:
            return max(1, min(i + 1, len(box) - 1))
    return max(1, len(box) // 2)


def _nearest(px, palette):
    r, g, b, a = px
    best, best_d = 0, None
    for i, c in enumerate(palette):
        dr, dg, db, da = r - c[0], g - c[1], b - c[2], a - c[3]
        d = dr * dr * 3 + dg * dg * 6 + db * db + da * da * 24
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


# ---------------------------------------------------------------------------
# texture records
# ---------------------------------------------------------------------------

def encode_texture(original_body, rgba, width, height):
    """Re-encode a 0x0040 record with new pixels, keeping its format.

    Every header field is copied from the original except the size fields, so
    bit depth, colour key, mip and alternate references and all the flags
    survive untouched.  The header is not a fixed length: a palettised record
    has 80 bytes before its palette, a direct-colour one has 64 before its
    pixels, so the split is derived from the original rather than assumed.
    """
    b = original_body
    if len(b) < 64:
        raise WriteError('texture record is too short to rewrite')
    ow, oh, opitch, bpp = struct.unpack_from('<4I', b, 0)
    alpha_bits, colour_key = struct.unpack_from('<2I', b, 16)
    if width * height * 4 != len(rgba):
        raise WriteError('image data does not match %dx%d' % (width, height))

    span = opitch * oh
    if bpp == 8:
        header_len = HEADER_SIZE
        palette_bytes = len(b) - span - header_len
        entries = palette_bytes // 4
        if entries < 2:
            raise WriteError('original palette is unusable')
    else:
        header_len = len(b) - span              # 64 in the retail data
        entries = 0
    if header_len < 64:
        raise WriteError('cannot locate the end of the texture header')
    header = bytearray(b[:header_len])

    if bpp == 8:
        keyed = colour_key != NO_COLOUR_KEY and colour_key < entries
        # A keyed texture carries transparency as "this palette index is the
        # hole", not as an alpha channel, so transparent pixels must all land on
        # that one index and nothing else may use it.
        pixels = []
        for p in range(width * height):
            r, g, bl, a = rgba[p * 4:p * 4 + 4]
            if alpha_bits == 0:
                a = 255                          # the alpha byte is padding here
            elif alpha_bits == 1:
                a = 255 if a >= 128 else 0
            pixels.append((r, g, bl, a))

        if keyed:
            pal = b[header_len:len(b) - span]
            k = colour_key * 4
            key_colour = (pal[k + 2], pal[k + 1], pal[k], 255)
            solid = [px for i, px in enumerate(pixels)
                     if rgba[i * 4 + 3] >= 128]
            palette, _ = quantise(solid or [key_colour], entries,
                                  colour_key, key_colour)
            lookup = {c: i for i, c in enumerate(palette)}
            cache = {}
            indices = bytearray(width * height)
            for p, px in enumerate(pixels):
                if rgba[p * 4 + 3] < 128:
                    indices[p] = colour_key
                    continue
                hit = lookup.get(px)
                if hit is None or hit == colour_key:
                    hit = cache.get(px)
                    if hit is None:
                        hit = _nearest_excluding(px, palette, colour_key)
                        cache[px] = hit
                indices[p] = hit
            alpha = [127] * entries              # padding, exactly as shipped
        else:
            palette, indices = quantise(pixels, entries)
            alpha = []
            for c in palette:
                alpha.append(127 if alpha_bits == 0 else c[3])

        out = bytearray()
        for i, c in enumerate(palette):
            out += bytes((c[2], c[1], c[0], alpha[i]))
        pitch = width
        body = bytes(header) + bytes(out) + bytes(indices)

    elif bpp == 32:
        pitch = width * 4
        px = bytearray(width * height * 4)
        for p in range(width * height):
            r, g, bl, a = rgba[p * 4:p * 4 + 4]
            px[p * 4:p * 4 + 4] = bytes((bl, g, r, a if alpha_bits else 255))
        body = bytes(header) + bytes(px)

    elif bpp == 24:
        pitch = width * 3
        px = bytearray(width * height * 3)
        for p in range(width * height):
            r, g, bl = rgba[p * 4:p * 4 + 3]
            px[p * 3:p * 3 + 3] = bytes((bl, g, r))
        body = bytes(header) + bytes(px)

    elif bpp == 16:
        pitch = width * 2
        px = bytearray(width * height * 2)
        for p in range(width * height):
            r, g, bl, a = rgba[p * 4:p * 4 + 4]
            if alpha_bits == 4:
                v = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (bl >> 4)
            elif alpha_bits == 1:
                v = (0x8000 if a >= 128 else 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (bl >> 3)
            else:
                v = ((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3)
            px[p * 2] = v & 0xFF
            px[p * 2 + 1] = (v >> 8) & 0xFF
        body = bytes(header) + bytes(px)

    else:
        raise WriteError('unsupported bit depth %d' % bpp)

    out = bytearray(body)
    struct.pack_into('<4I', out, 0, width, height, pitch, bpp)
    return bytes(out)


def _nearest_excluding(px, palette, banned):
    r, g, b, a = px
    best, best_d = 0, None
    for i, c in enumerate(palette):
        if i == banned:
            continue
        dr, dg, db, da = r - c[0], g - c[1], b - c[2], a - c[3]
        d = dr * dr * 3 + dg * dg * 6 + db * db + da * da * 24
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best
