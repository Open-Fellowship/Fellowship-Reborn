#!/usr/bin/env python3
"""
build.py - compile every decompiled translation unit and check it against the
original, byte for byte.

Reads manifest.tsv, compiles each source under src/ once with VC++ 6.0, and
compares every function listed against the image it came from. Reports a
regression when something that used to match no longer does, and progress when
something marked todo now matches.

  python build.py                    everything
  python build.py vector3.cpp        one source file
  python build.py --flags "/Ox /Gy"  override the compiler switches

Paths come from the environment, with the defaults below:

  VC6   the portable toolchain, containing bin\\ include\\ lib\\
  RFL   a PRISTINE Fellowship.rfl   (see documentation/TOOLCHAIN.md for hashes)
  EXE   a PRISTINE Fellowship.exe

A patched image mismatches for reasons that have nothing to do with the source.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "tools"))
import matchtool  # noqa: E402

VC6 = os.environ.get("VC6", r"K:\OPEN FELLOWSHIP\vc6-portable")
IMAGES = {
    "rfl": os.environ.get("RFL", r"K:\OPEN FELLOWSHIP\reference\Fellowship.rfl"),
    "exe": os.environ.get("EXE", r"K:\OPEN FELLOWSHIP\reference\Fellowship.exe"),
}
# /GX emits nothing unless a function has a destructible local, which is why
# it stayed invisible for the first 34 matches. 0x1005c500 needs it: its C++ EH
# frame cannot be produced without it. Adding it changed no existing match.
FLAGS = "/O2 /Gy /GX"

SRC = os.path.join(HERE, "src")
# Kept separate from try.py's build\obj\, so an integration run cannot land on
# an object a worker is part-way through writing.
OBJ = os.environ.get("DECOMP_OBJDIR", os.path.join(HERE, "build", "all"))


def read_manifest():
    rows = []
    path = os.path.join(HERE, "manifest.tsv")
    with open(path) as fh:
        for n, line in enumerate(fh, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 6:
                raise SystemExit(f"manifest.tsv:{n}: expected 6 tab-separated fields, got {len(parts)}")
            image, va, size, source, expect, symbol = parts
            rows.append({
                "image": image, "va": int(va, 16), "size": int(size),
                "source": source, "expect": expect, "symbol": symbol,
            })
    return rows


def read_census():
    """Totals per image, so coverage has an honest denominator."""
    path = os.path.join(HERE, "census.tsv")
    out = {}
    if not os.path.exists(path):
        return out
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            p = line.split("\t")
            if len(p) >= 3:
                e = {"functions": int(p[1]), "bytes": int(p[2])}
                # Code Ghidra left outside any function - see the header of
                # census.tsv. Optional so an older census still loads, but its
                # absence means the denominator is the flattering one.
                if len(p) >= 7:
                    e["orphan_functions"] = int(p[5])
                    e["orphan_bytes"] = int(p[6])
                out[p[0]] = e
    return out


def status(rows):
    """Per-file progress, and coverage against the whole image.

    Two numbers, because they answer different questions. Manifest progress
    says whether what we have attempted is finished; it trends to 100% by
    construction and means little on its own. Image coverage says how much of
    the binary is actually accounted for.
    """
    print("  by source file")
    print(f"    {'':<26} {'done':>9}")
    for source in sorted({r["source"] for r in rows}):
        got = [r for r in rows if r["source"] == source]
        ok = sum(1 for r in got if r["expect"] == "match")
        print(f"    {source:<26} {ok:>4} / {len(got):<4}")

    census = read_census()
    print("\n  coverage of each image")
    for image in sorted({r["image"] for r in rows}):
        got = [r for r in rows if r["image"] == image and r["expect"] == "match"]
        # A function matched in two images is two entries but one piece of
        # work, so count unique addresses per image.
        addrs = {r["va"] for r in got}
        done_bytes = sum(r["size"] for r in got if r["va"] in addrs)
        c = census.get(image)
        if c:
            # Against all the image's code, not just the code that made it into
            # a function. The two differ by a third in the rfl, and reporting
            # the smaller denominator would be quietly claiming a third more
            # progress than we have.
            fn = c["functions"] + c.get("orphan_functions", 0)
            by = c["bytes"] + c.get("orphan_bytes", 0)
            print(f"    {image}: {len(addrs)} of {fn:,} functions "
                  f"({100.0 * len(addrs) / fn:.2f}%), "
                  f"{done_bytes:,} of {by:,} bytes "
                  f"({100.0 * done_bytes / by:.2f}%)")
            if "orphan_functions" in c:
                print(f"         of which {c['orphan_functions']:,} functions and "
                      f"{c['orphan_bytes']:,} bytes are code Ghidra never put in a "
                      f"function")
        else:
            print(f"    {image}: {len(addrs)} functions, {done_bytes:,} bytes "
                  f"(no census.tsv entry - denominator unknown)")


def compile_one(source, flags):
    """Compile one translation unit. Returns the object path, or None."""
    cl = os.path.join(VC6, "bin", "CL.EXE")
    if not os.path.exists(cl):
        raise SystemExit(f"no CL.EXE under {VC6}\n"
                         f"set VC6 to the portable toolchain directory")
    env = dict(os.environ)
    env["PATH"] = os.path.join(VC6, "bin") + os.pathsep + env.get("PATH", "")
    env["INCLUDE"] = os.path.join(VC6, "include")
    env["LIB"] = os.path.join(VC6, "lib")

    # `source` is relative to src\ and may name a subdirectory, so the object
    # tree mirrors it - two modules are then free to hold a same-named file.
    obj = os.path.join(OBJ, os.path.splitext(source)[0] + ".obj")
    os.makedirs(os.path.dirname(obj), exist_ok=True)
    cmd = [cl, "/nologo", "/c"] + flags.split() + [
        os.path.join(SRC, source), "/Fo" + obj,
    ]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, cwd=OBJ)
    if r.returncode != 0:
        print(f"  COMPILE FAILED: {source}")
        for line in (r.stdout + r.stderr).splitlines():
            if line.strip() and not line.strip().endswith(".cpp"):
                print(f"    {line.rstrip()}")
        return None
    return obj


def main(argv):
    flags = FLAGS
    only = None
    want_status = False
    args = argv[1:]
    while args:
        if args[0] == "--flags" and len(args) > 1:
            flags = args[1]
            args = args[2:]
        elif args[0] == "--status":
            want_status = True
            args = args[1:]
        else:
            only = args[0]
            args = args[1:]

    rows = read_manifest()

    if want_status:
        status(rows)
        return 0
    if only:
        rows = [r for r in rows if r["source"] == only]
        if not rows:
            raise SystemExit(f"no manifest entries for {only!r}")

    print(f"  toolchain  {VC6}")
    print(f"  flags      {flags}")
    print()

    results = []
    for source in sorted({r["source"] for r in rows}):
        obj = compile_one(source, flags)
        entries = [r for r in rows if r["source"] == source]
        if obj is None:
            results += [(r, "build") for r in entries]
            continue
        data = open(obj, "rb").read()
        for r in entries:
            image = IMAGES.get(r["image"])
            if not image or not os.path.exists(image):
                print(f"  MISSING IMAGE for {r['image']}: {image}")
                results.append((r, "image"))
                continue
            try:
                ours, relocs, _, _ = matchtool.coff_function(data, r["symbol"])
                ours = ours[:r["size"]]
                relocs = [(o, t) for o, t in relocs if o < r["size"]]
                _, _, orig = matchtool.image_bytes(open(image, "rb").read(),
                                                   r["va"], len(ours))
                a, _ = matchtool.mask(orig, relocs)
                b, _ = matchtool.mask(ours, relocs)
                results.append((r, "match" if a == b else "differ"))
            except Exception as exc:
                print(f"  ERROR {r['symbol']}: {exc}")
                results.append((r, "error"))

    print(f"  {'':4} {'address':>12}  {'size':>4}  {'source':<16} symbol")
    print(f"  {'-'*4} {'-'*12}  {'-'*4}  {'-'*16} {'-'*40}")
    regressions = progress = 0
    for r, got in results:
        if got == "match":
            tag = "ok" if r["expect"] == "match" else "NEW"
            if r["expect"] != "match":
                progress += 1
        else:
            tag = "FAIL" if r["expect"] == "match" else ".."
            if r["expect"] == "match":
                regressions += 1
        print(f"  {tag:<4} {r['va']:#010x}  {r['size']:>4}  {r['source']:<16} {r['symbol']}")

    matched = sum(1 for _, g in results if g == "match")
    print()
    print(f"  {matched} of {len(results)} match with {flags}")
    if regressions:
        print(f"  {regressions} REGRESSION(S) - these matched before and do not now")
    if progress:
        print(f"  {progress} newly matching - update `expect` to match in manifest.tsv")
    return 1 if (regressions or matched != len(results)) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
