# lithophane

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume.** What's established is established by an offline harness, `lptest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context, on this Mac's GPU at two rasters and on Apple's software renderer. Read off the
> picture, each channel's transmission falls as **exp(−k d)** with the measured porcelain's k to
> four decimals; the light's spread through the plate matches the **multipole diffusion
> profile** of its own thickness: a one-pixel line's blur radius equals the model's prediction
> to the third decimal (0.517 mm for a 0.6 mm line on a 6 mm plate, 1.516 mm for a 3 mm line),
> and its L1 distance from the true kernel's response is **0.0006–0.0008** of the light against
> tolerances of 0.0136–0.0158 at 1280×720; front-lit, the relief
> shades by Lambert's law to **1e-6**. **16** deliberately wrong models and **54**
> one-character mutants of the shipped code are all caught. White PLA's coefficients, the
> column approximation and the front lamp's default direction are **chosen**, not measured:
> see [Status](#status).

The clip carved into a backlit porcelain plate, for [Resolume](https://resolume.com) Arena and
Avenue: an FFGL effect, **SW Lithophane**.

A lithophane is a plate of unglazed porcelain carved thick where the picture is dark and thin
where it is light. Hold it up to a lamp and the picture appears. Light is absorbed on the way
through, more in the blue than in the red, so a white lamp comes through warm, and redder
where the plate is thicker. And it is scattered, so every point of the lamp side comes out of
the front as a soft spot that grows with the thickness it crossed: dark detail is blurred more
than bright detail, as well as darkened. Turn the lamp off and light it from the front, and the
same plate is a white relief: its carving catches the light, and thin places, which let more of
the dark room through, read a little darker, so it carries a faint negative of the picture.
Nothing is drawn: it all comes out of the thickness.

## Controls

| Group | |
| --- | --- |
| **Plate** | Thinnest (0.5–3 mm) and Thickest (0.5–6 mm), Material (Porcelain, Feldspar Porcelain, Clay Porcelain: three measured Japanese porcelain bodies; White PLA, a printed lithophane), Mapping (Linear Depth: thickness linear in the picture's luma, as 3dp.rocks' generator works; Linear Light: Cura's, transmission linear in the clip's luminance), Relief Side (Facing Viewer or Facing Lamp), Plate Width (20–400 mm across the frame). |
| **Light** | Back Light (the lamp behind, 0–16), Front Light (a lamp in front, 0–2), Light Angle and Light Elevation (where the front lamp is). |
| **Look** | Mix. |

The defaults are a 100 mm plate 0.8 to 3 mm thick (3dp.rocks' defaults; Osser's porcelain
lithophanes are "about ⅛ inch or 3 mm thick"), the typical body of Sugiyama et al.'s three,
lit from behind in a dark room. Back Light's default, **7.2146**, was set on Resolume's own 33
demo clips: there, the brightest 1% of the median clip just reaches full scale in its brightest
channel (red, which porcelain passes most).

**The relief side does nothing under back light.** The model spreads each column's light by
the slab profile of its own thickness, which can't tell which face is carved; Relief Side
changes the front-lit picture only. **The front lamp has no shadows and no gloss**: the
porcelain is unglazed biscuit, and the relief is lit by Lambert's law.

A transparent part of the clip is the plate's thickest porcelain, not a hole: the output is
opaque at Mix 1, and Mix blends premultiplied, so nothing under a transparent pixel ever shows.
Nothing moves with time.

## Status

**v0.1.0, 16 November 2026. Never loaded into Resolume.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). Every picture check runs at **two rasters**, 1280×720 and
320×180, and again on **Apple's software renderer** at 320×180. The numbers below are from
`lptest --all` on the same commit (an arm64 build, the same code); 320×180 in brackets.

| check | result |
| --- | --- |
| `--model` | the four materials equal the spec's table; the plugin's multipole profile against the harness's own direct sum to **5.8e-16** of the peak; its closed-form spread against quadrature to 1.6e-14; the 15-Gaussian fit within its stated bounds for every material (Porcelain **0.00517** of 0.0052); the B-spline decimation's L1 from the true Gaussian **0.001969** at every phase, centroid and variance exact; 300 frame plans, 0 broken rules |
| `--attenuation` | flat cards of eight codes, four materials, both mappings: every fitted slope equals −k to four decimals (Porcelain **0.9000 / 1.0100 / 1.4200**); worst point **0.024** (0.030) of a float tolerance; red > green > blue everywhere |
| `--blur` | one-pixel lines at 0.6, 0.8, 1.125, 1.5 and 3 mm on a 6 mm plate, columns at 8 or 64 phases and rows at 8: blur radius equal to the multipole's prediction to the third decimal in every channel, along x and along y (green **0.517 / 0.638 / 0.810 / 0.985 / 1.516 mm**), growing with thickness; L1 from the true kernel **0.0006–0.0008** of the light (0.0001–0.0019) against 0.0136–0.0158 (0.0170–0.0590); from the plugin's own mixture **0.00057–0.00066** (up to 0.00073) against 0.0023–0.0052 |
| `--front` | equal luma in five colours reflects the same; opposite ramps' ratio **0.678878** against Lambert's 0.678877, along x and along y; facing the lamp, no shading; thin over thick **0.81730 / 0.85300 / 0.91850**, equal to Kubelka–Munk to five decimals |
| `--edges` | a flat card flat to the frame's edges within **4.5e-7** at Plate Width 20, 100 and 400 mm (blur factors to 256); a ramp into each edge shades at the edge pixel as inside, 1.000000 (0.999999–1.000003) |
| `--alpha` | alpha = mix(α, 1, Mix) to **4.5e-8**; white under α = 0 changes **0** values; Mix 0 the clip bit for bit; Mix 0.5 the premultiplied mix to 1.5e-7. On two of Arena's own alpha clips, white painted under every transparent pixel changes **0** output bytes |
| `--resize` | 1280×720 → 320×180: **0** of 230,400 values differ from a fresh instance (there is no state) |
| `--clock` | host clocks from 0, from 4.99e8 ms and jumping back: **0** of 44,236,800 values differ |
| `--glstate` | the host's GL state comes back, **0** things changed |
| `--robust` | a clip with −1, 2, 10³⁰, ±inf and NaN renders exactly as its tamed copy; 21 renders with a control at an end, **0** non-finite; Mix 0 hands the clip through byte for byte |
| `--pipe` | 2.5 frames in, exactly 2 out; a bad cue, a bad option, `abc`, `nan`, `inf`, an empty value and `0.5x` refused (exit 2); an option by name taken; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **16** deliberately wrong models, **16** caught |
| mutation | **54** one-character mutants of the shipped GLSL and C++, **54** caught |
| `tools/sweep.py` | all **11** controls measurably change the picture |
| shaders | all 5 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Lithophane` / `LP01` / `effect` and renders through it |

Render cost, from verify.sh's final run on 16 November 2026 on a shared machine (other builds
running), over the harness's card, GPU time (`GL_TIME_ELAPSED`): at the defaults (six groups
of blur passes) **2.17 ms** a frame at 720p (worst 2.35), **4.13 ms** at 1080p (worst 4.47) and
**10.49 ms** at 4K (one run; worst 10.55). With the most groups (Plate Width 20 mm, Thickest
6 mm, Front Light on: seven groups, factors to 256) **1.99 ms** at 720p (worst 2.70) and
**4.54 ms** at 1080p (worst 4.76). What each pass takes, and the CPU's time a frame, were not
measured. The float intermediates cost 5–15% over half floats (measured while choosing them).
macOS only.

### Not established

- **No host has loaded it.** Not Arena, not Avenue, on any platform. `oxbow` (the fleet's test
  host) reads the bundle and renders through it, which is all.
- **Windows** is built by CI (`ci.yml`), and `tools/wincheck.ps1` loads the DLL as a host
  does and reads its name, id, type and parameters; no host has loaded it there.
- **No real lithophane has been compared with it.** Every check is against the published
  measurements and the diffusion model, not against a photographed plate.
- **Chosen, not measured:** the porcelain's absorption taken from its measured apparent
  coefficient k (σ_tr = k); White PLA's k (a two-point slope of a transmittance that is not
  exponential) and its scattering (an extrapolation past the source's data); one wavelength per
  channel (650, 550, 450 nm; flat 100 nm bands would pass up to 12% more blue through 3 mm); the
  front lamp's default direction (upper left, 45°); Facing Viewer as the default side.
- **The column approximation.** Each column spreads its own light by the slab profile of its
  own thickness; light crossing between columns of different thickness is left out, and with it
  any difference between the relief sides under back light.
- **White PLA is over-blurred**: about a third of its light comes through nearly straight
  (haze 56–64%), which a diffusion model can't carry.
- **The front-lit reflection is not blurred**, and the relief casts no shadows.
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's DXV demo clips decode to; whether Resolume hands an effect premultiplied colour has
  not been checked.
- No user guide, no presets, no OpenFX port and no browser demo.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Lithophane.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's under Effects as **SW Lithophane**.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and again on the software renderer, the alpha check and
Back Light's default on Arena's demo clips (required), the pipe, the negative controls, the
mutants, the sweep for dead controls and the render cost. CI (`ci.yml`) builds macOS and
Windows and runs the checks that need no GL; the pixel checks are this script's. `CLAUDE.md` lists the checks one by one; `./build/lptest --all` runs them all.

Render a frame of your own:

```bash
./build/lptest --out /tmp/lithophane.png --set "Material=Clay Porcelain" --set "Front Light=0.25"
```

(An option takes its name; a slider takes the host's 0..1, so Front Light 0.25 is 0.5 of its
0–2.)

Run a clip through it:

```bash
ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/lptest --pipe --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mov
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
