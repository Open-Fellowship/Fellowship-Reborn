#!/usr/bin/env python3
"""
matchtool.py - prove that a compiled function matches the original, byte for byte.

The problem this solves: you cannot diff a freshly compiled .obj against the
shipped image directly. A `call rel32` in the original points at a linked
address; in the .obj the same field is a placeholder the linker has not filled
in yet, accompanied by a relocation record. Diff them raw and every call site,
every global reference and every string address reads as a mismatch.

So this reads the .obj's relocation table, and blanks those operand fields on
BOTH sides before comparing. What is left is pure code generation: register
allocation, instruction selection, scheduling, stack layout. That is the part
the compiler and its switches decide, and the part that has to match.

  matchtool.py image   <pe> <va> <size>
  matchtool.py obj     <objfile> [symbol]
  matchtool.py compare <pe> <va> <objfile> <symbol>

Sizes and addresses accept 0x hex or decimal. No third-party packages.
"""

import struct
import sys

# ---- i386 relocation types. All of them patch a 4-byte field at r_vaddr. ----
REL_I386 = {
    0x0006: "DIR32",     # 32-bit absolute VA      - globals, string addresses
    0x0007: "DIR32NB",   # 32-bit RVA
    0x0009: "SEG12",
    0x000A: "SECTION",
    0x000B: "SECREL",
    0x000C: "TOKEN",
    0x000D: "SECREL7",
    0x0014: "REL32",     # 32-bit PC-relative      - call, jmp
}
RELOC_WIDTH = 4

IMAGE_SCN_LNK_COMDAT = 0x00001000


def parse_int(text):
    return int(text, 16) if text.lower().startswith("0x") else int(text, 0)


# ------------------------------------------------------------------ PE (image)

