#!/usr/bin/env python3
"""
try.py - compile one source and check one function against the original.

The inner loop for working on a single function. `build.py` checks the whole
manifest and is what proves nothing regressed; this checks one thing quickly
and is what you run twenty times in a row.

  python try.py <source.cpp> <va> <size> <symbol> [--image rfl|exe] [--flags "/O2 /Gy /GX"]
  python try.py <source.cpp> <va>                 [--flags ...]

The second form looks image, size and symbol up in manifest.tsv, for functions
that are already listed. The first is for a function you are adding; it
defaults to the rfl, so pass --image exe for one in Fellowship.exe.

Objects are written to build\\obj\\<source-stem>.obj, so two people or agents
working on different source files never collide. Two working on the *same*
source file will, which is why a source file belongs to one worker at a time.

Exit status is 0 on a match and 1 otherwise, so it drops straight into a loop.
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


def from_manifest(va):
    path = os.path.join(HERE, "manifest.tsv")
    with open(path) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) == 6 and int(parts[1], 16) == va:
                return parts[0], int(parts[2]), parts[5]
    return None


def main(argv):
    args, flags, want_image = [], "/O2 /Gy /GX", None
    i = 1
    while i < len(argv):
        if argv[i] == "--flags" and i + 1 < len(argv):
            flags = argv[i + 1]
            i += 2
        elif argv[i] == "--image" and i + 1 < len(argv):
            want_image = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1

    if len(args) not in (2, 4):
        print(__doc__.strip())
        return 2

    source = args[0]
    va = int(args[1], 16)

    if len(args) == 4:
        image, size, symbol = want_image or "rfl", int(args[2]), args[3]
    else:
        found = from_manifest(va)
        if not found:
            print(f"  {va:#x} is not in manifest.tsv - pass size and symbol explicitly")
            return 2
        image, size, symbol = found
        if want_image:
            image = want_image

    if image not in IMAGES:
        print(f"  unknown image {image!r} - expected one of {sorted(IMAGES)}")
        return 2

    src_path = source if os.path.isabs(source) else os.path.join(HERE, "src", source)
    if not os.path.exists(src_path):
        print(f"  no such source: {src_path}")
        return 2

    # Mirror the source tree under build\obj\ so two modules may hold a
    # same-named file without their objects colliding.
    obj = os.path.join(HERE, "build", "obj", os.path.splitext(source)[0] + ".obj")
    os.makedirs(os.path.dirname(obj), exist_ok=True)

    cl = os.path.join(VC6, "bin", "CL.EXE")
    if not os.path.exists(cl):
        print(f"  no CL.EXE under {VC6} - set VC6 to the portable toolchain")
        return 2

    env = dict(os.environ)
    env["PATH"] = os.path.join(VC6, "bin") + os.pathsep + env.get("PATH", "")
    env["INCLUDE"] = os.path.join(VC6, "include")
    env["LIB"] = os.path.join(VC6, "lib")

    r = subprocess.run(
        [cl, "/nologo", "/c"] + flags.split() + [src_path, "/Fo" + obj],
        capture_output=True, text=True, env=env, cwd=os.path.dirname(obj),
    )
    if r.returncode != 0:
        print("  COMPILE FAILED")
        for line in (r.stdout + r.stderr).splitlines():
            if line.strip() and not line.strip().endswith(".cpp"):
                print("   ", line.rstrip())
        return 2

    return matchtool.compare(IMAGES[image], va, obj, symbol, size)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
