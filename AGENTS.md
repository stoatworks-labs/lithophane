# AGENTS.md: Lithophane

Read this before changing the fit (`Plate.cpp`: `fit`, `kHold`, `kImagePairs`), the frame plan
(`PlanFor`: the factor rule, the delta column, the groups), the decimation and reconstruction
(`Shaders.cpp`: spread, pass, `level()`), the intermediates' format (`MultiTarget.h`) or Back
Light's default. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-lithophane.md`, written before any code and kept in step with
this repo (no As-built section: where the build departed from the first draft, the spec was
changed, and the changes are listed below).

## The idea

**A plate of porcelain is thick where the picture is dark and thin where it is light; a lamp
behind it shines through.** Each column passes exp(−k d) of each channel (red further than
blue: Sugiyama's measured porcelain bodies), and scatters what it passes into a soft spot whose
size is the multipole slab profile of its own thickness, so dark detail is blurred more than
bright detail as well as darkened. Lit from the front, the carved face shows its relief by
Lambert's law, times the plate's Kubelka–Munk reflectance, which is lower where the plate is
thin: a faint negative. Nothing is drawn. Neighbours not duplicated: `repousse` (a reflected
metal relief: its normal convention is matched, nothing else) and `contour` (luma contours).

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Lithophane`, `LP01` (checked free against every `"LP01"` in
  `~/Projects/*/*/source` and `~/dev/*/source`), an effect, `com.stoatworks.ffgl.lithophane`,
  harness `lptest`. One class, `LithophanePlugin`; host indices are the ParamIds, the About block
  last.
- **One wavelength per channel**, R 650, G 550, B 450 nm: the three at which Sugiyama shows
  ln(t) straight against thickness. The cost against 100 nm flat bands was measured from the
  digitised Fig. 5 (3 mm: +12.1 / +5.8 / −0.3% B/G/R) and is left: a flat band is itself a
  stand-in for a camera.
- **k drives the transmission; K and S only the reflectance and the scattering length.**
  Sugiyama's measured apparent absorption coefficient is the one number with a stated error
  (5%); their K–M coefficients disagree with it by their own ±20%. σ_a is **chosen** so that the
  multipole's thick-slab decay σ_tr equals k; σ′_s = 4S/3 (Pham et al.); F_dr = 0.5 for
  porcelain (Hogg's r₂, as Sugiyama assumes), Donner & Jensen's fit at n = 1.4 for PLA.
- **White PLA's k** is Wang & Zhou's two-point slope 0.8 → 2.4 mm (their transmittance is not
  exponential), and its μs′ = 3.75 mm⁻¹ is Ragunathan et al.'s mixing fit extrapolated past
  their data. Both **chosen**, and the diffusion model over-blurs it (haze 56–64%).
- **The column (entry-point) approximation**: each column keeps exp(−k d) of its light and
  spreads it by the slab profile of its own thickness. So the relief side does not change the
  back-lit picture. Stated in the spec, not resolved.
- **15 fixed Gaussians**, σ_j = 0.0625 · √2^j mm, fitted by NNLS to the 2πr-weighted radial
  profile on a table of 111 rows (0.5 … 6 mm, 0.05 mm), weights interpolated linearly between
  rows. **Two constraint rows hold the total and the variance**, weight `kHold = 100`
  (**chosen**: 0.01 already holds the variance to 1e-3). 30 image pairs each side.
- **The multi-resolution plan**: a scale under 0.25 px is the pixel itself (the delta column);
  f = 1 under 4 px; otherwise the largest power of two with σ_px / f ≥ 2. Quadratic B-spline
  decimation and reconstruction, low-res variance σ²/f² − ½ (the B-spline's own f²/4 per
  stage, exact at every phase). A scale weighing under 1e-5 everywhere in the frame's rows is
  dropped (`kActiveWeight`, **chosen**). At most four scales a group (MRT), at most eight groups.
- **Every sampled kernel's radius is fixed first** (⌈4σ⌉ of the target), then σ is bisected
  so the truncated kernel has exactly the target variance.
- **RGBA32F intermediates** (below, under traps).
- **The plate continues past the frame's edge** at the edge's thickness (clamp to edge).
- **Front light**: a directional lamp, Lambert off the carved face with repousse's normal
  (central difference in mm, one-sided at the frame's edge, n = normalize(−∇d, 1)), times
  Sugiyama's K–M reflectance over a dark room with r₁ = 0.06 and r₂ = 0.5. No specular (the
  porcelain is unglazed biscuit), no shadows, the reflection not blurred. Relief Side defaults
  to Facing Viewer (the printing guides; no porcelain source says). Light Angle 135° and
  Elevation 45° are **chosen**.
- **Back Light's default from the demo clips**: the median over 33 clips of the p99 brightest
  output channel is 1 at the default plate. 7.2146; range 0–16.
- **Output alpha mix(α, 1, Mix)**, the premultiplied mix as convergence, Mix 0 the clip
  untouched (NaN and all). The thickness comes from the premultiplied clip, so a transparent
  pixel is the thickest porcelain.
- **Nothing moves and nothing is kept**: no clock is read; the only thing kept between frames is
  the plan, a pure function of the controls and the raster.
- **A wild clip is tamed** before anything multiplies: NaN to 0, then clamped to 0..1.

## Changes from the spec's first draft

Each was made in the spec as well; the spec's status line says so.

- The numbers carried to the digits measured: per-axis spread 0.658 / 1.008 / 1.565 mm through
  0.8 / 1.5 / 3 mm, R/B 0.695 / 0.594; σ_a 0.0535, σ′_s 6.307, z_b 0.3145; the fit's bounds
  0.0052 / 0.0011 / 0.0014 / 0.030; the B-spline's 0.001969.
- **The fit's variance constraint** (without it the fit missed the variance by up to 4.75%,
  the blur radius by 2.4%), and 30 image pairs (10 lost 1.4% of PLA's variance).
- **The kernel's radius rule** (a radius following σ made the variance jump where ⌈4σ⌉ steps;
  a target in a jump was missed by 1e-4).
- **RGBA32F, not RGBA16F**, in the chain.
- **Back Light**: the rule reads the brightest channel, not luminance, and the range is 0–16,
  not 0–8 (below).
- **The harness's cards**: `--attenuation` on flat cards, not patches (the multipole's tail
  still carries about 3e-3 of the light 8 mm out, above the rounding tolerance, so a patch's
  centre is never clear of its neighbours); `--blur` measures a row along y as well as a column
  along x, adds a line at 1.125 mm (midway between table rows) and compares with the plugin's
  own mixture as well as the multipole; `--front` (v), the ramps along y; `--edges`, a ramp into
  each edge; `--robust`, Mix 0 checked as the clip byte for byte.

## The traps actually hit

- **Half floats hid a 9% error inside a tolerance that allowed it.** With RGBA16F
  intermediates the blur line's second moment came out 21.7 against 23.8 px² at 1280 × 720,
  and the M2 check passed: its rigorous bound (the encode and the float sums, pixel by pixel,
  weighted by distance squared) was 1090 px². The rounding of the thick background (2⁻¹¹
  relative) was the cause. RGBA32F brought it to 1.4e-4 relative, for 5–15% more GPU time. The
  check now prints the relative error beside the tolerance, so a loose bound can't hide a
  large error again.
- **Back Light's first rule could not be met.** "Median p99 luminance = 1" assumed luminance
  reaches the top first; red does (the plate passes red most), so at the rule's own value
  (8.8171) the median clip's p99 luminance was 0.9527. Reaching 1 would need blue at the top
  too, which burns the picture. Now it reads the brightest channel.
- **`--robust` failed on Mix 0 by design**: Mix 0 hands the clip through, NaN and all. The
  finiteness sweep leaves that render out and checks it is the clip, byte for byte.
- **The submodule's recorded commit was 5b6e078**, not the pinned b1afaf9 the working tree had
  checked out. The working tree's b1afaf9 was staged; verify.sh checks the pin.
- **Checks measured along x only.** The blur's passes along y and the front normal's y were
  unmeasured until a row and (v) were added; the one-sided difference at the frame's edge was
  unmeasured until `--edges` got its ramp.
- **Six mutants survived the first full run** (48 of 54). The multipole comparison's tolerance
  must carry the fit's allowance (0.0052 of the light), and that hid four GPU faults: the
  table's rows interpolated backwards, the reconstruction's texel centres, the scales 2^0.6
  apart and the low-res variance's ½. The fix was to compare the same excess with **the
  plugin's own mixture** (its table's weights through the harness's own kernels). There the
  tolerance is the B-spline's bound, and the measured error is 0.0006 of the light at 1280 ×
  720. A line midway between table rows (1.125 mm) was added for the interpolation. The dipole
  mutant moved to `--model`, which compares the profile to 1e-12.
- **A group mutant that no plan reaches.** `if( LevelCount > 3 )` → `> 4` drops the fourth
  group only when it is the last, and no check's plan has exactly four groups (5 at 320 × 180,
  6–7 at 1280 × 720; printed in `--blur`'s lines). The last group in both is scales 13–14
  (5.7 and 8 mm). Their largest weight in the rows the checks read is 1.25e-4 (scale 14, green,
  6 mm), below every check's resolution, so "the last group dropped" (`> 4` → `> 5` at
  320 × 180, `> 6` → `> 7` at 1280 × 720) survives as well. Measured; the table now has "the
  fourth group subtracted", which is caught. See the open questions.
- **A degenerate window passes.** At 160 × 90 `--blur`'s 80 px margin along x leaves no
  column window. Every line along x then "passes" with L1 0 against a tolerance of 1.05 of the
  light, and only the radius-grows check fails. The checks are built for 320 × 180 and up.
- **The default `$TMPDIR` once, unexported.** A heredoc in a script hangs without a writable
  TMPDIR on this Mac; it has to be exported, not set on one line.
- **Apple's driver logs "texture unloadable"** (and samples a zero texture) for a declared
  sampler with no complete texture bound, read or not; seen here while the passes were
  being built. Every sampler now gets one.

## Verified, and assumed

Verified, by `OXBOW=... tools/verify.sh` on 16 November 2026 (numbers in README's Status):
every check at 1280 × 720 and 320 × 180 on this Mac's GPU and again on Apple's software
renderer at 320 × 180; the alpha decision and Back Light's default on Arena's clips; the pipe;
the negative controls and mutants; the sweep; the bundle universal, signed ad hoc, read and
rendered by oxbow.

Every claim, its number at 1280 × 720 (320 × 180) and its tolerance's source:

- **The materials, the transport and the profile** (`--model`, no GL): the four materials'
  coefficients equal the spec's table (0 difference; 1e-12); μ_a, z_b, ℓ, α′ against the
  harness's own formulas to 6.8e-15 (1e-12); the plugin's profile against the harness's direct
  sum to 5.8e-16 of the peak (1e-12); 30 image pairs against 80, 7.6e-17 of the peak (1e-6,
  three orders under the fit); the closed-form M0 and M2 against a 20 000-step trapezoid in
  ln r, 9.5e-10 and 1.6e-14 (1e-5, the quadrature's own error under 1e-6). Per-axis spread
  0.6579 / 1.0083 / 1.5648 mm.
- **The fit** (`--model`): 2-D L1 between the mixture and the multipole, every row and channel:
  Porcelain 0.00517, Feldspar 0.00105, Clay 0.02945, PLA 0.00026, against the spec's measured
  bounds 0.0052 / 0.0011 / 0.030 / 0.0014; midway between rows within +0.005 of them (worst
  0.00517 / 0.00457 / 0.02944 / 0.00458). The variance held under 1e-3 relative (worst 5.8e-4).
- **The kernels and the B-spline** (`--model`): every sampled kernel's variance off by 2.6e-15
  relative, sum 4.4e-16 (1e-12, double); B-spline decimation by 2 … 32 at every phase: L1 from
  the sampled Gaussian 0.001969 (the spec's 0.0020, its measured bound), centroid 4.3e-14 px,
  variance 3.3e-15 relative (1e-9). 300 plans, 0 broken rules; every row's weights sum to 1
  within 1.2e-7 (16 float roundings).
- **Transmission** (`--attenuation`, flat cards of eight codes, four materials, both mappings):
  every fitted slope equals −k to the four decimals printed (Porcelain 0.9000 / 1.0100 / 1.4200);
  worst point 0.024 (0.030) and worst slope 0.009 (0.010) of the tolerance; R > G > B
  everywhere; Linear Light's green linear in Cura's luminance within 0.024 (0.030). Tolerance per
  point: four float roundings of the stored intermediates (2⁻²⁴ each), the encode (2⁻¹⁷), the
  float sums, the recode's thickness, the flush to zero.
- **The blur against the true kernel** (`--blur`, lines at 0.6 / 0.8 / 1.125 / 1.5 / 3 mm on a
  6 mm background, a column at every phase 0 … 63 at 0.8 mm and eight phases otherwise, a row
  at eight phases): green L1 of the excess against the multipole's line spread 0.0006 to
  0.0008 of the line's light (0.0001 to 0.0019), against 0.0136 to 0.0158 (0.0170 to 0.0590)
  (the fit's bound + the table's midway allowance + the B-spline bound + rounding + the
  pixel-sampled against pixel-averaged discretisation, computed per line). The blur radius of
  the excess equals the prediction to the third decimal in every channel along x and along y
  (green 0.517 / 0.638 / 0.810 / 0.985 / 1.516 mm), at both rasters, and grows with thickness.
  The second moment is off by at most 1.8e-4 (7.3e-4) relative. Its rigorous tolerance is loose,
  up to 1.06 relative, and is printed beside it. Against the plugin's own mixture: green L1
  0.00057 to 0.00066 of the light (0.000004 to 0.00073), against 0.0026 to 0.0052 (0.0023 to
  0.0027) (the B-spline bound on all the weight, the dropped scales, rounding, the window's
  outside); worst ratio 0.299 (0.298).
- **Front light** (`--front`, Back Light 0): five colours of luma 128 reflect the same (0.000 of
  the tolerance); opposite ramps' ratio 0.678878 against Lambert's 0.678877 along x and along y
  (0.006 of the tolerance: the recode through dR/dd, the encode); facing the lamp, 1.000000;
  thin over thick 0.81730 / 0.85300 / 0.91850, equal to Kubelka–Munk to five decimals (0.026 of
  the tolerance).
- **Edges** (`--edges`): a flat card flat to the edges, worst 4.5e-7 relative against 1.7e-5 …
  1.1e-4 (stored roundings, the encode, the float sums, scaled by the plan's widest factor),
  Plate Width 20 / 100 / 400 mm (factors to 256); a ramp into each edge shades at the edge pixel
  as inside: 1.000000 (0.999999 … 1.000003), 0.001 (0.007) of the tolerance (d's float error
  and the recode through the slope and R(d), the encode).
- **Alpha** (`--alpha`): alpha off mix(α, 1, Mix) by 0 at Mix 1 and 4.5e-8 at Mix 0.5 (1e-6, a
  few float roundings); white under α = 0 changes 0 values at Mix 1 and 0.5; Mix 0 the clip, 0
  values differ; Mix 0.5's colour off the premultiplied mix by 1.5e-7 (1.4e-7) (1e-6). On
  Arena's clips (`tools/clips.py alpha`): BattleWeapon_Tank_09 (60% fully transparent) and Ethnik2 (50%), 20 frames at 320 × 180: alpha 255 at Mix 1, within 0.50 codes of a/2 + 127.5 at Mix 0.5 (0.5, the byte's rounding), 0 output bytes changed by white under every transparent pixel at Mix 1 and 0.5; the negative control (white on about 200 opaque pixels) shows on both clips.
- **No state** (`--resize`, 1280 × 720 → 320 × 180, Front Light on): 0 of 230 400 values differ
  from a fresh instance. Exact.
- **No time** (`--clock`, six frames from 0, from 4.99e8 ms and jumping back): 0 of 44 236 800
  (2 764 800) values differ. Exact.
- **The host's GL state** (`--glstate`): 0 things not put back, GL errors included. Exact.
- **Robustness** (`--robust`): the wild clip renders as its tamed copy, 0 values differ; 21
  renders with one control at an end, 0 with a non-finite value; Mix 0 hands the wild clip
  through byte for byte. Exact.
- **Back Light's default** (`tools/clips.py defaults`): 33 clips at the default plate and Back Light 1, 640 × 360, 30 frames from 1 s: p99 brightest channel 0.0673 … 0.4787, median 0.138607, so 1 / median = 7.2146; the default as read back from the binary (four decimals of the host's 0..1) is 7.2144, off by 0.00024 against 0.00090 (--list's four decimals times 16, the source's four decimals, the p99 histogram's bin); there the median clip's p99 brightest channel is 0.999996 (4.0e-6 off 1 against 2.3e-5: the same, the encode and decode); every clip under 1 at Back Light 1, so the percentile scales exactly; the median clip (Trinity_09) at 0.999 of the default is under 1 (0.998968). Its p99 luminance there is 0.8177.

Assumed or chosen, not measured: σ_a from k (σ_tr = k); White PLA's k and μs′; one
wavelength per channel; the column approximation (the relief side has no effect under back
light); the weight 100 on the fit's constraint rows; kActiveWeight 1e-5; Light Angle 135° and
Elevation 45°; Facing Viewer as the default; the lamp white and uniform; a dark room behind a
front-lit plate.

Not done: loading it into Resolume on any platform; a user guide, presets, an OpenFX port (the
browser demo is a port, not the plugin: see Browser demo); looking at the demo clips through it
as pictures rather than numbers; a lithophane photographed against the render. Windows is built by CI, and `tools/wincheck.ps1`
loads the DLL as a host does.

## Would this hold on another rasteriser, at another raster?

One line per check. Apple's software renderer at 320 × 180 passes every one of them.

- `--model`: no GL; double.
- `--attenuation`: texelFetch only; the tolerance is float roundings and the encode, which hold
  on any IEEE float pipeline; the flush-to-zero term covers a driver that flushes denormals.
- `--blur`: the kernels and weights are double on the CPU, uploaded as float; the passes are
  texelFetch and float multiply-adds, so the tolerance (rounding per tap, accumulated over the
  plan's widest kernel) holds wherever float arithmetic does. The window needs 80 px of frame
  either side along x: rasters under 320 wide are not supported (above).
- `--front`, `--edges`: per-pixel float arithmetic; the tolerances are in the slope and R(d)'s
  derivatives, not in ulps of the raster.
- `--alpha`: a few float roundings; Mix 0 is a texelFetch and a copy.
- `--clock`, `--resize`: compare the plugin with itself, so they hold wherever one driver is
  deterministic run to run.
- `--glstate`: whatever the driver reports.
- `--robust`: relies on the tame coming before anything multiplies. NaN is found by its bits
  (`floatBitsToUint`), not by `isnan`, so a compiler that assumes no NaN can't drop the test,
  and clamp takes ±inf to 0 and 1. Both renderers here pass it bit for bit.

## The negative controls and mutants

`lptest --negative`, each wrong model required to fail (checks failed at both rasters), all 16
caught:

| check | wrong model | failed |
| --- | --- | --- |
| model | every thickness given the 1.5 mm row's kernel | 2 |
| attenuation | red and blue coefficients swapped | 4 |
| attenuation | a linear 1 − k d law | 4 |
| blur | every thickness given the 1.5 mm row's kernel | 12 |
| blur | a variance-matched single Gaussian per thickness | 10 |
| blur | a box decimation (nearest reconstruction) | 9 |
| front | the clip's colour let into the front term | 4 |
| front | the normal's sign flipped | 4 |
| front | the relief side ignored | 2 |
| edges | black past the frame's edge | 6 |
| alpha | the clip's alpha passed through | 2 |
| alpha | the thickness from the clip's straight colour | 2 |
| clock | the light's angle turned by the host's clock | 2 |
| resize | the first raster's pitch kept across a resize | 1 |
| glstate | the host's GL state not put back | 2 |
| robust | the clip's NaN and range not tamed | 2 |

`tools/mutate.sh`: 54 one-character mutants of the shipped GLSL (34) and C++ (20), arm64,
320 × 180 unless the mutant names its raster, a perl alarm at 600 s (a timeout counts as
caught). All 54 caught. GLSL: the sRGB decode's exponent and the encode's offset, the clip's
light divided by α, a NaN read as 1, Linear Depth's luma weight, Linear Light's thickness the
wrong way and Cura's gamma, the column brightening with thickness, red and blue swapped, the
thickest clamp, the table read a millimetre off and its rows interpolated backwards, the
B-spline's centre and side lobe, the decimation a texel off along x and along y, the first
scale given the second's weight, a group's fourth scale subtracted, the reconstruction's texel
centres, its low-res texel and its left weight, the passes along the wrong axis, Back Light
divided by the transmission, the fourth group subtracted (`--blur`); the slope in mm times
pixels, the normal's z doubled, Lambert's floor, r₁ 0.07, K–M's κ with 3S and coth as tanh
(`--front`); a central difference's span at the edge (`--edges`); the plate transparent, the
clip's share at Mix m 2 − m, the clip handed through up to Mix 0.5 (`--alpha`). C++: σ_a with
4k²/2, the closed-form M2 times σ_tr, the variance not held, the kernels cut at one σ, the
factor rule σ/f ≥ 1, the delta column under ¾ px, PLA's S, the K–M r₂ 0.6, the dipole's near
term doubled, Front Light's and Thickest's tops (`--model`); the boundary's A, the scales 2^0.6
apart, the low-res variance's ½ as 0.6, the box decimation (`--blur`); Porcelain's green k,
Linear Light's MinT from blue (`--attenuation`); the elevation in the wrong unit (`--front`);
the plan kept while its inputs change (`--resize`); the host's viewport put back square
(`--glstate`).

Left out, measured as below every check's resolution (not in the table, which must all be
caught): `if( LevelCount > 3 )` → `> 4` (no check's plan has exactly four groups), and the last
group dropped at either raster (its largest weight is 1.25e-4). Not tried: the spread's and
pass's edge-model lines (their GLSL has `||`, which the table's field separator can't carry).

## The sweep's honest list

`python3 tools/sweep.py`: all 11 controls change the picture, each in a context where it acts
(mean 8-bit difference per channel between the control's ends, one frame of the card at
320 × 180; barely alive under 0.05): Thinnest 51.5, Thickest 105.5, Material 36.1 (Porcelain
against White PLA), Mapping 5.1, Relief Side 1.3 (Front Light 0.5), Plate Width 16.1, Back
Light 180.7, Front Light 34.9, Light Angle 1.5 (0° against 180°, Front Light 0.5), Light
Elevation 28.6 (Front Light 0.5), Mix 93.3.

`--bare` (the defaults, no context): **dead** — Relief Side, Light Angle and Light Elevation,
all three because the default Front Light is 0 and they act on the front light only (Relief
Side has no effect under back light in this model, by the column approximation). Nothing is
barely alive. The smallest live ones are the front light's own: Relief Side 1.3 and Light Angle
1.5, with Front Light at 0.5.

## Browser demo

https://lithophane-demo.stoatworks-labs.com/ (`demo/`, the Worker in `wrangler.toml`: a route,
plus a proxied AAAA `100::` record for `lithophane-demo` made through the API on 2026-11-18;
delete the record and the page goes dark while deploys stay green). `deploy.yml` redeploys it
on every push to main.

- **What is the plugin's and what is not.** `demo/shaders.js` is spliced from `Shaders.cpp`
  (kVersion, kQuadVertex, kCommon and the four fragments) by
  `demo/tools/check_shaders.py --write`; `tools/verify.sh` runs the check, which fails on a
  changed character in a piece, on a change to how the pieces are joined (Shaders.cpp's
  Assemble, Shaders.h's Pass, kKernelCentre, kKernelTaps and kMaxLevels, Lithophane.cpp's stage
  list), on a uniform `port.js` never names or sets as a float when it is an integer, and on
  any change to the C++ the port was translated from (SHA-256 of Plate.{h,cpp},
  Controls.{h,cpp}, MultiTarget::Ensure, Lithophane.h's PluginHooks and seven Lithophane.cpp
  functions; `--hashes` prints new ones once a reader has carried a change across).
  `demo/plate.js` is Plate.cpp and Controls.cpp by hand; `demo/port.js` is Lithophane.cpp's
  frame (InitGL, ensureBuffers, setKernels, render, ProcessOpenGL, SetFloatParameter, with the
  test hooks fixed at their defaults) and MultiTarget; `demo/fit-worker.js` runs plate.js's
  TableFor in a module Worker. `demo/plugin.js` is the page: the panel, the harness's card in
  GLSL (the default clip), the fit queue, the line under the picture, 13 presets of its own and
  `window.lithophaneDemo` for the crosscheck.
- **The fit is made in the page**, in the Worker: all four materials at load, the one on screen
  first, about 120 ms each on this Mac. Nothing is drawn for a material until its table is
  back, and the line under the picture says "Fitting" meanwhile. There is no precomputed table.
  If the Worker cannot start, the fit runs on the page's own thread.
- **Numbers** (`demo/tools/crosscheck/port_numbers.py`: compiles the plugin's Plate.cpp and
  Controls.cpp into `plate_print.cpp`, runs plate.js through `plate_print.mjs` in node, and
  compares): 3,376,493 numbers over 396 plans (96 fixed, 300 random, seed 20261118). Every
  integer, string and float the GPU gets is equal: the plans, groups, scales and kernel radii,
  the 475,132 kernel floats and the uniforms uPitch, uSpan and uThin to the bit; the
  conversions within 1e-12. The fit's weights are within 7.4e-8 absolute of the C++'s, the
  uploaded weight table within 2.4e-7 and the worst-L1 figures within 2.7e-7 relative: the
  fit's normal equations carry the last bits of JavaScript's exp and log, which are not
  Apple's, into the small weights. So the tool judges the fit with an absolute tolerance
  (1e-6), not by its bits.
- **Crosscheck** (`demo/tools/crosscheck/crosscheck.py`, by hand: needs Chrome and a GPU), M4
  Max, macOS 26.4.1, Chrome 157 (ANGLE on Metal) against `lptest --pipe` (GL 4.1), 18 November
  2026, 320×180 unless named, every frame compared. 33 cases: the card at the defaults; each
  material; Linear Light; a flat 0.5 mm plate; a 3–6 mm plate; inverted (Thinnest above
  Thickest); Plate Width 20 and 400 mm; Back Light 0 and 16; front-lit four ways; Mix 0.5;
  cues (Material at frame 1, Mapping at frame 2, Plate Width ramping over frames 0–3); the
  defaults at 640×360, 960×540, 1280×720, 1920×1080 and 333×187; the most groups (eight) at
  1280×720; and each of the kit's clips (scene, spot, ramp, bars, grid, detail, alpha, alpha at
  Mix 0.5, scene front-lit), handed to lptest as the page drew them.

  | case | identical | max |
  | --- | --- | --- |
  | the other 29 | **100%** | **0** |
  | cues | 99.9996% (one pixel, frame 1) | 1 |
  | defaults at 1280×720 | 99.9999% (one pixel) | 1 |
  | defaults at 1920×1080 | 99.9999% (three pixels) | 1 |
  | scene, front-lit | 99.9983% (one pixel) | 1 |

  Six pixels in about 6.7 million were one 8-bit level apart, none more. With the C++'s
  weights handed to the page (`--cpp-weights`), the 1280×720 and 1920×1080 pixels go, so they
  are the fit's last bits; the other two stay, so they are GPU arithmetic. Handing the page the
  clip as lptest holds it (`--input float`, RGBA32F codes / 255) changes no figure. The table of
  controls matches `lptest --list` (11 controls: names, kinds, defaults, order) and lptest
  accepts all 8 option elements by name; every uniform is set (carve declares kCommon's Table,
  RowOrigin, RowStep and RowCount and never reads them, so the compiler drops them, as in the
  plugin), no GL error, no frame refused, no plan truncated. The page's card is within one
  level of the harness's at every page size (93.6–95.3% identical: a float port of a picture
  drawn in double); at 333×187, not a page size, six pixels on a building's edge fall the other
  way (128 levels), reported and not failed. Negative controls, the page one slider step off,
  all caught: Thickest 46.96% identical, Thinnest 82.37%, Back Light 62.65%, Plate Width
  90.13%, Light Angle front-lit 93.84% (max 9). Not covered: a phone GPU.
- **Flashes on the page:** its default view (the harness's card at the defaults, 960×540,
  rendered through `window.lithophaneDemo` into an RGBA8 target at 60 fps for 20 s, written
  lossless and run through xenon's `flashcheck.py`): 0 transitions, whole frame and every
  window. The kit's seven other clips at the defaults, 20 s each: 0 over the whole frame; the
  worst window 2 transitions in 20 s (ramp), never more than 1 in a second. No Start gate. The
  note at the top of the page quotes the release survey's NeonRoom2_32 figures (4 transitions
  bare and 7 through the plugin in its worst second, over a third of the frame).
- **What the page says it leaves out** (its differences list): the CPU half is a port, checked
  by a reader and this crosscheck, not by the harness; the fit is made in the page; alpha (the
  kit's clips are premultiplied, the plugin assumes straight alpha); the default card is a
  float port of lptest's double one; scale follows Plate Width in millimetres, so a bigger
  raster blurs over more pixels, as in the plugin; at most eight groups (the line under the
  picture says when the widest are dropped; no page size reaches eight); highp everywhere,
  checked on a desktop GPU only; the line under the picture is the page's, not the plugin's;
  the presets are the page's; the About block, the log file and the harness's negative
  controls are absent; and what the harness measured that the page did not.

## Open questions

- **The widest scales cost a group for 1.25e-4 of the light.** Scale 14 (8 mm) carries 1.25e-4
  of the green at 6 mm, which is over the 1e-5 activity threshold, so it costs a whole group
  (two passes at f = 8 or 32). No check can see it go, so its effect is under 1e-4 of the
  picture. Raising `kActiveWeight` to 1e-3 would drop it and save the passes; it would also
  change the spec's stated tolerance. Not done.
- **Which face is carved.** No porcelain source read says which face of a lithophane carries
  the relief; the printing guides put it toward the viewer, and that is the default. Both are
  offered. Under back light the model can't tell them apart (the column approximation).
- **The column approximation under back light.** The truth for a relief facing the viewer lies
  between the entry-point form (used) and the exit-point form (no blur at all under a uniform
  lamp). A real plate photographed against the render would say how far.
- **White PLA is over-blurred.** Its haze is 56–64%: a third of the light comes through nearly
  straight, which the diffusion model blurs away. A two-component PLA (sharp + diffuse) would
  need a source for the split against thickness.
- **Back Light's default puts the median clip's brightest 1% at the top code in red.** Brighter
  clips clip in red at the default (about half the demo clips, by construction of a median).
  A gentler rule (p95, or 0.9) would clip fewer.
- **Straight or premultiplied alpha in Resolume's pipeline?** As convergence: the plugin weights
  the clip's light by α, right for straight alpha, which Arena's DXV clips decode to through
  ffmpeg. Needs a look in Arena.
- **Front Light's reflection is not blurred**, and the relief casts no shadows. At a low Light
  Elevation real carved porcelain would show both.
