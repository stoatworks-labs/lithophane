"""The browser demo against the plugin: the same frames, two renderers, one picture.

    python3 demo/tools/crosscheck/crosscheck.py [--lptest build/lptest] [--size 320x180] [--keep DIR]

Each case below is a short run of frames rendered twice:

  - by the PLUGIN: `lptest --pipe` (the real LithophanePlugin class in a GL 4.1
    context), fed the case's clip with the case's settings (`--set`) and cues
    (`--script`). Build lptest first (tools/verify.sh leaves one in
    build-verify/, or configure with -DLITHOPHANE_BUILD_TOOLS=ON).
  - by the PAGE: demo/ served from here and loaded in headless Chrome,
    through window.lithophaneDemo.begin, which makes a fresh instance and runs
    the same frames with the same settings and cues, reading its float output
    back and rounding it as lptest rounds its own.

The clips are the harness's card, taken from lptest itself rather than rebuilt
here (with Mix at 0 the plugin hands its clip through to the bit, so
`lptest --film N --set Mix=0` IS the card), and the kit's own generated clips,
drawn by the page and handed to lptest byte for byte -- the alpha clip among
them, so the premultiplied bytes the page holds are what both sides carve. The
page's own card (plugin.js's GLSL port of buildCard, the page's default clip)
is compared with lptest's card too, and passes within --max levels at the
page's own sizes: it is a float port of a picture the harness draws in double.
At an odd raster (333x187 here) the edge of one of the skyline's buildings can
land on a pixel's centre and fall the other way; that is reported, not failed.

The GPU half is the plugin's GLSL (check_shaders.py), so what this mostly
tests is the PORT: Plate.cpp's plan (which scales are active, which go to the
delta column, each one's factor, the groups, the weight table as uploaded,
the kernels), Controls.cpp's conversions and Lithophane.cpp's passes, texture
units and uniforms. demo/tools/crosscheck/port_numbers.py checks the plan's
numbers on their own; this checks the pictures they make. A difference is a
port bug, or a difference between GL 4.1 on this machine and WebGL2 through
ANGLE on the same GPU. Before any picture, the page's table of controls
(names, kinds, defaults) is compared with `lptest --list`, and every option
element the page names is handed to lptest by name, which refuses one it does
not know.

`--input float` hands the page the clip as lptest holds it (RGBA32F, the codes
/ 255) instead of as every clip on the page is held (RGBA8), to tell a
difference of representation from one of arithmetic. `--cpp-weights` hands the
page the C++'s own fitted weights (plate_print.cpp, built on Plate.cpp as
port_numbers.py builds it) in place of the page's fit, to tell a difference the
fit's last bits make (port_numbers.py: up to 7.4e-8 in a weight) from one the
rest makes.

A case passes when no pixel of any frame is more than --max levels of 255
apart and --exact % of them are identical. The negative controls -- the page a
little wrong: one setting one slider step (0.001) off -- are each compared
with the plugin's run as it should be, and each must fail that test, so the
comparison is shown to see a slip of that size.

Not covered, because --pipe cannot do it: a change of raster under a running
instance (the plan is remade from scratch for a new raster in both, and the
page's resolutions each have a case of their own here).

The carve pass declares the weight table's uniforms (they are in kCommon, which
every fragment stage shares) and never reads them, so the compiler drops them
and the page's set calls on them miss, as the plugin's glUniform calls on
location -1 do: reported, not failed.

Not run by tools/verify.sh: it needs a GPU and Chrome. Exit 1 if a table
differs, a uniform is missing (outside carve's table) or unset, a case fails
or a negative control passes. Figures: AGENTS.md, "Browser demo".
"""
import argparse
import functools
import http.server
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, "..", ".."))
REPO = os.path.abspath(os.path.join(DEMO, ".."))
sys.dont_write_bytecode = True

