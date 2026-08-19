#!/usr/bin/env python3
"""
ordmap.py - find every property access in an image and name it.

The engine reads an authored property with one instruction shape (see
documentation/OBJECT-MODEL.md):

    PUSH -1                 element index, 0 for the list forms
    PUSH <ordinal>          the property's flat index in its class's schema
    CALL [reg + 8]          -> void*

The ordinal is meaningless on its own. Paired with a class it names a property,
because documentation/generated/ gives every class's properties in that exact
order. This tool does the pairing: it finds the sites, groups them by the
function they sit in, works out which class each function is handling, and
prints the property names.

  python ordmap.py                     summary and the classes it could attribute
  python ordmap.py Player              every function that touches that class
  python ordmap.py --function 1005c500 one function's accesses
  python ordmap.py --tables <dir>      generated markdown for documentation/

## Attributing a class

Three signals, in order of strength.

**A class id in the function's own bytes.** Every dword in the body that falls
in the ObjectDef id range is a candidate. This is the strong signal: the engine
tests `record[+4]` against a literal id before reading that class's properties,
so the id is usually right there.

**A class id in a direct caller.** Accessors are frequently one level below the
test. Weaker, because a caller may test several classes and dispatch.

**The ordinal ceiling.** An ordinal is only legal for a class with more
properties than that, so the highest ordinal a function uses rules out every
class smaller than it. On its own this rarely picks one class - but it is
enough when the ordinals run high, and `Player` has 166 properties where the
next largest has 154, so anything above 154 is Player and nothing else.

Attribution is reported with the signal that produced it. A function nothing
identifies is still listed, with its ordinals, because the ordinals are facts
even when the class is not.
"""

import io
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classdump  # noqa: E402

REPO = os.path.dirname(os.path.dirname(HERE))
IMAGES = {
    "rfl": os.environ.get(
        "RFL", r"K:\OPEN FELLOWSHIP\reference\Fellowship.rfl"),
    "exe": os.environ.get(
        "EXE", r"K:\OPEN FELLOWSHIP\reference\Fellowship.exe"),
}
CENSUS = {
    "rfl": os.path.join(REPO, "decomp", "export", "census-rfl", "index.json"),
    "exe": os.path.join(REPO, "decomp", "export", "census-exe", "index.json"),
}


def schema():
    """Every class, with its properties flattened into ordinal order."""
    _, classes = classdump.read()
    out = {}
    for c in classes:
        flat = [p for g in c["groups"] for p in g["properties"]]
        out[c["id"]] = {"name": c["name"], "props": flat}
    return out


def text_section(img):
    for nm, va, vsz, ptr, rsz in img.secs:
        if nm == ".text":
            return va, ptr, rsz
    raise SystemExit("no .text")


def find_sites(img):
    """Every (address, ordinal) matching the property-read shape.

    Anchored on the CALL rather than on the pushes. The two pushes are not
    adjacent - the engine commonly adjusts the object pointer between them, as
    in `PUSH -1 / ADD EAX,0x14 / PUSH 2` - so a scanner that demands
    `6A FF 6A xx` back to back silently drops most of the sites.

    Arguments go on the stack right to left, so the push CLOSEST to the call is
    the ordinal and the one before it is the element index. The element index
    is required to be -1 or 0, which is what keeps `PUSH -1 / PUSH -1` from
    being read as ordinal 255: that pair inflates the ordinal range past the
    schema ceiling and destroys the one check that makes this reading
    falsifiable.
    """
    d = img.d
    va, ptr, rsz = text_section(img)
    sites = []
    for o in range(ptr, ptr + rsz - 3):
        # FF /2 with an 8-bit displacement of 8: call dword ptr [reg+8]
        if not (d[o] == 0xFF and (d[o + 1] & 0xF8) == 0x50 and d[o + 2] == 0x08):
            continue
        lo = max(ptr, o - 32)
        pushes = []
        k = lo
        while k < o:
            if d[k] == 0x6A and k + 1 < o:
                pushes.append((k, d[k + 1] if d[k + 1] != 0xFF else -1))
            elif d[k] == 0x68 and k + 5 <= o:
                pushes.append((k, struct.unpack_from("<I", d, k + 1)[0]))
            k += 1
        if len(pushes) == 1:
            # The element index in a register rather than an immediate: a list
            # property read at a computed index, `PUSH ESI / PUSH 12 / CALL`.
            # Accepted only in that exact shape - the ordinal push immediately
            # preceded by a one-byte PUSH r32 - because the general case of
            # "one immediate push before the call" matches far too much.
            #
            # This does give up the elem-is--1-or-0 test for these sites, so it
            # is worth being clear about what still constrains them: the ordinal
            # ceiling. No class has more than 166 properties, so a misread here
            # shows up as an impossible ordinal, and --check asserts that none
            # appears. That is the test the reading actually rests on.
            kord, ordv = pushes[0]
            if not (kord - 1 >= ptr and 0x50 <= d[kord - 1] <= 0x57):
                continue
            elem = -1
        elif len(pushes) < 2:
            continue
        else:
            (_, elem), (kord, ordv) = pushes[-2], pushes[-1]
        if elem in (-1, 0) and 0 <= ordv < 0x10000:
            # `D9 00` is FLD dword ptr [EAX] - the accessor's result being taken
            # as a float, in the two bytes immediately after the call. Exactly
            # two bytes, no search window: an earlier version looked 24 bytes
            # ahead and picked up neighbouring FPU code, which made the same
            # ordinal read as float at one site and integer at another.
            isf = d[o + 3] == 0xD9 and d[o + 4] == 0x00
            sites.append((va + (kord - ptr), ordv, isf))
    return sites