def pe_sections(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("no PE signature")
    coff = e_lfanew + 4
    nsections = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    image_base = (struct.unpack_from("<I", data, opt + 28)[0] if magic == 0x10B
                  else struct.unpack_from("<Q", data, opt + 24)[0])

    sections = []
    sec = opt + opt_size
    for i in range(nsections):
        base = sec + i * 40
        name = data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((name, vaddr, vsize, rawptr, rawsize))
    return image_base, sections


def image_bytes(data, va, size):
    """Pull `size` bytes at virtual address `va` out of a PE file image."""
    image_base, sections = pe_sections(data)
    rva = va - image_base
    for name, vaddr, vsize, rawptr, rawsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            off = rawptr + (rva - vaddr)
            if off + size > len(data):
                raise ValueError(f"{size} bytes at {va:#x} runs past end of file")
            return name, off, data[off:off + size]
    raise ValueError(f"va {va:#x} (rva {rva:#x}) is not inside any section")


# ------------------------------------------------------------------ COFF (.obj)

def coff_parse(data):
    machine, nsections, _, symptr, nsyms = struct.unpack_from("<HHIII", data, 0)
    if machine != 0x014C:
        raise ValueError(f"not an i386 object (machine {machine:#06x})")

    sections = []
    for i in range(nsections):
        base = 20 + i * 40
        raw_name = data[base:base + 8].rstrip(b"\0")
        vsize, vaddr, rawsize, rawptr, relptr, _, nrel, _ = struct.unpack_from(
            "<IIIIIIHH", data, base + 8
        )
        chars = struct.unpack_from("<I", data, base + 36)[0]
        sections.append({
            "name": raw_name.decode("ascii", "replace"),
            "size": rawsize, "rawptr": rawptr,
            "relptr": relptr, "nrel": nrel, "chars": chars,
        })

    # The string table sits right after the symbol table and holds any name
    # longer than 8 characters - which is most decorated C++ symbols.
    strtab_off = symptr + nsyms * 18
    strtab = data[strtab_off:] if strtab_off < len(data) else b""

    def sym_name(base):
        if struct.unpack_from("<I", data, base)[0] == 0:
            off = struct.unpack_from("<I", data, base + 4)[0]
            return strtab[off:strtab.index(b"\0", off)].decode("ascii", "replace")
        return data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")

    symbols, i = [], 0
    while i < nsyms:
        base = symptr + i * 18
        # Name occupies bytes 0-7; Value starts at byte 8.
        value, secnum, _, storage, naux = struct.unpack_from("<IhHBB", data, base + 8)
        symbols.append({
            "name": sym_name(base), "value": value,
            "section": secnum, "storage": storage,
        })
        i += 1 + naux

    return sections, symbols


def coff_relocs(data, section):
    out = []
    for i in range(section["nrel"]):
        base = section["relptr"] + i * 10
        r_vaddr, r_symidx, r_type = struct.unpack_from("<IIH", data, base)
        out.append((r_vaddr, r_symidx, r_type))
    return out


def coff_function(data, symbol):
    """Locate a function symbol and return (bytes, relocs-relative-to-it)."""
    sections, symbols = coff_parse(data)

    defined = [s for s in symbols if s["section"] > 0]

    hits = [s for s in defined if s["name"] == symbol]
    if not hits:
        # The leading-underscore form cdecl gives C functions.
        hits = [s for s in defined if s["name"] == "_" + symbol]
    if not hits:
        # Last resort: substring, so a half-remembered C++ mangling still
        # finds its function. Only accepted when it is unambiguous.
        loose = [s for s in defined if symbol in s["name"]]
        if len(loose) == 1:
            hits = loose
            print(f"  note: {symbol!r} matched {hits[0]['name']!r} by substring")
        elif len(loose) > 1:
            names = "\n    ".join(sorted(s["name"] for s in loose))
            raise ValueError(
                f"{symbol!r} is ambiguous, it matches {len(loose)} symbols:\n    {names}"
            )
    if not hits:
        raise ValueError(
            f"no defined symbol named {symbol!r} in this object. "
            f"Run `matchtool.py obj <objfile>` to list what is there."
        )

    sym = hits[0]
    sec = sections[sym["section"] - 1]
    start = sym["value"]

    # With /Gy each function is its own COMDAT section, so the section bounds
    # it exactly. Without /Gy, functions are packed together and the next
    # symbol in the same section marks the end.
    if sec["chars"] & IMAGE_SCN_LNK_COMDAT:
        end = sec["size"]
    else:
        later = [s["value"] for s in symbols
                 if s["section"] == sym["section"] and s["value"] > start]
        end = min(later) if later else sec["size"]

    body = data[sec["rawptr"] + start:sec["rawptr"] + end]
    relocs = [(v - start, t) for v, _, t in coff_relocs(data, sec)
              if start <= v < end]
    return body, relocs, sec["name"], bool(sec["chars"] & IMAGE_SCN_LNK_COMDAT)


# -------------------------------------------------------------------- compare

def mask(buf, relocs):
    """Blank the 4-byte operand at each relocation site."""
    out = bytearray(buf)
    covered = set()
    for off, _ in relocs:
        for i in range(off, min(off + RELOC_WIDTH, len(out))):
            out[i] = 0
            covered.add(i)
    return bytes(out), covered


def hexdiff(a, b, covered, width=16):
    """Side-by-side hex, '..' for masked operands, '^' under differing bytes."""
    lines = []
    for base in range(0, max(len(a), len(b)), width):
        ra, rb = a[base:base + width], b[base:base + width]
        if ra == rb and base + width <= min(len(a), len(b)):
            continue  # identical row, not worth printing

        def render(row, other):
            cells = []
            for i, ch in enumerate(row):
                off = base + i
                if off in covered:
                    cells.append("..")
                elif i >= len(other) or other[i] != ch:
                    cells.append(f"\x1b[31m{ch:02x}\x1b[0m" if sys.stdout.isatty()
                                 else f"{ch:02x}")
                else:
                    cells.append(f"{ch:02x}")
            return " ".join(cells)

        lines.append(f"  {base:04x}  orig  {render(ra, rb)}")
        lines.append(f"        ours  {render(rb, ra)}")
        marks = []
        for i in range(width):
            off = base + i
            if off in covered:
                marks.append("  ")
            elif i < len(ra) and i < len(rb) and ra[i] == rb[i]:
                marks.append("  ")
            elif i < max(len(ra), len(rb)):
                marks.append("^^")
            else:
                marks.append("  ")
        lines.append(f"              {' '.join(marks)}")
        lines.append("")
    return lines


def compare(pe_path, va, obj_path, symbol, size=None):
    obj = open(obj_path, "rb").read()
    ours, relocs, secname, comdat = coff_function(obj, symbol)

    # With /Gy the COMDAT includes the alignment padding after the function,
    # so `ours` is usually a little longer than the code itself. That is
    # harmless when the two agree. When they do not, comparing the candidate's
    # full length runs the diff off the end of the original and into whatever
    # function follows it, which is noise. Pass the original's real length to
    # keep the comparison inside it.
    overrun = None
    if size is not None and size < len(ours):
        overrun = len(ours) - size
        ours = ours[:size]
        relocs = [(o, t) for o, t in relocs if o < size]

    pe = open(pe_path, "rb").read()
    secname_img, off, orig = image_bytes(pe, va, len(ours))

    print(f"  original   {pe_path}")
    print(f"             va {va:#x}  section {secname_img}  file offset {off:#x}")
    print(f"  candidate  {obj_path}")
    print(f"             symbol {symbol}  section {secname}"
          f"{'  (COMDAT, /Gy)' if comdat else '  (packed, no /Gy)'}")
    if overrun:
        print(f"  length     {len(ours)} bytes compared"
              f"  ({overrun} trailing bytes ignored - COMDAT alignment padding)")
    else:
        print(f"  length     {len(ours)} bytes")
    print(f"  relocs     {len(relocs)}"
          + (f"  [{', '.join(sorted({REL_I386.get(t, hex(t)) for _, t in relocs}))}]"
             if relocs else "  (none - unmasked comparison, a match is unambiguous)"))
    print()

    a, covered = mask(orig, relocs)
    b, _ = mask(ours, relocs)

    if a == b:
        print(f"  MATCH - {len(ours)} bytes identical"
              + (f" ({len(covered)} masked as relocated operands)" if covered else ""))
        return 0

    differing = sum(1 for i in range(len(a)) if a[i] != b[i])
    print(f"  MISMATCH - {differing} of {len(a)} bytes differ\n")
    for line in hexdiff(a, b, covered):
        print(line)
    print("  Masked bytes show as '..'; they are relocation operands and carry")
    print("  no information about code generation.")
    return 1


# ------------------------------------------------------------------------ cli

def main(argv):
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    cmd = argv[1]

    if cmd == "image" and len(argv) == 5:
        data = open(argv[2], "rb").read()
        va, size = parse_int(argv[3]), parse_int(argv[4])
        name, off, buf = image_bytes(data, va, size)
        print(f"  {argv[2]}  va {va:#x}  section {name}  file offset {off:#x}"
              f"  {size} bytes\n")
        for base in range(0, len(buf), 16):
            row = buf[base:base + 16]
            print(f"  {va + base:08x}  " + " ".join(f"{c:02x}" for c in row))
        return 0

    if cmd == "obj" and len(argv) in (3, 4):
        data = open(argv[2], "rb").read()
        if len(argv) == 3:
            sections, symbols = coff_parse(data)
            print(f"  {argv[2]}  {len(sections)} sections, {len(symbols)} symbols\n")
            print("  defined symbols in code sections:")
            for s in symbols:
                if s["section"] > 0:
                    sec = sections[s["section"] - 1]
                    if sec["chars"] & 0x20:  # IMAGE_SCN_CNT_CODE
                        print(f"    {s['value']:>#8x}  {sec['name']:<12} {s['name']}")
            return 0
        body, relocs, secname, comdat = coff_function(data, argv[3])
        print(f"  {argv[3]}  section {secname}"
              f"{'  (COMDAT)' if comdat else ''}  {len(body)} bytes")
        print(f"  {len(relocs)} relocations\n")
        for off, rtype in relocs:
            print(f"    +{off:#06x}  {REL_I386.get(rtype, hex(rtype))}")
        print()
        for base in range(0, len(body), 16):
            row = body[base:base + 16]
            print(f"  {base:04x}  " + " ".join(f"{c:02x}" for c in row))
        return 0

    if cmd == "compare" and len(argv) in (6, 7):
        size = parse_int(argv[6]) if len(argv) == 7 else None
        return compare(argv[2], parse_int(argv[3]), argv[4], argv[5], size)

    print(__doc__.strip())
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