# name, --set settings (host values; options by element name), cues as
# (frame, parameter, value) -- options by index --, frames, the raster (None:
# --size), and the clip: 'card' (lptest's), or a kit clip's id (drawn by the
# page at t = 0, 1.5, 3, ... s).
CASES = [
    ("defaults", {}, [], 2, None, "card"),
    ("feldspar", {"Material": "Feldspar Porcelain"}, [], 1, None, "card"),
    ("clay", {"Material": "Clay Porcelain"}, [], 1, None, "card"),
    ("white-pla", {"Material": "White PLA"}, [], 1, None, "card"),
    ("linear-light", {"Mapping": "Linear Light"}, [], 1, None, "card"),
    ("flat-0.5mm", {"Thinnest": "0", "Thickest": "0"}, [], 1, None, "card"),
    ("thick-3-6mm", {"Thinnest": "1", "Thickest": "1"}, [], 1, None, "card"),
    ("inverted", {"Thinnest": "1", "Thickest": "0"}, [], 1, None, "card"),
    ("width-20mm", {"Plate Width": "0"}, [], 1, None, "card"),
    ("width-400mm", {"Plate Width": "1"}, [], 1, None, "card"),
    ("back-0", {"Back Light": "0"}, [], 1, None, "card"),
    ("back-16", {"Back Light": "1"}, [], 1, None, "card"),
    ("front-lit", {"Back Light": "0", "Front Light": "0.5"}, [], 1, None, "card"),
    ("front-facing-lamp", {"Back Light": "0", "Front Light": "0.5", "Relief Side": "Facing Lamp"}, [], 1, None, "card"),
    ("front-raking", {"Front Light": "1", "Light Angle": "0", "Light Elevation": "0"}, [], 1, None, "card"),
    ("front-overhead", {"Front Light": "0.5", "Light Angle": "0.6", "Light Elevation": "1"}, [], 1, None, "card"),
    ("mix-half", {"Mix": "0.5"}, [], 1, None, "card"),
    ("cues", {}, [(1, "Material", 2), (2, "Mapping", 1), (0, "Plate Width", 0.2105), (3, "Plate Width", 0.0)], 4, None, "card"),
    ("defaults-640x360", {}, [], 1, "640x360", "card"),
    ("defaults-960x540", {}, [], 1, "960x540", "card"),
    ("defaults-1280x720", {}, [], 1, "1280x720", "card"),
    ("defaults-1920x1080", {}, [], 1, "1920x1080", "card"),
    ("defaults-333x187", {}, [], 1, "333x187", "card"),
    ("most-groups-1280x720", {"Plate Width": "0", "Thickest": "1", "Material": "White PLA"}, [], 1, "1280x720", "card"),
    ("clip-scene", {}, [], 2, None, "scene"),
    ("clip-spot", {}, [], 2, None, "spot"),
    ("clip-ramp", {}, [], 2, None, "ramp"),
    ("clip-bars", {}, [], 1, None, "bars"),
    ("clip-grid", {}, [], 1, None, "grid"),
    ("clip-detail", {}, [], 1, None, "detail"),
    ("clip-alpha", {}, [], 2, None, "alpha"),
    ("clip-alpha-mix-half", {"Mix": "0.5"}, [], 1, None, "alpha"),
    ("clip-scene-front", {"Back Light": "0.2", "Front Light": "0.6"}, [], 1, None, "scene"),
]
# The negative controls: the page a little wrong, compared with the plugin's
# run of the base case as it should be. (name, base, settings changed, label)
NEGATIVES = [
    ("thickest-1-step", "defaults", {"Thickest": "0.4555"}, "Thickest one slider step (0.001) off"),
    ("thinnest-1-step", "defaults", {"Thinnest": "0.1210"}, "Thinnest one step off"),
    ("back-1-step", "defaults", {"Back Light": "0.4519"}, "Back Light one step off"),
    ("width-1-step", "defaults", {"Plate Width": "0.2115"}, "Plate Width one step off"),
    ("angle-1-step", "front-lit", {"Light Angle": "0.3760"}, "Light Angle (front-lit) one step off"),
]
CLIP_TIME_STEP = 1.5
# The kit's Composition sizes (demo.js RESOLUTIONS): where the page's own card must match lptest's.
PAGE_RASTERS = {"640x360", "960x540", "1280x720", "1920x1080"}
KINDS = {"Material": "option", "Mapping": "option", "Relief Side": "option"}


def parse_size(text):
    w, h = text.lower().split("x")
    return int(w), int(h)


def run_lptest(lptest, args, env, stdin=None):
    done = subprocess.run([lptest, *args], input=stdin, capture_output=True, check=False, env=env)
    if done.returncode != 0:
        sys.exit(f"lptest {' '.join(args)} failed: {done.stderr.decode(errors='replace').strip()}")
    return done.stdout, done.stderr.decode(errors="replace")


