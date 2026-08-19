#!/usr/bin/env python3
"""
richdump.py - identify the exact toolchain that built a PE32 image.

Reads the DOS header, the undocumented "Rich" header, the COFF/Optional
headers and the debug directory. No third-party dependencies.

The Rich header is a table Microsoft's linker stamps between the DOS stub
and the PE signature. It records, for every object file fed to the linker,
which tool emitted it and that tool's exact build number - including tools
that left no other trace in the image. It is the most precise compiler
fingerprint a stripped release binary carries.

Usage:  python richdump.py <path-to-exe> [...]
"""

import struct
import sys

# ---------------------------------------------------------------- prodid table
#
# comp.id = (prodID << 16) | buildNumber.  The prodID names below are the
# community-reconstructed mapping (Microsoft has never documented it). IDs
# absent from this table are printed raw rather than guessed at, so a gap
# here can never turn into a wrong answer upstream.
#
PRODID = {
    0x00: "Unknown",
    0x01: "Import (linker-generated)",
    0x02: "Linker 5.10",
    0x03: "Cvtomf 5.10",
    0x04: "Linker 6.00",
    0x05: "Cvtomf 6.00",
    0x06: "Cvtres 5.00",
    0x07: "UTC 11 Basic",
    0x08: "UTC 11 C",
    0x09: "UTC 12 Basic",
    0x0A: "UTC 12 C",
    0x0B: "UTC 12 C++",
    0x0C: "AliasObj 6.00",
    0x0D: "Visual Basic 6.00",
    0x0E: "MASM 6.13",
    0x0F: "MASM 7.10",
    0x10: "Linker 5.11",
    0x11: "Cvtomf 5.11",
    0x12: "MASM 6.14",
    0x13: "Linker 5.12",
    0x14: "Cvtomf 5.12",
    0x15: "UTC 12 C (std)",
    0x16: "UTC 12 C++ (std)",
    0x17: "UTC 12 C (book)",
    0x18: "UTC 12 C++ (book)",
    0x19: "Implib 7.00",
    0x1A: "Cvtomf 7.00",
    0x1B: "UTC 13 Basic",
    0x1C: "UTC 13 C",
    0x1D: "UTC 13 C++",
    0x1E: "Linker 6.10",
    0x1F: "Cvtomf 6.10",
    0x20: "Linker 6.01",
    0x21: "Cvtomf 6.01",
    0x22: "UTC 12.1 Basic",
    0x23: "UTC 12.1 C",
    0x24: "UTC 12.1 C++",
    0x25: "Linker 6.20",
    0x26: "Cvtomf 6.20",
    0x27: "AliasObj 7.00",
    0x28: "Linker 6.21",
    0x29: "Cvtomf 6.21",
    0x2A: "MASM 6.15",
    0x2B: "UTC 13 LTCG C",
    0x2C: "UTC 13 LTCG C++",
    0x2D: "MASM 6.20",
    0x2E: "ILAsm 1.00",
    0x2F: "UTC 12.2 Basic",
    0x30: "UTC 12.2 C",
    0x31: "UTC 12.2 C++",
    0x32: "UTC 12.2 C (std)",
    0x33: "UTC 12.2 C++ (std)",
    0x34: "UTC 12.2 C (book)",
    0x35: "UTC 12.2 C++ (book)",
    0x36: "Implib 6.22",
    0x37: "Cvtomf 6.22",
    0x38: "Cvtres 5.01",
    0x39: "UTC 13 C (std)",
    0x3A: "UTC 13 C++ (std)",
    0x3B: "Cvtpgd 13.00",
    0x3C: "Linker 6.22",
    0x3D: "Linker 7.00",
    0x3E: "Export 6.22",
    0x3F: "Export 7.00",
    0x40: "MASM 7.00",
    0x41: "UTC 13 POGO-I C",
    0x42: "UTC 13 POGO-I C++",
    0x43: "UTC 13 POGO-O C",
    0x44: "UTC 13 POGO-O C++",
    0x45: "Cvtres 7.00",
    0x46: "Cvtres 7.10 (pre)",
    0x47: "Linker 7.10 (pre)",
    0x48: "AliasObj 7.10 (pre)",
    0x49: "Cvtpgd 13.10",
    0x4A: "UTC 13.10 C (pre)",
    0x4B: "UTC 13.10 C++ (pre)",
    0x56: "Linker 7.10",
    0x5A: "Cvtres 7.10",
    0x5B: "UTC 13.10 C",
    0x5C: "UTC 13.10 C++",
    0x69: "Linker 8.00",
    0x6B: "UTC 14.00 C",
    0x6C: "UTC 14.00 C++",
    0x83: "Linker 9.00",
    0x85: "UTC 15.00 C",
    0x86: "UTC 15.00 C++",
    0x95: "Linker 10.00",
    0x97: "UTC 16.00 C",
    0x98: "UTC 16.00 C++",
}

