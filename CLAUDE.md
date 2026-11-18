# lithophane

The clip carved into a backlit porcelain plate, for Resolume Arena/Avenue as one FFGL effect.
Thickness from darkness (Linear Depth, or Cura's Linear Light); each column passes
exp(−k d) of each channel, red further than blue (Sugiyama's porcelain bodies, or White PLA);
the light spreads by the multipole slab profile of the column's own thickness, wider through
thicker plate, fitted by a mixture of 15 Gaussians and applied at several resolutions; a
front lamp shows the carved relief and the faint negative of the plate's reflectance.
C++/GLSL, CMake MODULE → universal `.bundle` (macOS). MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `LP01` | `SW Lithophane` | `Lithophane.bundle`, `com.stoatworks.ffgl.lithophane` | effect |

Read `AGENTS.md` before touching the fit (`Plate.cpp`: `fit`, `kHold`, `kImagePairs`), the
frame plan (`PlanFor`: the factor rule, the delta column, the groups), the decimation and
reconstruction (`Shaders.cpp`: spread, pass, `level()`), the intermediates' format
(`MultiTarget.h`) or Back Light's default. The spec is
`~/Projects/resolume/specs/SPEC-lithophane.md` and is kept in step with this repo.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DLITHOPHANE_BUILD_TOOLS=ON`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/lptest --out /tmp/l.png` (the defaults over the harness's card;
  one frame: nothing moves)
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/lptest --list`
- Set anything by name: `./build/lptest --set "Material=White PLA" --set "Front Light=0.5"`
  (a standard control takes the host's 0..1; an option takes its name; anything unparseable,
  nan and inf included, is refused with exit 2)
- A clip through it: `ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/lptest --pipe --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
- `--stats` prints each frame's 99th-percentile output luminance and brightest channel
  (linear, from the float readback) on stderr, and the same pooled over every frame at the end.
- Back Light's default, re-measured: `python3 tools/clips.py defaults` (all 33 of Arena's demo
  clips; about 40 s). The alpha check on two of them: `python3 tools/clips.py alpha`.

## Verify
- Everything: `TMPDIR=<a writable dir> OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved
  words, `glslc` on all 5 shaders, the submodule pin, a fresh universal build with no warnings
  outside external/ffgl, lipo, plist, ad-hoc signature, `oxbow probe` and `selftest` (required,
  by name: there is no default path, and no oxbow is a failure); every check at 1280x720 and
  320x180; every check again on Apple's software renderer at 320x180; `--offline`; the alpha
  check and Back Light's default on Arena's clips; `--pipe`; the negative controls; the
  mutants; the sweep; the bench). `KEEP_BUILD=1` reuses `build-verify/`. It needs Resolume
  Arena's demo clips in `/Applications/Resolume Arena/media`.
- Every check at once: `./build/lptest --all`. One at a time (`--size WxH`: that raster alone):
  - **No GL**: `--model` (the materials back to the spec's table, the transport coefficients,
    the profile against the harness's own sum, the closed-form moments against quadrature, the
    fit's L1 and variance, the kernels, the B-spline against the true Gaussian, 300 frame
    plans, the K–M reflectance, the conversions).
  - **Back light**: `--attenuation` (flat cards of eight codes: ln(out) against d is a line of
    slope −k per channel, every material, both mappings), `--blur` (a one-pixel column and row
    against the multipole's line spread: L1 and second moment, at many phases; the radius
    grows with thickness; then against the plugin's own mixture, the tight measure of the
    passes; a line midway between table rows).
  - **Front light**: `--front` (no clip colour; Lambert's ratio along x and along y; facing the
    lamp, no shading; thin against thick by Kubelka–Munk).
  - **Host**: `--edges` (flat to the frame's edges; one-sided slopes there), `--alpha`,
    `--clock` (host time never read), `--resize` (no state), `--glstate`, `--robust` (a wild
    clip renders as its tamed copy; every control at its ends is finite; Mix 0 is the clip).
- The checks can fail: `./build/lptest --negative` (16 wrong models), `tools/mutate.sh`
  (54 mutants, arm64, 320x180 unless a mutant names its raster, perl alarm at 600 s; `ONLY=n`
  runs one; set TMPDIR first).
- No dead controls: `python3 tools/sweep.py` (each control in a context where it acts;
  `--bare` drops the context).
- Render cost: `./build/lptest --bench` (720p, 1080p, one 4K run).
- `LPTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **One full-raster triangle per draw** (`Shaders.h`, five shaders for `glslc`): carve
  (RGBA32F: a_R, a_G, a_B, d), then per group of up to four scales at one factor f (MRT,
  RGBA32F): at f = 1, spread along x through each scale's sampled Gaussian (the table's
  weights read by d) and one pass along y summing the scales; at f > 1, spread along x through
  the quadratic B-spline decimation, the decimation along y, the Gaussian along x, the Gaussian
  along y summing the scales; then composite (the delta column, each group reconstructed by
  the B-spline at its factor by hand, Back Light, the front term, sRGB, the premultiplied Mix).
  Nothing is blended.