# Immediates that can carry a class id, as opcode -> where the imm32 starts.
# A raw scan for the dword anywhere in the body finds far too much: any
# displacement or float constant can land in the id range. Only an immediate
# operand of a compare, a move or a push means anything.
def id_immediates(img, entry, size, valid):
    """[(address, class id)] for every literal class id in the body."""
    d = img.d
    o = img.off(entry)
    if o is None:
        return []
    body = d[o:o + size]
    out = []
    k = 0
    n = len(body)
    while k < n - 4:
        op = body[k]
        pos = None
        if op == 0x3D:                                   # cmp eax, imm32
            pos = k + 1
        elif op == 0x68:                                 # push imm32
            pos = k + 1
        elif 0xB8 <= op <= 0xBF:                         # mov r32, imm32
            pos = k + 1
        elif op in (0x81, 0xC7) and k + 1 < n:           # cmp/mov r/m32, imm32
            modrm = body[k + 1]
            reg = (modrm >> 3) & 7
            if (op == 0x81 and reg == 7) or (op == 0xC7 and reg == 0):
                mod, rm = modrm >> 6, modrm & 7
                skip = 2 + (1 if (mod != 3 and rm == 4) else 0)
                if mod == 1:
                    skip += 1
                elif mod == 2 or (mod == 0 and rm == 5):
                    skip += 4
                pos = k + skip
        if pos is not None and pos + 4 <= n:
            w = struct.unpack_from("<I", body, pos)[0]
            if w in valid:
                out.append((entry + k, w))
                k = pos + 4
                continue
        k += 1
    return out


def functions(image):
    """Ghidra's functions, plus the code that is in none of them.

    Ghidra leaves a lot of an optimised C++ image outside any function - in the
    rfl it is 219,696 bytes across 1,927 runs, a fifth of .text - because code
    reached only through a vtable or a jump table is never the target of a call
    it can follow. Property reads in that code used to be counted as orphans and
    dropped, which meant the reads with the least other evidence about them were
    also the ones getting no function context to attribute them with.

    `census` mode records those runs and whether each one begins exactly where a
    function body ends. The two kinds want different treatment:

      continuation  the body stopped early, most often at a mid-body INT3.
                    The run is the same function's tail, so it is merged into
                    it - which is also the better attribution, since the class
                    id being tested is usually in the head, not the tail.
      standalone    a function Ghidra never found. Added as one, keyed on its
                    own start.
    """
    idx = json.load(io.open(CENSUS[image], encoding="utf-8"))
    fns = dict((int(f["entry"], 16), f["size"]) for f in idx)

    path = os.path.join(os.path.dirname(CENSUS[image]), "orphans.tsv")
    if os.path.exists(path):
        ends = {}
        for entry, size in fns.items():
            ends[entry + size] = entry
        # In address order, so a run continuing a run that continued a function
        # still lands on the function.
        rows = sorted(
            (int(r[0], 16), int(r[1]), r[2].strip())
            for r in (l.rstrip("\n").split("\t")
                      for l in io.open(path, encoding="utf-8")
                      if l.strip() and not l.startswith("#")))
        for start, size, cont in rows:
            owner = ends.get(start) if cont != "-" else None
            if owner is not None:
                fns[owner] += size
            else:
                fns[start] = size
                owner = start
            ends[owner + fns[owner]] = owner

    fns = sorted(fns.items())
    return fns, [f[0] for f in fns]