def value_at(track, frame, ramp):
    """main.cpp's valueAt, in float as there."""
    f32 = np.float32
    if frame <= track[0][0]:
        return f32(track[0][1])
    if frame >= track[-1][0]:
        return f32(track[-1][1])
    for (fa, va), (fb, vb) in zip(track, track[1:]):
        if fa <= frame < fb:
            if not ramp:
                return f32(va)
            t = f32(frame - fa) / f32(fb - fa)
            return f32(va) + (f32(vb) - f32(va)) * t
    return f32(track[-1][1])


def cue_changes(cues, frames):
    """What lptest's --pipe sets before each frame: a track from its first key on,
    or from frame 0 if it ramps (a STANDARD control)."""
    tracks = {}
    for frame, name, value in cues:
        tracks.setdefault(name, []).append((frame, float(value)))
    out = []
    for n in range(frames):
        changes = {}
        for name, track in tracks.items():
            track.sort(key=lambda kv: kv[0])
            ramp = KINDS.get(name, "standard") == "standard"
            if n >= track[0][0] or ramp:
                changes[name] = float(value_at(track, n, ramp))
        out.append(changes)
    return out


# ---------------------------------------------------------------- the server
class Handler(http.server.SimpleHTTPRequestHandler):
    """demo/ as it is, plus /xc/ for the job, the frames and the page's answers."""

    def __init__(self, *args, state, **kwargs):
        self.state = state
        super().__init__(*args, directory=DEMO, **kwargs)

    def log_message(self, *args):
        pass

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if self.path == "/xc/job.json":
            return self.reply(json.dumps(self.state["job"]).encode(), "application/json")
        if self.path.startswith("/xc/frames/"):
            return self.reply(self.state["frames"][self.path[len("/xc/frames/"):]], "application/octet-stream")
        return super().do_GET()

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/xc/out/"):
            self.state["page"][self.path[len("/xc/out/"):]] = body
        elif self.path.startswith("/xc/clip/"):
            self.state["clips"][self.path[len("/xc/clip/"):]] = body
        elif self.path == "/xc/table":
            self.state["table"] = json.loads(body)
        elif self.path == "/xc/missing":
            self.state["missing"] = json.loads(body)
        elif self.path == "/xc/error":
            self.state["error"] = body.decode(errors="replace")
            self.state["finished"].set()
        elif self.path == "/xc/done":
            self.state["finished"].set()
        self.reply(b"ok", "text/plain")

    def reply(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------- the judging
def compare(a, b, width, height):
    """Every frame of two runs: the share identical, within 1, the worst
    difference and the frame it is on, and the mean over colour."""
    x = np.frombuffer(a, np.uint8).reshape(-1, height, width, 4).astype(np.int16)
    y = np.frombuffer(b, np.uint8).reshape(-1, height, width, 4).astype(np.int16)
    worst = np.abs(x - y).max(axis=3)
    per_frame = worst.reshape(worst.shape[0], -1).max(axis=1)
    return {
        "exact": float((worst == 0).mean() * 100.0),
        "within1": float((worst <= 1).mean() * 100.0),
        "max": int(worst.max()),
        "worst_frame": int(per_frame.argmax()),
        "frames_off": int((per_frame > 0).sum()),
        "pixels_off": int((worst > 0).sum()),
        "mean": float(np.abs(x - y)[..., :3].mean()),
    }


def passes(s, args):
    return s["max"] <= args.max and s["exact"] >= args.exact


LIST_ROW = re.compile(r"^(\d+)\s+(.+?)\s{2,}(standard|option|event|text|boolean)\s+(\S+)\s*$")


def compare_table(lptest, page_table, env):
    """`lptest --list` against the page's table: name, kind, default (4 dp), in
    order, up to the About block; then every option element the page names,
    handed to lptest by name, which refuses one it does not know."""
    rows = []
    for line in run_lptest(lptest, ["--list"], env)[0].decode().splitlines()[1:]:
        m = LIST_ROW.match(line.rstrip())
        if m is None:
            continue
        index, name, kind, value = m.groups()
        if name == "About":
            break
        rows.append((int(index), name, kind, float(value)))
    problems = []
    if len(rows) != len(page_table):
        problems.append(f"lptest lists {len(rows)} controls before the About block, the page {len(page_table)}")
    for (index, name, kind, value), mine in zip(rows, page_table):
        if (name, kind) != (mine["name"], mine["kind"]) or abs(value - mine["value"]) > 0.00005:
            problems.append(f"#{index}: lptest says {name} {kind} {value:.4f}, the page "
                            f"{mine['name']} {mine['kind']} {mine['value']:.4f}")
    elements = 0
    for mine in page_table:
        for e in mine["elements"]:
            done = subprocess.run([lptest, "--film", "1", "--size", "16x9", "--set", f"{mine['name']}={e}"],
                                  capture_output=True, check=False, env=env)
            elements += 1
            if done.returncode != 0:
                problems.append(f"lptest refuses {mine['name']}={e}: {done.stderr.decode(errors='replace').strip()}")
    return len(rows), elements, problems


def cpp_weights(work):
    """Each material's fitted weights from the C++ (Plate.cpp's TableFor), as
    plate_print.cpp prints them (%.17g, which a double reads back exactly)."""
    printer = os.path.join(work, "plate_print")
    build = subprocess.run(["/usr/bin/c++", "-std=c++17", "-O3", "-I", os.path.join(REPO, "source"),
                            os.path.join(REPO, "source", "Plate.cpp"), os.path.join(REPO, "source", "Controls.cpp"),
                            os.path.join(HERE, "plate_print.cpp"), "-o", printer],
                           capture_output=True, text=True, check=False)
    if build.returncode != 0:
        sys.exit(f"plate_print.cpp did not build:\n{build.stderr}")
    empty = os.path.join(work, "no-cases.txt")
    open(empty, "w").close()
    text = subprocess.run([printer, empty], capture_output=True, text=True, check=True).stdout
    out = {}
    for line in text.splitlines():
        key, kind, value = line.split(" ", 2)
        parts = key.split(".")
        if parts[0] == "material" and parts[2] == "w":
            out.setdefault(parts[1], []).append(float(value))
        elif parts[0] == "material" and parts[2] == "worstL1":
            out.setdefault(f"{parts[1]}.worstL1", float(value))
    return out


def plugin_run(lptest, name, settings, cues, size, frames_in, work, env):
    sets = [x for k, v in settings.items() for x in ("--set", f"{k}={v}")]
    script = []
    if cues:
        path = os.path.join(work, f"{name}.cues")
        with open(path, "w") as handle:
            handle.write("".join(f"{f} {n} {v}\n" for f, n, v in cues))
        script = ["--script", path]
    out, _ = run_lptest(lptest, ["--pipe", "--size", size, *sets, *script], env, stdin=frames_in)
    if len(out) != len(frames_in):
        sys.exit(f"lptest --pipe for {name} wrote {len(out)} bytes for {len(frames_in)} in")
    return out


def main(argv):
    parser = argparse.ArgumentParser()
    default_lptest = os.path.join(REPO, "build-verify", "lptest")
    if not os.access(default_lptest, os.X_OK):
        default_lptest = os.path.join(REPO, "build", "lptest")
    parser.add_argument("--lptest", default=default_lptest)
    parser.add_argument("--size", default="320x180", help="the raster of every case that does not name one")
    parser.add_argument("--keep", help="write each case's worst frame, both pictures, here as PNGs")
    parser.add_argument("--max", type=int, default=1,
                        help="the largest difference, in levels of 255, allowed anywhere (default 1)")
    parser.add_argument("--exact", type=float, default=99.0,
                        help="share of pixels, %%, that must be identical (default 99)")
    parser.add_argument("--only", help="run only the cases whose names contain this")
    parser.add_argument("--input", choices=("rgba8", "float"), default="rgba8",
                        help="how the page holds the clip: rgba8, as every clip on the page is (default), or "
                             "float, RGBA32F holding the same codes / 255, as lptest --pipe holds it")
    parser.add_argument("--cpp-weights", action="store_true",
                        help="hand the page the C++'s fitted weights instead of its own fit (a diagnostic)")
    parser.add_argument("--chrome", default=os.environ.get(
        "CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--port", type=int, default=0, help="the local server's port (default: any free one)")
    args = parser.parse_args(argv)
    if not os.access(args.lptest, os.X_OK):
        sys.exit(f"no lptest at {args.lptest}: build it (tools/verify.sh, or configure with "
                 "-DLITHOPHANE_BUILD_TOOLS=ON and build the lptest target)")

    work = tempfile.mkdtemp(prefix="lithophane-xc-")
    # The harness writes the plugin's log; keep it out of ~/Library/Logs.
    env = {**os.environ, "LITHOPHANE_LOG_DIR": os.path.join(work, "log")}

    cases = [c for c in CASES if not args.only or args.only in c[0]]
    negatives = [n for n in NEGATIVES if any(c[0] == n[1] for c in cases)]

    # The harness's card at each raster: Mix 0 hands the clip through to the bit.
    need = {}
    for name, _, _, frames, size, clip in cases:
        size = size or args.size
        need[size] = max(need.get(size, 0), frames if clip == "card" else 0)
    need[args.size] = max(need.get(args.size, 0), 1)
    frames_by_key = {}
    for size, count in need.items():
        if count:
            out, _ = run_lptest(args.lptest, ["--film", str(count), "--size", size, "--set", "Mix=0"], env)
            frames_by_key[size] = out

    job_cases = []
    for name, settings, cues, frames, size, clip in cases:
        size = size or args.size
        width, height = parse_size(size)
        job_cases.append({"name": name, "settings": settings, "width": width, "height": height, "cards": size,
                          "clip": clip, "frames": frames, "indices": list(range(frames)),
                          "times": [n * CLIP_TIME_STEP for n in range(frames)], "changes": cue_changes(cues, frames)})
    by_name = {c["name"]: c for c in job_cases}
    for name, base, change, _ in negatives:
        job_cases.append({**by_name[base], "name": name, "settings": {**by_name[base]["settings"], **change}})

    card_jobs = [{"name": size, "width": parse_size(size)[0], "height": parse_size(size)[1]}
                 for size in frames_by_key]
    job = {"cases": job_cases, "cards": card_jobs, "input": args.input, "weights": None}
    if args.cpp_weights:
        job["weights"] = cpp_weights(work)

    state = {"job": job, "frames": frames_by_key, "page": {}, "clips": {}, "table": None, "missing": None,
             "error": None, "finished": threading.Event()}
    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), functools.partial(Handler, state=state))
    port = server.server_address[1]
    threading.Thread(target=server.serve_forever, daemon=True).start()
    profile = tempfile.mkdtemp(prefix="lithophane-chrome-")
    chrome = subprocess.Popen([
        args.chrome, "--headless=new", "--no-first-run", "--no-default-browser-check",
        f"--user-data-dir={profile}", "--use-angle=metal", "--enable-gpu", "--ignore-gpu-blocklist",
        "--window-size=1600,1200", f"http://127.0.0.1:{port}/tools/crosscheck/",
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not state["finished"].wait(1200):
            sys.exit("the page never finished (1200 s)")
    finally:
        chrome.terminate()
        try:
            chrome.wait(10)
        except subprocess.TimeoutExpired:
            chrome.kill()
        server.shutdown()
        shutil.rmtree(profile, ignore_errors=True)
    if state["error"]:
        shutil.rmtree(work, ignore_errors=True)
        sys.exit(f"the page failed:\n{state['error']}")

    # The plugin's runs, on the same frames: the card from lptest, a kit clip
    # as the page drew it.
    plugin = {}
    clip_inputs = {}
    for c in job_cases:
        size = f"{c['width']}x{c['height']}"
        one = c["width"] * c["height"] * 4
        if c["clip"] == "card":
            frames_in = frames_by_key[size][: one * c["frames"]]
        else:
            frames_in = state["clips"].get(c["name"])
            if frames_in is None:
                sys.exit(f"the page posted no clip for {c['name']}")
        clip_inputs[c["name"]] = frames_in
    for name, settings, cues, frames, size, clip in cases:
        size = size or args.size
        plugin[name] = plugin_run(args.lptest, name, settings, cues, size, clip_inputs[name], work, env)

    failures = 0
    count, elements, table_problems = compare_table(args.lptest, state["table"] or [], env)
    if table_problems:
        failures += len(table_problems)
        print("the page's controls differ from lptest's:")
        for line in table_problems:
            print(f"  {line}")
    else:
        print(f"controls: all {count} of lptest --list's before the About block, same names, kinds and defaults, "
              f"in order; all {elements} option elements accepted by lptest by name")
    gaps = state["missing"] or {}
    dropped = {"carve: Table", "carve: RowOrigin", "carve: RowStep", "carve: RowCount"}
    missing = [u for u in gaps.get("missing", []) if u not in dropped]
    carve_dropped = sorted(u for u in gaps.get("missing", []) if u in dropped)
    for label, items in (("uniforms the page set that a program lacks", missing),
                         ("uniforms a program has that the page never set", gaps.get("unset")),
                         ("GL errors", gaps.get("errors")), ("frames refused or unallocated", gaps.get("refused")),
                         ("plans truncated at eight groups", gaps.get("truncated"))):
        if items:
            failures += 1
            print(f"{label}: {', '.join(sorted(set(items))[:20])}")
    print(f"the page's weights: {gaps.get('weights', 'its own fit')}")
    if not (missing or gaps.get("unset") or gaps.get("errors") or gaps.get("refused") or gaps.get("truncated")):
        print("every uniform set, none missing, no GL error, no frame refused, no plan truncated"
              + (f" (carve declares and never reads {', '.join(u.split(': ')[1] for u in carve_dropped)}: "
                 "dropped by the compiler, as in the plugin)" if carve_dropped else ""))

    if args.keep:
        from PIL import Image
        os.makedirs(args.keep, exist_ok=True)

    print("\nevery frame of each run; == identical, <=1 within one level of 255, max the worst anywhere\n")
    print(f"{'case':<24} {'raster':>9} {'clip':>6} {'frames':>6} {'==':>9} {'<=1':>9} {'max':>4} {'mean':>7}  groups")
    for name, _, _, frames, size, clip in cases:
        size = size or args.size
        width, height = parse_size(size)
        got = state["page"].get(name)
        if got is None or len(got) != len(plugin[name]):
            print(f"{name:<24} no picture from the page")
            failures += 1
            continue
        s = compare(plugin[name], got, width, height)
        ok = passes(s, args)
        failures += 0 if ok else 1
        groups = " ".join(f"{g['factor']}:{len(g['scales'])}" for g in (gaps.get("groups") or {}).get(name, []))
        print(f"{name:<24} {size:>9} {clip:>6} {frames:>6} {s['exact']:8.4f}% {s['within1']:8.4f}% {s['max']:>4} "
              f"{s['mean']:7.4f}  {groups:<22} {'ok' if ok else 'FAIL'}"
              + (f"  ({s['pixels_off']} pixels on {s['frames_off']} frames not identical, the worst on frame "
                 f"{s['worst_frame']})" if s["max"] else ""))
        if args.keep and s["max"]:
            n = s["worst_frame"]
            one = width * height * 4
            for label, data in (("plugin", plugin[name]), ("page", got), ("clip", clip_inputs[name])):
                Image.frombytes("RGBA", (width, height), data[n * one:(n + 1) * one]).save(
                    os.path.join(args.keep, f"{name}-{n}-{label}.png"))

    print(f"\nthe page's own card (plugin.js, float) against the harness's (double), within {args.max} level(s)")
    for k in state["job"]["cards"]:
        w, h = k["width"], k["height"]
        mine = state["page"].get(f"card-{k['name']}")
        theirs = frames_by_key[k["name"]][: w * h * 4]
        s = compare(theirs, mine, w, h) if mine else None
        # A GLSL port in float of a picture the harness makes in double and
        # rounds with lround: within --max levels is the test, not identity. At
        # a raster the page does not offer, a building's edge in the skyline can
        # land on a pixel's centre and fall the other way in float: reported,
        # not failed (the page's clip, not the port; the cases above hand both
        # sides lptest's card).
        offered = k["name"] in PAGE_RASTERS or k["name"] == args.size
        ok = s is not None and s["max"] <= args.max
        failures += 0 if ok or (s is not None and not offered) else 1
        verdict = "ok" if ok else "FAIL" if offered else "differs (not one of the page's sizes: reported, not failed)"
        print(f"  {k['name']:>9}  " + (f"{s['exact']:8.4f}% {s['within1']:8.4f}% {s['max']:>4} {s['mean']:7.4f}  "
                                         f"{verdict}" + (f" ({int(round((100 - s['within1']) / 100 * w * h))} pixels "
                                                         f"more than {args.max} apart)" if not ok else "")
                                         if s else "no picture"))

    print("\nnegative controls: the page a little wrong, against the plugin as it should be")
    for name, base, _, label in negatives:
        got = state["page"].get(name)
        c = by_name[base]
        s = compare(plugin[base], got, c["width"], c["height"]) if got else None
        caught = s is not None and not passes(s, args)
        failures += 0 if caught else 1
        print(f"  {label:<38} " + (f"{s['exact']:8.4f}% {s['within1']:8.4f}% {s['max']:>4} {s['mean']:7.4f}  "
                                     f"{'caught' if caught else 'NOT CAUGHT'}" if s else "no picture"))
    print(f"\n{'every case passes, every negative control is caught' if not failures else f'{failures} problem(s)'}"
          f" (pass: no pixel more than {args.max} level(s) apart, {args.exact}% identical)")
    shutil.rmtree(work, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
