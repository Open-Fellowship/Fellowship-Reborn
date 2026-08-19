#!/usr/bin/env python3
"""
corpus.py - fold a whole-image function export into something greppable.

`ExportFunctions.java` writes one JSON file per function, which is the right
shape for a tool and the wrong shape for a person. Nobody greps 4,962 JSON
files, and the cost of not being able to shows up as work done twice: a
question like "where else does the engine call this" or "what other function
touches that global" is a one-second grep against a flat file and an afternoon
without one.

  python corpus.py --build rfl        write the flat files from export/rfl-all
  python corpus.py <pattern>          search them, printing whole functions
  python corpus.py --calls 1004c210   every function that calls that address

## What it writes, under export/<image>-all/

  corpus.c     every decompilation, each under a banner naming its address,
               size and callees
  corpus.asm   the same functions as disassembly, one instruction a line with
               its bytes - which is what a matching question needs
  corpus.tsv   address, size, name, callee count, leaf, so the set can be
               sorted and filtered without parsing anything

The banner format is fixed and greppable on purpose: `grep -n "^;;;;" corpus.asm`
lists every function boundary, so a hit can always be traced back to the
function containing it with a single search.
"""

import io
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
EXPORT = os.path.join(REPO, "decomp", "export")

BANNER = ";;;; %s  %s  %d bytes  %d callees%s"


def outdir(image):
    return os.path.join(EXPORT, "%s-all" % image)


def build(image):
    d = outdir(image)
    if not os.path.isdir(d):
        raise SystemExit("no export at %s - run ExportFunctions.java over the "
                         "whole image first" % d)
    names = sorted(f for f in os.listdir(d) if re.fullmatch(r"[0-9a-f]{8}\.json", f))
    if not names:
        raise SystemExit("no per-function JSON in %s" % d)

    cf = io.open(os.path.join(d, "corpus.c"), "w", encoding="utf-8", newline="\n")
    af = io.open(os.path.join(d, "corpus.asm"), "w", encoding="utf-8", newline="\n")
    tf = io.open(os.path.join(d, "corpus.tsv"), "w", encoding="utf-8", newline="\n")
    tf.write("# entry\tsize\tname\tcallees\tleaf\n")

    n = 0
    for name in names:
        with io.open(os.path.join(d, name), encoding="utf-8") as fh:
            f = json.load(fh)
        leaf = not f["calls"]
        banner = BANNER % (f["entry"], f["name"], f["size"], len(f["calls"]),
                           "  LEAF" if leaf else "")
        tf.write("%s\t%d\t%s\t%d\t%s\n"
                 % (f["entry"], f["size"], f["name"], len(f["calls"]),
                    "true" if leaf else "false"))

        af.write(banner + "\n")
        for c in f["calls"]:
            af.write(";;;;   calls %s\n" % c)
        for s in f["data"]:
            af.write(";;;;   data  %s\n" % s)
        for line in f["disassembly"]:
            af.write(line + "\n")
        af.write("\n")

        cf.write(banner.replace(";;;;", "////") + "\n")
        cf.write((f["decompiled"] or "// decompilation failed") + "\n")
        n += 1

    for fh in (cf, af, tf):
        fh.close()
    print("%s: %d functions" % (image, n))
    for f in ("corpus.c", "corpus.asm", "corpus.tsv"):
        p = os.path.join(d, f)
        print("  %-12s %8.1f MB" % (f, os.path.getsize(p) / 1048576.0))


def functions_of(path, marker):
    """Yield (banner, [lines]) for each function in a flat corpus file."""
    # The marker plus " 0x", not the marker alone: the header block under each
    # banner is also commented with it, and treating those lines as banners
    # made the callee and string lists unsearchable - the one part of the file
    # a question like "who calls this" needs.
    head = marker + " 0x"
    banner, body = None, []
    with io.open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith(head):
                if banner is not None:
                    yield banner, body
                banner, body = line.rstrip("\n"), []
            elif banner is not None:
                body.append(line.rstrip("\n"))
    if banner is not None:
        yield banner, body


def search(pattern, image, which):
    rx = re.compile(pattern, re.I)
    path = os.path.join(outdir(image),
                        "corpus.asm" if which == "asm" else "corpus.c")
    marker = ";;;;" if which == "asm" else "////"
    if not os.path.exists(path):
        raise SystemExit("no %s - run --build %s first" % (path, image))
    hits = 0
    for banner, body in functions_of(path, marker):
        found = [l for l in body if rx.search(l)]
        if not found:
            continue
        hits += 1
        print(banner)
        for l in found[:12]:
            print("    " + l.strip())
        if len(found) > 12:
            print("    ... %d more lines" % (len(found) - 12))
    print("\n%d function(s) match %r" % (hits, pattern))
    return 0


def main(argv):
    args = [a for a in argv[1:]]
    image = "rfl"
    if "--image" in args:
        i = args.index("--image")
        image = args[i + 1]
        del args[i:i + 2]
    which = "asm" if "--asm" in args else "c"
    args = [a for a in args if a != "--asm"]

    if args and args[0] == "--build":
        build(args[1] if len(args) > 1 else image)
        return 0
    if args and args[0] == "--calls":
        # A call is recorded as "<addr> <name>" on its own banner line, so this
        # is just a search of the header block rather than of the code.
        return search(r"^;;;;\s+calls %s" % args[1].lstrip("0x"), image, "asm")
    if not args:
        print(__doc__.strip().splitlines()[2])
        print("  --build <image>   write corpus.c / corpus.asm / corpus.tsv")
        print("  <pattern>         search the decompilation")
        print("  --asm <pattern>   search the disassembly instead")
        print("  --calls <addr>    every function calling that address")
        return 2
    return search(args[0], image, which)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