- **The intermediates are RGBA32F, not RGBA16F.** Half floats put the blur's measured second
  moment 9% off the multipole's (their rounding of the thick background); floats 1.4e-4.
- **No time and no randomness reach a shader**, and nothing is kept between frames but a plan
  that is a pure function of the controls and the raster (rebuilt when one changes).
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index.
- GLSL 4.10 core. Reserved words (`common partition active … packed sample patch layout flat`,
  `noise1`–`noise4`, `any far near fixed half`) must not be identifiers, and nothing writes
  `M_PI`; `verify.sh` greps for both. Every sampler a shader declares gets a complete texture
  bound, read or not, or Apple's driver logs "unloadable".
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `lithophane_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- `.github/workflows/`: `ci.yml` (macOS arm64 with `lptest --list`, `--offline` and glslc;
  Windows x64 with vcpkg's GLEW and `tools/wincheck.ps1`, which loads the DLL as a host does)
  and `release.yml` (a `v*` tag publishes the universal bundle and the x64 DLL, zips, a dmg
  and an installer). Actions run because the repo is public; a private copy must never
  enable them. `wincheck.ps1`'s `-Params` moves whenever a control or the About block does.
  The pixel checks are still `tools/verify.sh`'s, on a Mac.
- `~/Projects/resolume/lithophane`, public at github.com/stoatworks-labs/lithophane. The spec
  is `~/Projects/resolume/specs/SPEC-lithophane.md`.

## Browser demo
- `demo/` is served at https://lithophane-demo.stoatworks-labs.com/ by the Worker in
  `wrangler.toml` (a route, plus a proxied AAAA `100::` record for `lithophane-demo`: delete the
  record and the page goes dark while deploys stay green); `deploy.yml` redeploys it on every
  push to main, by hand `cf-run npx wrangler deploy`. `demo/shaders.js` is a copy (after a
  shader change: `python3 demo/tools/check_shaders.py --write`); `demo/plate.js` and
  `demo/port.js` are hand ports of Plate.cpp, Controls.cpp and Lithophane.cpp: change them with
  the C++, then `check_shaders.py --hashes` and `demo/tools/crosscheck/crosscheck.py` (Chrome and
  a GPU; figures in AGENTS.md). `verify.sh` fails on drift. `demo/vendor/` is the shared kit:
  never edit it. Keep the photosensitivity note at the top of the page.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume). Each
material's fit logs its worst L1 distance once per process.

    ~/Library/Logs/lithophane/lithophane.YYYY-MM-DD.log
    %LOCALAPPDATA%\lithophane\logs\lithophane.YYYY-MM-DD.log

`LITHOPHANE_LOG_DIR` overrides the folder.
