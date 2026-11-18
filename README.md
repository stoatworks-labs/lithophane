# lithophane

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. In **Resolume Arena 7.29.0 on a Mac**
> it loads with every control as declared and, over a still test card, holds still and changes
> the picture with every control. On **Windows** its release build loads, registers and renders
> in Arena 7.27.1 on software rendering, all 12 valued controls live. Everything else is
> established by an offline harness, `lptest`, that drives
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

> **Photosensitivity note.** Lithophane adds no flashing of its own, but it lifts a clip's
> black to a warm glow, so a dark clip's pulses can come out as large changes of brightness.
> At its defaults, over Resolume's 33 demo clips (8 s each, xenon's rule: more than six
> transitions in any second, over the whole frame or any third of it), NeonRoom2_32 went over
> three flashes a second through it (7 transitions in a third of the frame; 4 on its own), and
> Bass 006 and Beat 002 reached the line (6). At Back Light 16 none passed six. Flashing light
> can trigger seizures in people with photosensitive epilepsy: the
> [user guide](https://stoatworks-labs.com/software/lithophane/guide/#flashing) has the table.

![A warm, honey-coloured picture of a night skyline: a soft bright sun behind rows of
buildings, a glowing white sign in the sky, and windows that come through as soft points of
light](docs/hero.png)

<sub>One frame rendered by `lptest`, not captured from Resolume: the harness's card at the
defaults but for Back Light 4.</sub>

A lithophane is a plate of unglazed porcelain carved thick where the picture is dark and thin
where it is light. Hold it up to a lamp and the picture appears. Light is absorbed on the way
through, more in the blue than in the red, so a white lamp comes through warm, and redder
where the plate is thicker. And it is scattered, so every point of the lamp side comes out of
the front as a soft spot that grows with the thickness it crossed: dark detail is blurred more
than bright detail, as well as darkened. Turn the lamp off and light it from the front, and the
same plate is a white relief: its carving catches the light, and thin places, which let more of
the dark room through, read a little darker, so it carries a faint negative of the picture.
Nothing is drawn: it all comes out of the thickness.

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/lithophane/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`lithophane-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/lithophane/releases/download/v0.1.0/lithophane-0.1.0-macos-universal.dmg) | 252 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`lithophane-macos-universal.zip`](https://github.com/stoatworks-labs/lithophane/releases/latest/download/lithophane-macos-universal.zip) | 211 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`lithophane-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/lithophane/releases/download/v0.1.0/lithophane-0.1.0-windows-x86_64-setup.exe) | 259 KB |
| x64 · .zip archive | [`lithophane-windows-x86_64.zip`](https://github.com/stoatworks-labs/lithophane/releases/latest/download/lithophane-windows-x86_64.zip) | 133 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/lithophane/releases](https://github.com/stoatworks-labs/lithophane/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

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

**v0.1.0, 18 November 2026.** A [user guide](https://stoatworks-labs.com/software/lithophane/guide/)
(also [docs/USER-GUIDE.md](docs/USER-GUIDE.md)) and a [browser demo](#browser-demo); no presets.

### In Resolume

- **Windows, Arena 7.27.1** (win-lab, software rendering: Mesa llvmpipe, no GPU, no sound
  device), the fleet's Arena gate on the DLL `release.yml` built from the registered tree:
  it loads and registers (`SW Lithophane` / `LP01` / effect), all 17 host controls (the 16
  declared plus Arena's Opacity) match their names, order, types, ranges and defaults, it
  renders (89.8 levels from its bare input at the defaults), and Arena's log stays clean: 9
  passed, 0 failed, 0 skipped. All 12 valued controls changed Arena's picture, Opacity
  included; Relief Side, Light Angle and Light Elevation with the lamp in front (Back Light 0,
  Front Light 1), Light Angle at 0° against 180°. Software rendering says nothing about a GPU
  or speed.
- **macOS, Arena 7.29.0** on Apple Silicon (macOS 26), the same run's universal bundle, over
  REST with one layer added on top of an open show and a still of the harness's card on it:
  Arena's log records `registered extension: 'SW Lithophane' uid: LP01 category: 1`; the
  effect has all 16 controls, their defaults and the option names as declared, and the About
  line reads `Lithophane v0.1.0`. From Arena's composition snapshot (mean difference in 8-bit
  levels over the middle three-fifths): the effect at its defaults 138.0 from the bare clip;
  two snapshots 3 s apart, nothing touched, **0.0** (nothing moves); Thinnest 68.8, Thickest
  111.0, Porcelain to White PLA 32.4, Feldspar to Clay Porcelain 139.0, Mapping 5.7, Plate
  Width 26.1, Back Light 93.0, Front Light 29.8; **Relief Side under back light alone 0.0**, as
  the column model says; front-lit, Relief Side 4.2, Light Angle 3.7, Light Elevation 105.1;
  Mix 0 against the bare clip **0.0**, and back at the defaults the first snapshot to 0.0. The
  layer was removed, Arena quit without saving and the show file untouched.

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

- **Not through a show.** Arena has loaded it on both platforms over a still test card (above),
  not with real footage, not on a GPU on Windows, and nothing has been timed inside Resolume.
  Avenue has not been tried.
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
- **Back Light's default puts the median demo clip's brightest 1% at the top code in red**, so
  about half of Resolume's demo clips clip in red at it, and it was set for Porcelain: Feldspar
  Porcelain and White PLA come out nearly white at it (the guide says to turn it down).
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's DXV demo clips decode to; whether Resolume hands an effect premultiplied colour has
  not been checked.
- No presets and no OpenFX port. The browser demo below is a port, not the plugin.

### Open questions, decided for this release but not settled

- **The widest blur scales** cost a whole group of passes (two at f = 8 or 32) for about
  1.25e-4 of the light through 6 mm; dropping them would save the time and change the spec's
  stated tolerance. Kept.
- **Which face is carved.** No porcelain source read says; the printing guides put the relief
  towards the viewer, which is the default. **Relief Side only shows under front light**:
  under back light the column model can't tell the faces apart.
- **White PLA looks over-blurred** (above). A sharp-plus-diffuse PLA would need a source for
  the split against thickness.
- **A gentler Back Light default** (the median clip's p95, or 0.9 of the top) would clip fewer
  clips in red.
- **No blur on the front-lit reflection, and no relief shadows**: at a low Light Elevation real
  carved porcelain would show both.

## Browser demo

[lithophane-demo.stoatworks-labs.com](https://lithophane-demo.stoatworks-labs.com/) runs the
plugin's own GLSL in WebGL2, unedited, fed by a JavaScript port of its CPU half (the materials,
the multipole profile, the 15-Gaussian fit, the kernels, the frame's plan and every
conversion) that a reader and a crosscheck check, not the harness, over the harness's card, a
generated clip or one you pick. The page makes each material's fit itself, in a Worker, as the
plugin does the first time a material is chosen. Run against `lptest --pipe` on the same
frames, 29 of 33 cases came out identical to the bit; in the other four, 6 pixels in all were
one 8-bit level apart (`demo/tools/crosscheck/`; the figures are in AGENTS.md). It opens on the
plugin's defaults, with a photosensitivity note at the top. It is a demo, not the plugin, and
the page lists what it leaves out.

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