def containing(fns, starts, addr):
    import bisect
    i = bisect.bisect_right(starts, addr) - 1
    if i < 0:
        return None
    e, sz = fns[i]
    return e if addr < e + sz else None


def call_graph(img, fns, starts):
    """Direct E8 calls only, as {callee: set(caller entries)}."""
    d = img.d
    va, ptr, rsz = text_section(img)
    out = {}
    for o in range(ptr, ptr + rsz - 5):
        if d[o] != 0xE8:
            continue
        ins = va + (o - ptr)
        target = ins + 5 + struct.unpack_from("<i", d, o + 1)[0]
        caller = containing(fns, starts, ins)
        if caller is not None:
            out.setdefault(target, set()).add(caller)
    return out


def this_calls(img, fns, starts, sizes):
    """{caller: set(callees)} for calls that pass the caller's own `this`.

    A `__thiscall` method saves `this` out of ECX in its prologue - `MOV ESI,ECX`
    and friends - and puts it back with `MOV ECX,ESI` immediately before calling
    another method on the same object. Both halves are two bytes and both are
    unambiguous, so this needs no dataflow.

    What it proves is narrow but useful: the callee is invoked on an object of
    the caller's class. It does NOT prove the callee belongs to that class - a
    base-class method called on a Player is still shared with every other
    subclass. `0x10015a90` is the standing example, reading ordinal 115 for a
    Player and 73 for an NPC. That is why propagation below records every class
    a function is reached from and refuses to name one when there is more than
    one.
    """
    d = img.d
    va, ptr, rsz = text_section(img)
    SAVE = {0xF1: 0xCE, 0xF9: 0xCF, 0xD9: 0xCB}   # MOV esi/edi/ebx,ECX -> MOV ECX,same

    def takes_this(entry):
        """Does the callee actually consume ECX as `this`?

        Without this test the pattern fires on `__stdcall` functions that take
        their object on the stack, where a caller's `MOV ECX,ESI` was setting up
        something else entirely. `0x1005ad00` is the case that caught it: it
        begins `MOV EAX,[ESP+4]`, never touches ECX, and was being attributed to
        Player - which independent evidence had already disproved.

        So require a read of ECX in the first few bytes: `MOV reg,ECX` or a load
        through ECX, both of which are `8B` with the modrm r/m field selecting
        ECX.
        """
        o = img.off(entry)
        if o is None:
            return False
        head = d[o:o + 14]
        for k in range(len(head) - 1):
            if head[k] != 0x8B:
                continue
            modrm = head[k + 1]
            if (modrm & 7) == 1 and (modrm >> 6) != 3:      # [ECX+disp]
                return True
            if modrm in (0xC1, 0xC9, 0xD1, 0xD9, 0xE1, 0xE9, 0xF1, 0xF9):
                return True                                  # MOV reg,ECX
        return False

    out = {}
    for e, sz in fns:
        o = img.off(e)
        if o is None or sz < 8:
            continue
        body = d[o:o + sz]
        # which register the prologue parked `this` in, if any
        restore = None
        for k in range(min(12, len(body) - 1)):
            if body[k] == 0x8B and body[k + 1] in SAVE:
                restore = SAVE[body[k + 1]]
                break
        if restore is None:
            continue
        for k in range(2, len(body) - 5):
            if body[k] != 0xE8:
                continue
            if body[k - 2] == 0x8B and body[k - 1] == restore:
                t = e + k + 5 + struct.unpack_from("<i", body, k + 1)[0]
                if takes_this(t):
                    out.setdefault(e, set()).add(t)
    return out


def propagate(results, edges):
    """Carry an established class along same-`this` calls, to a fixpoint.

    Seeded from functions the count and type tests already pin to one class.
    A function reached from two different classes is left alone - it is shared,
    and naming its ordinals against either class would be wrong for the other.
    """
    reached = {}
    for e, r in results.items():
        if len(r["shortlist"]) == 1:
            reached[e] = set(r["shortlist"])
    changed = True
    while changed:
        changed = False
        for caller, callees in edges.items():
            src = reached.get(caller)
            if not src or len(src) != 1:
                continue
            for callee in callees:
                have = reached.setdefault(callee, set())
                if not src <= have:
                    have |= src
                    changed = True
    return reached


