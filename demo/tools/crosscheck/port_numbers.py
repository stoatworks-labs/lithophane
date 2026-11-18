"""The page's CPU half against the plugin's: the same numbers, two implementations.

    python3 demo/tools/crosscheck/port_numbers.py [--cases N] [--keep DIR] [--no-contract]

demo/plate.js is a hand port of source/Plate.cpp and source/Controls.cpp. This
compiles plate_print.cpp on those two files as they stand (C++17, -O3: the
plugin's own flags, with Apple clang's default multiply-add contraction, as
lptest and the bundle are built), runs plate_print.mjs on the port under node,
and compares every line:

  - the controls' table (names, groups, kinds, elements, defaults as floats);
  - every conversion on a grid of host values past both ends;
  - the four materials, the multipole's parameters, the profile and its
    variance at five thicknesses;
  - each material's fitted table (111 rows x 15 scales x 3 channels) and the
    fit's worst L1;
  - ExactKernel at 400 variances, as the floats the GPU gets;
  - PlanFor on the defaults at six rasters, every material, the corners of
    the controls, odd rasters, and --cases more drawn at random: the rows, the
    active and delta scales, the factors, the groups and their sizes, the
    uniforms the plan feeds (as floats), and the weight table and kernels as
    uploaded.

How each is judged, and why:

  - strings and integers (names, the plan's structure): equal.
  - floats handed to the GPU, other than the weight table (the kernels, the
    defaults, Thin, Span, Pitch): the same bits.
  - doubles that are a few operations deep (the materials, the multipole's
    parameters, the conversions, the variance): within 1e-12 relative. The
    plugin's build fuses multiply-adds and V8's exp, log, pow and sqrt are not
    Apple's libm, so the last bits may move.
  - the profile: within 1e-12 absolute (it peaks near 1.4 per mm^2; its far
    tail is a sum that cancels to 1e-17 and is noise in both).
  - the fit's weights, and the weight table as uploaded: within --fit
    absolute (default 1e-6; a row sums to 1). The normal equations of 15
    Gaussians a sqrt(2) apart are ill conditioned, and the NNLS carries the
    last bits of exp into the small weights: building the printer with
    --no-contract shows the same worst difference, so it is the conditioning
    and not the contraction.
  - the fit's worst L1: within 1e-5 relative.

Needs node and Apple's c++. Not run by tools/verify.sh (it is part of the
crosscheck, run by hand, like crosscheck.py). Exit 1 on any difference past
its tolerance.
"""
import argparse
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.dont_write_bytecode = True

RASTERS = [(320, 180), (640, 360), (960, 540), (1280, 720), (1920, 1080), (3840, 2160)]


def f32(v):
    return struct.unpack("f", struct.pack("f", v))[0]


def host(v):
    """A host value as text that parses back to the same float on both sides."""
    return repr(f32(v))


def cases(count, seed):
    # The defaults, as Controls.cpp makes them (computed the same way here).
    thin = f32((0.8 - 0.5) / 2.5)
    thick = f32((3.0 - 0.5) / 5.5)
    width = f32((100.0 - 20.0) / 380.0)
    out = []
    for w, h in RASTERS:
        out.append((0, thin, thick, width, w, h))
    for m in range(4):
        for w, h in ((320, 180), (1280, 720)):
            out.append((m, thin, thick, width, w, h))
    # The corners: Thinnest and Thickest at their ends, inverted, equal; the
    # plate width at its ends.
    for m in range(4):
        for a, b in ((0, 0), (0, 1), (1, 0), (1, 1), (thin, thin), (0.5, 0.5)):
            for pw in (0.0, width, 1.0):
                out.append((m, a, b, pw, 1280, 720))
    # Odd rasters (a picture of a visitor's own is any size).
    for w, h in ((1, 1), (7, 5), (1000, 667), (1080, 1920), (4096, 2160)):
        out.append((0, thin, thick, width, w, h))
        out.append((3, 0.0, 1.0, 0.0, w, h))
    rng = random.Random(seed)
    for _ in range(count):
        w, h = rng.choice(RASTERS[:5] + [(1000, 667), (500, 500)])
        out.append((rng.randrange(4), rng.random(), rng.random(), rng.random(), w, h))
    return out


def parse(text):
    rows = []
    for line in text.splitlines():
        key, kind, value = line.split(" ", 2)
        rows.append((key, kind, value))
    return rows


def category(key):
    parts = key.split(".")
    if parts[0] == "plan":
        return "plan." + parts[2]
    if parts[0] == "material":
        return "material." + (parts[2] if parts[2] in ("w", "worstL1", "worstL1At", "Fdr", "name") else parts[3])
    if parts[0] == "conv":
        return "conv." + parts[2]
    return parts[0]


def as_float(text):
    return struct.unpack(">f", bytes.fromhex(text))[0]