# ------------------------------------------------------------- build -> release
#
# Build numbers are only unique WITHIN a tool family - build 8034 means one
# thing for a VC6 compiler and something unrelated for a VC5 linker. So the
# mapping is keyed by family, and a build is annotated only inside the family
# it belongs to. Anything not listed prints bare; nothing is approximated.
#
FAMILY = {
    "utc12": {  # VC++ 6.0 compiler back end (C, C++, and the /std /book SKUs)
        0x09, 0x0A, 0x0B, 0x15, 0x16, 0x17, 0x18,
        0x22, 0x23, 0x24, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
    },
    "link6": {0x04, 0x1E, 0x20, 0x25, 0x28, 0x3C},   # VC++ 6.0 linker
    "utc13": {0x1B, 0x1C, 0x1D, 0x2B, 0x2C, 0x39, 0x3A, 0x41, 0x42, 0x43, 0x44},
    "link7": {0x3D},
}

BUILD = {
    "utc12": {
        8168: "VC++ 6.0, SP5",
        8447: "VC++ 6.0, SP5 + Processor Pack",
        8804: "VC++ 6.0, SP6",
    },
    "link6": {
        8168: "VC++ 6.0, SP5",
        8447: "VC++ 6.0, SP5 + Processor Pack",
        8804: "VC++ 6.0, SP6",
    },
    "utc13": {
        9466: "VC++ 7.0 beta",
        9782: "VC++ 7.0 (Visual Studio .NET 2002)",
    },
    "link7": {
        9782: "VC++ 7.0 (Visual Studio .NET 2002)",
    },
}


def annotate(prodid, build):
    """Release name for this build, but only within the tool's own family."""
    for fam, ids in FAMILY.items():
        if prodid in ids:
            return BUILD.get(fam, {}).get(build)
    return None

SUBSYSTEM = {1: "native", 2: "Windows GUI", 3: "Windows console", 9: "Windows CE GUI"}
MACHINE = {0x014C: "i386 (32-bit)", 0x8664: "x86-64", 0x01C0: "ARM", 0xAA64: "ARM64"}
DEBUG_TYPE = {0: "unknown", 1: "COFF", 2: "CODEVIEW", 3: "FPO", 4: "MISC", 12: "VC_FEATURE"}


def rol32(value, shift):
    shift &= 31
    return ((value << shift) | (value >> (32 - shift))) & 0xFFFFFFFF


def parse_rich(data):
    """Return (key, [(prodid, build, count), ...], checksum_ok) or None."""
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]

    rich_off = data.find(b"Rich", 0x40, e_lfanew)
    if rich_off < 0:
        return None
    key = struct.unpack_from("<I", data, rich_off + 4)[0]

    # "DanS" opens the record, stored XORed with the same key.
    dans = struct.pack("<I", 0x536E6144 ^ key)
    dans_off = data.rfind(dans, 0x40, rich_off)
    if dans_off < 0:
        return None

    # DanS is followed by three XORed zero dwords of padding.
    entries = []
    for off in range(dans_off + 16, rich_off, 8):
        compid = struct.unpack_from("<I", data, off)[0] ^ key
        count = struct.unpack_from("<I", data, off + 4)[0] ^ key
        entries.append((compid >> 16, compid & 0xFFFF, count))

    # The key doubles as a checksum over everything preceding the record plus
    # the entries themselves. Recomputing it proves the header is intact and
    # the decode is correctly aligned. The sum is seeded with the offset of
    # the DanS record, and e_lfanew is excluded because the linker had not
    # yet fixed it up when the sum was taken.
    csum = dans_off
    for i in range(dans_off):
        if 0x3C <= i < 0x40:
            continue
        csum = (csum + rol32(data[i], i)) & 0xFFFFFFFF
    for prodid, build, count in entries:
        csum = (csum + rol32((prodid << 16) | build, count)) & 0xFFFFFFFF

    return key, entries, csum == key, dans_off, rich_off


def parse_headers(data):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        raise ValueError("no PE signature at e_lfanew")

    coff = e_lfanew + 4
    machine, nsections, timestamp, _, _, opt_size, characteristics = struct.unpack_from(
        "<HHIIIHH", data, coff
    )

    opt = coff + 20
    magic, link_major, link_minor = struct.unpack_from("<HBB", data, opt)
    if magic == 0x10B:      # PE32
        image_base = struct.unpack_from("<I", data, opt + 28)[0]
        dirs = opt + 96
        nrva = struct.unpack_from("<I", data, opt + 92)[0]
    elif magic == 0x20B:    # PE32+ (ImageBase widens to 8 bytes at +24)
        image_base = struct.unpack_from("<Q", data, opt + 24)[0]
        dirs = opt + 112
        nrva = struct.unpack_from("<I", data, opt + 108)[0]
    else:
        raise ValueError(f"unknown optional header magic {magic:#06x}")

    subsystem = struct.unpack_from("<H", data, opt + 68)[0]

    # Data directory entry 6 is the debug directory.
    debug_rva = debug_size = 0
    if nrva > 6:
        debug_rva, debug_size = struct.unpack_from("<II", data, dirs + 6 * 8)

    sections = []
    sec = opt + opt_size
    for i in range(nsections):
        base = sec + i * 40
        name = data[base:base + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, base + 8)
        sections.append((name, vaddr, vsize, rawptr, rawsize))

    return {
        "magic": magic,
        "machine": machine,
        "timestamp": timestamp,
        "characteristics": characteristics,
        "linker": (link_major, link_minor),
        "image_base": image_base,
        "subsystem": subsystem,
        "debug": (debug_rva, debug_size),
        "sections": sections,
    }