def build(image="rfl"):
    """Sites grouped by function, each site attributed to a class or to none.

    Attribution is per SITE, not per function. A function that handles two
    classes on two branches - `0x10015a90` reads a jaw channel from either a
    Player or an NPC - would otherwise have both its ordinals read against
    whichever class happened to win, and the wrong one names a real but
    unrelated property. That kind of answer is worse than no answer, because
    nothing about it looks wrong.
    """
    sch = schema()
    valid = set(sch)
    img = classdump.Image(IMAGES[image])
    fns, starts = functions(image)
    sizes = dict(fns)

    # No class has more than this many properties, so a higher ordinal cannot
    # be one: the site is a false positive of the scanner. Dropping them is not
    # cosmetic - one bad site poisons a function's whole shortlist, because the
    # shortlist is derived from the highest ordinal used.
    cap = max(len(c["props"]) for c in sch.values())

    per_fn, orphan, rejected = {}, [], 0
    for addr, ordv, isf in find_sites(img):
        if ordv >= cap:
            rejected += 1
            continue
        e = containing(fns, starts, addr)
        if e is None:
            orphan.append((addr, ordv))
        else:
            per_fn.setdefault(e, []).append((addr, ordv, isf))

    graph = call_graph(img, fns, starts)
    # Only Player has more than 154 properties, so an ordinal above that names
    # Player and nothing else. The one place the ceiling alone decides.
    sole_big = [cid for cid in valid if len(sch[cid]["props"]) > 154]
    ceiling_cid = sole_big[0] if len(sole_big) == 1 else None

    results = {}
    for e, hits in sorted(per_fn.items()):
        marks = id_immediates(img, e, sizes[e], valid)
        upstream = set()
        if not marks:
            for caller in graph.get(e, ()):
                upstream |= set(c for _, c in
                                id_immediates(img, caller, sizes.get(caller, 0), valid))
        sites = [{"addr": a, "ordinal": o, "float": f,
                  "class": None, "how": "unattributed"}
                 for a, o, f in sorted(hits)]
        top = max(s["ordinal"] for s in sites)

        # Two constraints, both arithmetic rather than inference.
        #
        #   count   a class must have more properties than the highest ordinal
        #   type    an ordinal taken as a float must BE a float in that class
        #
        # The second is what separates Player from Control Input Names, whose
        # 154 properties are all strings: no string is loaded with FLD.
        short = []
        for c in valid:
            props = sch[c]["props"]
            if len(props) <= top:
                continue
            if any(s["float"] and props[s["ordinal"]]["type"] != "float"
                   for s in sites):
                continue
            short.append(c)
        short.sort()

        for s in sites:
            if len(short) == 1:
                s["class"], s["how"] = short[0], "count and type"
            elif ceiling_cid is not None and s["ordinal"] > 154:
                s["class"], s["how"] = ceiling_cid, "ordinal ceiling"

        results[e] = {
            "size": sizes[e], "sites": sites, "top": top,
            "shortlist": short,
            "ids": sorted(set(c for _, c in marks)),
            "caller_ids": sorted(upstream),
        }

    # Third pass: carry established classes along same-`this` call chains.
    reached = propagate(results, this_calls(img, fns, starts, sizes))
    for e, r in results.items():
        if len(r["shortlist"]) == 1:
            continue
        got = reached.get(e, set())
        if len(got) != 1:
            continue
        cid = list(got)[0]
        # Only if that class can actually hold every ordinal read here.
        props = sch[cid]["props"]
        if r["top"] >= len(props):
            continue
        if any(s["float"] and props[s["ordinal"]]["type"] != "float"
               for s in r["sites"]):
            continue
        # The chain establishes the class of `this` - and NOTHING MORE. It does
        # not say whose properties the function reads. `Player::EquipWeapon`
        # and `Player::StowWeapon` are Player methods reached by exactly such a
        # chain, and both read their ordinals off a `Player Weapon Properties`
        # record returned by a call, where ordinals 0 and 2 are `WeaponChan`
        # and `AmmoChannel` - not Player's `InitialHealth` and `Difficulty`.
        # Naming the sites from the chain produced three functions' worth of
        # real, plausible, wrong property names.
        #
        # So record the class on the function and leave the sites alone.
        # Telling "reads its own properties" from "reads a callee's record"
        # needs dataflow this tool does not do.
        r["shortlist"] = [cid]
        r["this_class"] = cid

    return sch, results, orphan, rejected


def named(sch, s):
    if s["class"] is None:
        return None
    p = sch[s["class"]]["props"][s["ordinal"]]
    return p["key"], p["type"]