def ulps(a, b):
    def ordered(h):
        i = struct.unpack(">i", bytes.fromhex(h))[0]
        return -(i & 0x7FFFFFFF) if i < 0 else i
    return abs(ordered(a) - ordered(b))


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("--cases", type=int, default=300, help="random plans beyond the fixed ones (default 300)")
    parser.add_argument("--seed", type=int, default=20261118)
    parser.add_argument("--fit", type=float, default=1e-6,
                        help="the largest absolute difference allowed in a fitted weight or a table entry (default 1e-6)")
    parser.add_argument("--node", default=shutil.which("node") or "node")
    parser.add_argument("--keep", help="keep both printouts in this directory")
    parser.add_argument("--no-contract", action="store_true",
                        help="build the printer with -ffp-contract=off (not the plugin's flags): tells the "
                             "differences that fused multiply-adds make from the ones libm makes")
    args = parser.parse_args(argv)

    work = tempfile.mkdtemp(prefix="lithophane-numbers-")
    try:
        printer = os.path.join(work, "plate_print")
        build = subprocess.run(
            ["/usr/bin/c++", "-std=c++17", "-O3", *(["-ffp-contract=off"] if args.no_contract else []),
             "-I", os.path.join(REPO, "source"),
             os.path.join(REPO, "source", "Plate.cpp"), os.path.join(REPO, "source", "Controls.cpp"),
             os.path.join(HERE, "plate_print.cpp"), "-o", printer],
            capture_output=True, text=True, check=False)
        if build.returncode != 0:
            sys.exit(f"plate_print.cpp did not build:\n{build.stderr}")
        listed = cases(args.cases, args.seed)
        path = os.path.join(work, "cases.txt")
        with open(path, "w", encoding="utf-8") as handle:
            for m, a, b, pw, w, h in listed:
                handle.write(f"{host(m)} {host(a)} {host(b)} {host(pw)} {w} {h}\n")
        cpp = subprocess.run([printer, path], capture_output=True, text=True, check=False)
        if cpp.returncode != 0:
            sys.exit(f"plate_print failed: {cpp.stderr}")
        js = subprocess.run([args.node, os.path.join(HERE, "plate_print.mjs"), path], capture_output=True, text=True,
                            check=False)
        if js.returncode != 0:
            sys.exit(f"plate_print.mjs failed: {js.stderr}")
        if args.keep:
            os.makedirs(args.keep, exist_ok=True)
            for name, text in (("cpp.txt", cpp.stdout), ("js.txt", js.stdout)):
                with open(os.path.join(args.keep, name), "w", encoding="utf-8") as handle:
                    handle.write(text)
    finally:
        shutil.rmtree(work, ignore_errors=True)

    # (category) -> (what is measured, tolerance): "abs" / "rel" for doubles;
    # the weight table, a float, by "abs"; every other float by its bits.
    rules = {
        "material.w": ("abs", args.fit),
        "plan.table": ("abs", args.fit),
        "material.profile": ("abs", 1e-12),
        "material.worstL1": ("rel", 1e-5),
    }
    a, b = parse(cpp.stdout), parse(js.stdout)
    failures = []
    if len(a) != len(b):
        failures.append(f"the C++ printed {len(a)} lines, the port {len(b)}")
    stats = {}
    for (ka, kinda, va), (kb, kindb, vb) in zip(a, b):
        if (ka, kinda) != (kb, kindb):
            failures.append(f"line order differs: {ka} {kinda} against {kb} {kindb}")
            break
        cat = category(ka)
        how, tol = rules.get(cat, ("rel", 1e-12) if kinda == "d" else ("bits", 0))
        s = stats.setdefault(cat, {"n": 0, "kind": kinda, "how": how, "tol": tol, "differ": 0, "worst": 0.0,
                                   "ulps": 0, "at": None})
        s["n"] += 1
        # Doubles by value: %.17g and JS's toPrecision can spell one number two ways.
        if va == vb or (kinda == "d" and float(va) == float(vb)):
            continue
        s["differ"] += 1
        if kinda in ("s", "i"):
            s["worst"] = float("inf")
            s["at"] = s["at"] or f"{ka}: {va} against {vb}"
            continue
        if kinda == "f":
            s["ulps"] = max(s["ulps"], ulps(va, vb))
            x, y = as_float(va), as_float(vb)
        else:
            x, y = float(va), float(vb)
        gap = abs(x - y) if how in ("abs", "bits") else abs(x - y) / max(abs(x), abs(y))
        if how == "bits":
            gap = float("inf")
        if gap > s["worst"] or s["at"] is None:
            s["worst"], s["at"] = max(gap, s["worst"]), f"{ka}: {va} against {vb}"

    plans = sum(1 for k, _, _ in a if k.endswith(".pitch"))
    print(f"{len(a)} numbers; {plans} plans ({plans - args.cases} fixed, {args.cases} random, seed {args.seed})"
          + (" -- the printer built WITHOUT contraction (not the plugin's flags)" if args.no_contract else "") + "\n")
    print(f"{'what':<22} {'kind':<6} {'count':>8} {'differ':>8}  {'judged by':<22} worst")
    for cat in sorted(stats):
        s = stats[cat]
        kind = {"s": "string", "i": "int", "f": "float", "d": "double"}[s["kind"]]
        judged = ("equal" if s["kind"] in ("s", "i") else "the same bits" if s["how"] == "bits" else
                  f"{s['tol']:g} {'absolute' if s['how'] == 'abs' else 'relative'}")
        worst = ""
        if s["differ"] and s["kind"] in ("f", "d") and s["how"] != "bits":
            worst = f"{s['worst']:.2g}" + (f" ({s['ulps']} ulp)" if s["kind"] == "f" else "")
        elif s["differ"]:
            worst = "DIFFERS"
        print(f"{cat:<22} {kind:<6} {s['n']:>8} {s['differ']:>8}  {judged:<22} {worst}")
        if s["differ"] and s["worst"] > s["tol"]:
            failures.append(f"{cat}: {s['differ']} differ, the worst {s['at']}")
    print()
    if failures:
        print("FAIL")
        for line in failures:
            print(f"  {line}")
        return 1
    print("every string, integer and float the GPU gets equal; the fit and the weight table within "
          f"{args.fit:g} absolute; every other double within its tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