def rva_to_offset(rva, sections):
    for _, vaddr, vsize, rawptr, rawsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return rawptr + (rva - vaddr)
    return None


def parse_debug(data, hdr):
    """Yield (type, timestamp, pdb_path_or_None) for each debug directory entry."""
    rva, size = hdr["debug"]
    if not rva or not size:
        return
    off = rva_to_offset(rva, hdr["sections"])
    if off is None:
        return
    for i in range(size // 28):
        base = off + i * 28
        if base + 28 > len(data):
            break
        _, ts, _, _, dtype, dsize, _, draw = struct.unpack_from("<IIHHIIII", data, base)
        pdb = None
        if dtype == 2 and draw and draw + 4 <= len(data):  # CODEVIEW
            sig = data[draw:draw + 4]
            if sig == b"RSDS":
                pdb = data[draw + 24:draw + dsize].split(b"\0")[0].decode("ascii", "replace")
            elif sig == b"NB10":
                pdb = data[draw + 16:draw + dsize].split(b"\0")[0].decode("ascii", "replace")
        yield dtype, ts, pdb


def report(path):
    with open(path, "rb") as fh:
        data = fh.read()

    print("=" * 78)
    print(path)
    print(f"{len(data):,} bytes")
    print("=" * 78)

    if data[:2] != b"MZ":
        print("  not an MZ image")
        return

    hdr = parse_headers(data)
    major, minor = hdr["linker"]

    print("\n-- PE headers " + "-" * 63)
    print(f"  format          {'PE32' if hdr['magic'] == 0x10B else 'PE32+'}")
    print(f"  machine         {hdr['machine']:#06x}  {MACHINE.get(hdr['machine'], '?')}")
    print(f"  linker version  {major}.{minor:02d}")
    print(f"  image base      {hdr['image_base']:#010x}")
    print(f"  subsystem       {hdr['subsystem']}  {SUBSYSTEM.get(hdr['subsystem'], '?')}")
    print(f"  timestamp       {hdr['timestamp']:#010x}", end="")
    if hdr["timestamp"]:
        import datetime
        stamp = datetime.datetime.fromtimestamp(
            hdr["timestamp"], datetime.timezone.utc
        )
        print(f"  ({stamp:%Y-%m-%d %H:%M:%S} UTC)")
    else:
        print()
    print(f"  characteristics {hdr['characteristics']:#06x}")

    print("\n-- Sections " + "-" * 65)
    print(f"  {'name':<10} {'vaddr':>10} {'vsize':>10} {'rawptr':>10} {'rawsize':>10}")
    for name, vaddr, vsize, rawptr, rawsize in hdr["sections"]:
        print(f"  {name:<10} {vaddr:>#10x} {vsize:>#10x} {rawptr:>#10x} {rawsize:>#10x}")

    print("\n-- Rich header " + "-" * 62)
    rich = parse_rich(data)
    if rich is None:
        print("  absent  (non-Microsoft linker, or stripped)")
    else:
        key, entries, ok, dans_off, rich_off = rich
        span = rich_off - (dans_off + 16)
        print(f"  DanS at         {dans_off:#06x}")
        print(f"  Rich at         {rich_off:#06x}")
        print(f"  entry span      {span} bytes"
              f"  ({'8-byte aligned' if span % 8 == 0 else 'MISALIGNED - decode suspect'})")
        print(f"  xor key         {key:#010x}")
        print(f"  checksum        "
              f"{'valid - header intact, decode trustworthy' if ok else 'MISMATCH - header altered after link'}")
        print(f"  {len(entries)} entries\n")
        print(f"  {'prodID':>7} {'build':>7} {'count':>7}  tool / release")
        print(f"  {'-'*7} {'-'*7} {'-'*7}  {'-'*44}")
        for prodid, build, count in sorted(entries, key=lambda e: -e[2]):
            tool = PRODID.get(prodid, f"<unmapped prodID {prodid:#06x}>")
            rel = annotate(prodid, build)
            label = f"{tool}" + (f"   [{rel}]" if rel else "")
            print(f"  {prodid:>#7x} {build:>7} {count:>7}  {label}")

    print("\n-- Debug directory " + "-" * 58)
    found = False
    for dtype, ts, pdb in parse_debug(data, hdr):
        found = True
        name = DEBUG_TYPE.get(dtype, f"type {dtype}")
        print(f"  {name}" + (f"  ->  {pdb}" if pdb else ""))
    if not found:
        print("  empty  (stripped release build)")
    print()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__.strip())
        sys.exit(2)
    for arg in sys.argv[1:]:
        report(arg)