def summary(sch, res, orphan, rejected, image):
    total = sum(len(r["sites"]) for r in res.values())
    named_n = sum(1 for r in res.values() for s in r["sites"] if s["class"])
    print("%s: %d property reads in %d functions, %d more outside any function"
          % (image, total, len(res), len(orphan)))
    one = [r for r in res.values() if len(r["shortlist"]) == 1]
    nofit = [r for r in res.values() if not r["shortlist"]]
    print("     %d reads carry a class; %d functions resolve to exactly one"
          % (named_n, len(one)))
    print("     %d functions fit NO single class - they read properties of "
          "several, so a one-class answer would be wrong for at least one read"
          % len(nofit))
    print("     %d candidate sites rejected: ordinal above any class's property count"
          % rejected)
    fn = set(e for e, r in res.items() if any(s["class"] for s in r["sites"]))
    print("     %d functions identified as Player" % len(fn))
    print("\n  the ordinals themselves are exact; the class usually is not.")
    print("  narrowest shortlists (fewest classes big enough to hold the ordinals):")
    rows = sorted(res.items(), key=lambda kv: (len(kv[1]["shortlist"]), -kv[1]["top"]))
    for e, r in rows[:16]:
        names = [sch[c]["name"] for c in r["shortlist"]]
        print("    %#010x  %5d bytes  %2d reads  top ordinal %3d  ->  %s"
              % (e, r["size"], len(r["sites"]), r["top"],
                 ", ".join(names[:4]) + (" ..." if len(names) > 4 else "")))


def show_function(sch, res, want):
    r = res.get(want)
    if not r:
        raise SystemExit("no property reads in %#x" % want)
    print("%#010x  %d bytes  %d reads" % (want, r["size"], len(r["sites"])))
    print("  class ids compared in body : %s"
          % (", ".join(sch[c]["name"] for c in r["ids"]) or "none"))
    print("  classes large enough       : %d  %s"
          % (len(r["shortlist"]),
             ", ".join(sch[c]["name"] for c in r["shortlist"][:6])))
    print()
    for s in r["sites"]:
        if s["class"]:
            p = sch[s["class"]]["props"][s["ordinal"]]
            print("  %#010x  ordinal %3d  %-26s %-20s [%s, %s]"
                  % (s["addr"], s["ordinal"], p["key"], p["type"],
                     sch[s["class"]]["name"], s["how"]))
        else:
            print("  %#010x  ordinal %3d  %s" % (s["addr"], s["ordinal"], s["how"]))


def show_class(sch, res, want):
    cid = None
    for k, v in sch.items():
        if v["name"].lower() == want.lower():
            cid = k
    if cid is None:
        raise SystemExit("no class named %r" % want)
    props = sch[cid]["props"]
    rows = []
    for e, r in res.items():
        if cid not in r["shortlist"]:
            continue
        keys = sorted(set(props[s["ordinal"]]["key"] for s in r["sites"]))
        sure = any(s["class"] == cid for s in r["sites"])
        rows.append((e, r["size"], len(r["shortlist"]), sure, keys))
    print("%s (id %#x, %d properties)" % (sch[cid]["name"], cid, len(props)))
    print("%d functions could be reading it; %d are certain\n"
          % (len(rows), sum(1 for r in rows if r[3])))
    for e, size, n, sure, keys in sorted(rows, key=lambda x: (not x[3], x[2], -len(x[4]))):
        print("  %s %#010x %5d bytes  shortlist %3d  %s"
              % ("**" if sure else "  ", e, size, n,
                 ", ".join(keys[:8]) + (" ..." if len(keys) > 8 else "")))


def write_tables(sch, res, outdir, image):
    path = os.path.join(outdir, "ordinal-map.md")
    with io.open(path, "w", encoding="utf-8", newline="\n") as fh:
        print("Generated by `decomp/tools/ordmap.py --tables`. Do not edit.", file=fh)
        print("", file=fh)
        print("Every authored-property read in `Fellowship.%s`. The ordinal is exact."
              % image, file=fh)
        print("The class is only named where it is certain; see"
              " documentation/ORDINAL-MAP.md.", file=fh)
        print("", file=fh)
        print("| function | size | reads | top ordinal | classes large enough | certain |",
              file=fh)
        print("|---|---:|---:|---:|---:|---|", file=fh)
        for e, r in sorted(res.items()):
            sure = sorted(set(sch[s["class"]]["name"] for s in r["sites"] if s["class"]))
            print("| `%#010x` | %d | %d | %d | %d | %s |"
                  % (e, r["size"], len(r["sites"]), r["top"], len(r["shortlist"]),
                     ", ".join(sure) or "-"), file=fh)
        print("", file=fh)
        print("## Certain: Player", file=fh)
        print("", file=fh)
        print("| function | size | properties |", file=fh)
        print("|---|---:|---|", file=fh)
        for e, r in sorted(res.items()):
            ks = sorted(set(sch[s["class"]]["props"][s["ordinal"]]["key"]
                            for s in r["sites"] if s["class"]))
            if ks:
                print("| `%#010x` | %d | %s |"
                      % (e, r["size"], " ".join("`%s`" % k for k in ks)), file=fh)
    print("wrote %s" % path)


# Facts established by byte-for-byte decompilation, not by this tool. Each one
# has caught a real regression: the attribution passes are heuristics over
# instruction patterns, and every widening of them so far has over-claimed
# somewhere before it was narrowed. `0x1005ad00` is the important one - it is a
# __stdcall free function inside the Player address range that operates on a
# weapon, and any pass that names it Player is wrong.
CHECKS = [
    (0x10057B60, "Player", 113),      # Player::GetMaxPurity, matched
    (0x1005C4E0, "Player", 139),      # Player::GetMaxMana, matched
    # Player::ClearUpperBodyPitch, matched byte for byte, and the class is not in
    # doubt - ordinal 129 is legal only for Player and Control Input Names, and
    # the code compares the value to -1, which is a channel's "none" and could
    # never be a Control Input Names key-name string. The TOOL cannot derive
    # that: the read is not an FLD so the type test says nothing, and no
    # same-this chain reaches the function. Expected as unattributed so the gap
    # stays visible. If a future pass attributes it, this check fires - go and
    # confirm it picked Player and not the other one, then update the line.
    (0x10060210, None, 129),
    # Both are Player methods reading Player's own properties - established by
    # decompilation, NOT derivable here: the chain proves the class of `this`,
    # and whose properties get read is a separate question the tool cannot
    # answer. Expected unattributed so the gap stays visible.
    (0x10057030, None, 11),
    (0x10057060, None, 10),
    # Player methods that read a DIFFERENT class's properties. Any pass that
    # names these from the chain is wrong; that is what this line guards.
    (0x1005A790, None, 0),
    (0x1005A8B0, None, 0),
    (0x1005ADF0, None, 1),
    (0x1005AD00, None, 34),           # NOT Player - proved by the index arithmetic
]


def check(sch, res):
    bad = 0
    for addr, want, ordinal in CHECKS:
        r = res.get(addr)
        if r is None:
            print("  FAIL %#010x  no property reads found" % addr)
            bad += 1
            continue
        site = [s for s in r["sites"] if s["ordinal"] == ordinal]
        if not site:
            print("  FAIL %#010x  ordinal %d not among %s"
                  % (addr, ordinal, [s["ordinal"] for s in r["sites"]]))
            bad += 1
            continue
        got = site[0]["class"]
        name = sch[got]["name"] if got is not None else None
        if name != want:
            print("  FAIL %#010x  ordinal %d attributed to %r, expected %r"
                  % (addr, ordinal, name, want))
            bad += 1
        else:
            print("  ok   %#010x  ordinal %3d  %s" % (addr, ordinal, want or "unattributed"))
    cap = max(len(c["props"]) for c in sch.values())
    high = [(e, s["ordinal"]) for e, r in res.items()
            for s in r["sites"] if s["ordinal"] >= cap]
    if high:
        print("  FAIL %d reads above the %d-property ceiling: %s" % (len(high), cap, high[:5]))
        bad += 1
    else:
        print("  ok   no read exceeds the %d-property ceiling" % cap)
    msg = ("  %d checks failed" % bad) if bad else "  all checks pass"
    print(msg)
    return 1 if bad else 0


def main(argv):
    args = argv[1:]
    image = "rfl"
    if "--image" in args:
        i = args.index("--image")
        image = args[i + 1]
        del args[i:i + 2]
    sch, res, orphan, rejected = build(image)
    if args and args[0] == "--check":
        return check(sch, res)
    if args and args[0] == "--function":
        show_function(sch, res, int(args[1], 16))
    elif args and args[0] == "--tables":
        write_tables(sch, res, args[1], image)
    elif args:
        show_class(sch, res, args[0])
    else:
        summary(sch, res, orphan, rejected, image)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
