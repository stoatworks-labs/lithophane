# Lithophane user guide

> **Photosensitivity note.** Lithophane adds no flashing of its own, but it lifts a clip's
> black to a warm glow, so the pulses of a dark clip can come out as large changes of
> brightness. Flashing light can trigger seizures in people with photosensitive epilepsy, and
> the guidance for broadcast and the web (Ofcom's, and WCAG's) draws the line at **three
> flashes a second** over a large part of the picture. At the defaults, over Resolume's 33
> demo clips, one dark clip with small flickering lights went **over** that line through
> Lithophane, though it stayed under it on its own. See [Flashing](#flashing) before you put it
> in front of an audience.

Lithophane is **your clip carved into a backlit porcelain plate, for
[Resolume](https://resolume.com) Arena and Avenue**: one FFGL effect, **SW Lithophane**.

A lithophane is a plate of unglazed porcelain carved thick where the picture is dark and thin
where it is light. Hold it up to a lamp and the picture appears. The plugin makes that plate
out of your clip and shines a lamp through it. Light is absorbed on the way through, more in
the blue than in the red, so a white lamp comes through warm, and warmer where the plate is
thicker. And it is scattered, so every point of the lamp side comes out of the front as a soft
spot that grows with the thickness it crossed: dark detail is blurred more than bright detail,
as well as darkened. Turn the lamp behind off and light the plate from the front, and the same
plate is a white relief whose carving catches the light. Nothing is drawn on top: it all comes
out of the thickness.

![A warm, honey-coloured picture of a night skyline: a soft bright sun behind rows of
buildings, a glowing white sign in the sky, and windows that come through as soft points of
light. The black buildings read as a darker gold, and the colour bars along the bottom are
shades of cream to tan](hero.png)

*SW Lithophane over the offline harness's test card, rendered by the harness rather than
captured from Resolume, at the defaults but for Back Light 4 (the default is 7.2, set on
Resolume's demo clips, which are darker than this card). Black is the thickest porcelain,
3 mm, and still lets a warm glow through.*

> **Before you rely on this:** this is **v0.1.0**, the first release, and early days. The
> plate is measured, not just asserted. An offline harness drives the plugin's own code on an
> Apple Silicon Mac's GPU, at two sizes, and again on Apple's software renderer. Read off the
> picture, each channel's light falls as **exp(−k d)** with the measured porcelain's
> coefficient to four decimals; the blur's radius equals the diffusion model's prediction to
> the third decimal, growing from **0.52 mm** through 0.6 mm of porcelain to **1.52 mm**
> through 3 mm; lit from the front, the relief shades by Lambert's law to a millionth.
> Sixteen deliberately wrong versions of the plugin and fifty-four one-character sabotages of
> its code are all caught, and every control changes the picture. In Resolume Arena it has
> loaded on a Mac (7.29.0) and on Windows (7.27.1, software rendering), with every control as
> declared, and every control changed Arena's picture. But that was over a still test card:
> it hasn't been through a show, and **it has never been compared with a real lithophane.**
> White PLA's numbers, the way light crosses between thick and thin columns, and which face
> is carved are **chosen**, not measured. **Try it on a spare layer first**, and shout if
> anything misbehaves.
>
> This codebase was created with AI assistance, directed and reviewed by a human author.

---

## Installing

Download the build for your platform. For macOS there's a universal `.dmg` or `.zip` (Apple
silicon and Intel), **Developer ID-signed and notarised** so the bundle just loads (the
signing happens on my Mac shortly after each release is published, so a download made in the
first few minutes may need **Open** from the context menu once), and for Windows an x64
installer or `.zip`. Put `Lithophane.bundle` (macOS) or `Lithophane.dll` (Windows) in
Resolume's effects folder, then restart Resolume:

```
macOS    ~/Documents/Resolume Arena/Extra Effects/
Windows  %USERPROFILE%\Documents\Resolume Arena\Extra Effects\
```

Avenue uses the same layout in its own folder. **SW Lithophane** appears in the effects
browser.

The Windows installer puts the DLL there for you, per user, with no admin rights needed. It
defaults to Arena's folder (Avenue's if only Avenue is installed) and lets you change it. The
Windows builds aren't code-signed. Plugin files aren't gated the way `.exe` files are, so
Resolume loads them as normal; only the installer trips SmartScreen, once: **More info** →
**Run anyway**. Or skip the installer and copy `Lithophane.dll` out of the `.zip` by hand.

---

## Start here

1. Put **SW Lithophane** on a clip with something bright on a dark background: a figure, a
   shape, light through smoke. The plate is thick where the clip is dark, so the background
   becomes thick porcelain glowing a warm gold, and the bright parts shine through thin
   porcelain almost white.
2. Look at the edges of bright things: they are soft, and dark detail is softer than bright
   detail. That's the light scattering inside the plate, more through thick porcelain.
3. Turn **Thickest** up: the dark parts go darker, redder and softer.
4. Try **Material**. Porcelain is the default; Feldspar Porcelain and White PLA let much more
   light through, so turn **Back Light** down to about 1 for them.
5. Turn **Back Light** to 0 and **Front Light** up: now the plate is lit from the room, a
   white relief of your clip. Move **Light Angle** and **Light Elevation** to place the lamp.

The defaults: a 100 mm plate, 0.8 mm at its thinnest and 3 mm at its thickest, of Porcelain,
carved by Linear Depth with the relief facing the viewer, Back Light 7.2, Front Light 0, the
front lamp from the upper left (135°) at 45°, Mix 1.

![A test card in Resolume Arena through SW Lithophane at its defaults: a pale gold skyline
with a white sun and a white sign, soft white points of light in the buildings, and a row of
cream to tan bars along the bottom](arena.jpg)

*What you should see: SW Lithophane inside Resolume Arena 7.29.0 on a Mac, from Arena's own
composition snapshot, at its defaults over a still of the harness's test card. The card is
much brighter than most clips, so at the default Back Light it comes out pale.*

---

## Flashing

Lithophane keeps nothing between frames and makes no flash of its own: a still clip gives a
still picture. But black is the plate's thickest porcelain, which still lets light through,
and Back Light multiplies everything, so a dark clip's small pulses of light become changes in
a picture that is bright all over. **On a dark clip with small flickering lights, that can
turn the flicker into flashing over a large part of the picture.**

People with photosensitive epilepsy can have a seizure from flashing light, and the line that
broadcast guidance (Ofcom) and web guidance (WCAG 2.x) draw is **no more than three flashes
in any second** over a large area of the picture. A flash here is a rise and a fall in
brightness of at least a tenth of white.

Measured from Lithophane's own output over all 33 of Resolume's demo clips, 8 seconds of each,
as rises and falls in brightness in the worst second (two make a flash, so the line is six):

| Settings | the whole picture | the worst third of the picture |
| --- | --- | --- |
| The clips on their own | 5 | 6, **at the line** |
| Lithophane at its defaults | 5 | 7, **over** |
| Back Light 16 (its top) | 0 | 6, **at the line** |

The clip that went over at the defaults was NeonRoom2_32, a dark room with small flickering
neon: 4 on its own, 7 through Lithophane. Bass 006 and Beat 002 reached 6, at the line (Beat
002 is at 6 on its own). IntoTheGlow_02 flashes by itself (5 over the whole picture) and is
the same through the plugin. At Back Light 16 the glow is so bright that most changes stay
above the range a flash is counted in. Other settings have not been surveyed.

What to do:

- **Look at the clip.** A dark clip with small, fast-flickering lights is the case to watch.
- **Watch it through the plugin** before a show, at the settings you will use.

None of this is a substitute for warning your audience. The [browser
demo](https://lithophane-demo.stoatworks-labs.com/) opens on the defaults, with the same note
at the top.

---

## The Plate group

- **Thinnest**: 0.5 to 3 mm. How thick the plate is where the clip is white. 0.8 mm, the
  default, is a printed lithophane generator's thinnest layer.
- **Thickest**: 0.5 to 6 mm. How thick it is where the clip is black. 3 mm is the default,
  about the ⅛ inch of a porcelain lithophane. Thicker is darker, warmer and blurrier in the
  shadows. If Thickest is set below Thinnest, the plate is flat, Thinnest thick everywhere.
- **Material**: what the plate is made of.
  - **Porcelain**, **Feldspar Porcelain** and **Clay Porcelain** are three porcelain bodies
    whose translucency Sugiyama, Horiuchi and Osaki measured (1996). Porcelain, the default,
    is the typical body; Feldspar Porcelain passes far more light; Clay Porcelain far less,
    and redder.
  - **White PLA** is a 3D-printed lithophane in milky white plastic. It passes far more
    light than porcelain.

  Back Light's default was set for Porcelain: with Feldspar Porcelain or White PLA, turn Back
  Light down to about 0.5–1, and with Clay Porcelain up towards 16.
- **Mapping**: how the clip becomes thickness.
  - **Linear Depth**, the default: thickness goes in a straight line with the clip's
    brightness (its luma), as the 3dp.rocks generator carves a plate.
  - **Linear Light**: the thickness is chosen so that the light coming through goes in a
    straight line with the clip's brightness, as Cura's lithophane mode works. Mid-tones
    come out a little different; the ends are the same.
- **Relief Side**: **Facing Viewer** (the default) or **Facing Lamp**. Which face of the plate
  carries the carving: the side you look at, or the side towards the lamp behind. **This only
  shows under front light.** Lit from behind, the model can't tell the two faces apart (see
  [Known limits](#known-limits)), so the picture is exactly the same either way. Lit from the
  front, Facing Viewer shows you the carved relief, and Facing Lamp shows you the flat back,
  with only the faint negative of the thickness on it.
- **Plate Width**: 20 to 400 mm. How wide the plate is, across the frame. The light spreads
  about a millimetre inside the porcelain whatever the plate's size, so a bigger plate keeps
  finer detail and a small one blurs more. 100 mm is the default.

## The Light group

- **Back Light**: 0 to 16. The lamp behind the plate. 7.2, the default, was set on
  Resolume's own demo clips: at it, the brightest 1% of the middle clip just reaches full
  brightness in red, the colour porcelain passes most. Brighter clips clip in red, so turn it
  down for them; dark clips may want more.
- **Front Light**: 0 to 2. A lamp in front of the plate, in the room. At 0, the default, the
  room is dark.
- **Light Angle**: 0 to 360°. Where the front lamp is, around the plate: 135°, the default,
  is the upper left. 0° and 360° are the same lamp.
- **Light Elevation**: 5 to 90°. How high the front lamp is above the plate's surface: 90° is
  straight on, low angles graze the surface and bring out the carving. 45° is the default.

Light Angle, Light Elevation and Relief Side act on the front light only. With Front Light at
0 they change nothing.

![A pale grey test card lit from the front: the skyline, the sun and the sign appear as a
carved relief, their edges standing out in thin shadowed lines, with no colour](relief.png)

*The test card front-lit (Back Light 0, Front Light 1), rendered by the harness: a white
relief. The carving is shaded by the lamp's direction; thin places read very slightly darker.*

## The Look group

- **Mix**: from the clip (0) to the plate (1). At 1 the plate is opaque, even where the clip
  was transparent: a transparent part of the clip is the plate's thickest porcelain, not a
  hole. At 0 the clip passes through untouched.

---

## Recipes

- **The lithophane on a lampshade.** The defaults, on a figure or a portrait on black.
- **A thin, bright plate.** Thickest down to 1.5 mm and Back Light down: less contrast,
  less blur, the whole picture glowing.
- **Antique and warm.** Clay Porcelain with Back Light at 12 to 16: deep amber shadows.
- **A printed lithophane.** White PLA with Back Light about 0.5: a cooler, greyer plate.
- **The plate on a desk.** Back Light 0, Front Light 1, Light Elevation about 20°: a white
  carved relief of your clip. Sweep Light Angle slowly for a moving lamp.
- **Between the two.** Back Light 2 and Front Light 0.7: the picture glows through, and the
  carving shows on top.
- **Fade the plate in.** Mix from 0 to 1 over a phrase.

---

## How it works

Each pixel's thickness comes from the clip: Thinnest where it is white, Thickest where it is
black (by its luma for Linear Depth; for Linear Light, so that the light through it is
proportional to the clip's brightness). Each colour passes exp(−k d) of the lamp through
thickness d, with k the porcelain's measured coefficient for red, green and blue. The light
that gets through has been scattered: it comes out spread by the diffusion profile of a slab
of that thickness (Donner and Jensen's multipole model), which the plugin fits once per
material by 15 Gaussians of fixed widths and applies at several resolutions, so a wide blur
costs little. Front light shades the carved surface by Lambert's law from a lamp you place,
times the plate's reflectance over a dark room (Kubelka–Munk), which is lower where the plate
is thin. Then the result is encoded for the screen. Nothing is kept between frames and no
clock is read.

---

## Performance

On the Mac it was measured on (Apple Silicon, shared with other work), GPU time a frame at the
defaults: about **2.2 ms** at 720p, **4.2 ms** at 1080p (a quarter of a 60 fps frame) and
**10.5 ms** at 4K. With the widest blur (Plate Width 20 mm, Thickest 6 mm, Front Light on):
**4.4 ms** at 1080p. Most of it is the blur's passes. Another GPU will differ, and nothing has
been timed inside Resolume.

---

## If it looks wrong

- **Everything is pale gold, and my clip's colours are gone.** That's the plate: porcelain
  passes red more than blue, so every colour becomes the lamp's warm white through some
  thickness of porcelain. Only brightness makes the thickness.
- **It's almost white.** Back Light is too high for the clip or the material. Turn it down,
  especially with Feldspar Porcelain or White PLA.
- **It's very dark brown.** Back Light is too low, the plate too thick, or the material is
  Clay Porcelain. Turn Back Light up.
- **The black background glows.** It's the thickest porcelain, and porcelain lets some light
  through even at 3 mm. Turn Thickest up to darken it.
- **It's blurry.** The light scatters about a millimetre in the porcelain, and more where it's
  thick. Turn Plate Width up for finer detail, or Thickest down.
- **Relief Side, Light Angle and Light Elevation do nothing.** They act on the front light.
  Turn Front Light up.
- **My transparent clip came out opaque.** At Mix 1 the plate is opaque, and a transparent
  pixel is thick porcelain. Lower Mix to bring the clip's transparency back.

---

## Known limits

- **Never compared with a real lithophane.** Every figure is checked against the published
  measurements and the diffusion model, not against a photographed plate.
- **Relief Side does nothing under back light.** The model treats each point of the plate as
  a column that passes and spreads its own light by its own thickness (the column
  approximation); light crossing between thick and thin columns is left out, and with it any
  difference between the two faces under back light. A real plate would show some.
- **Which face is carved.** No porcelain source I read says which face of a lithophane carries
  the relief. The 3D-printing guides put it towards the viewer, and that is the default.
- **White PLA is over-blurred.** About a third of the light through milky PLA comes nearly
  straight through, which a diffusion model can't carry, so the plugin blurs it more than a
  real print. Its absorption and scattering numbers are also chosen from limited data.
- **Back Light's default clips in red.** It was set so the brightest 1% of the middle demo
  clip just reaches full red; by the nature of a middle, about half of Resolume's demo clips
  are brighter and clip in red. A gentler default would clip fewer.
- **The front light has no shadows, no gloss and no blur.** The porcelain is unglazed, so it
  has no gloss; but a low lamp on real carved porcelain would cast shadows, and light reflected
  from inside the plate would be softened. Neither is modelled.
- **The widest blur costs a whole group of passes for very little light.** The broadest of the
  15 Gaussians carries about one ten-thousandth of the light through 6 mm and still costs a
  group of passes. Dropping it would save time and change the picture by less than any check
  can see; it is kept for this release.
- **Chosen, not measured:** one wavelength per colour channel (650, 550 and 450 nm), the
  porcelain's absorption taken from its measured apparent coefficient, White PLA's numbers,
  the front lamp's default direction, and Facing Viewer as the default side.
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's demo clips decode to. Whether Resolume hands an effect premultiplied colour has not
  been checked.
- **Tried in Resolume on a test card only.** It loads and renders in Resolume Arena 7.29.0 on
  a Mac and 7.27.1 on Windows (software rendering, no GPU), and every control changed the
  picture, but it hasn't been through a show, with real footage, or timed inside Resolume.
- **Other GPUs.** It has run on one Apple GPU, Apple's software renderer and, in Resolume on
  Windows, Mesa's software renderer only.

---

## About

The last group, **About**, has the plugin's name, version, licence and maker, and buttons that
open this user guide, the project page, the source on GitHub and the support page in your
browser.

## Links

- Project page: [stoatworks-labs.com/software/lithophane](https://stoatworks-labs.com/software/lithophane/)
- Source: [github.com/stoatworks-labs/lithophane](https://github.com/stoatworks-labs/lithophane)
- User guide: [stoatworks-labs.com/software/lithophane/guide](https://stoatworks-labs.com/software/lithophane/guide/)
- Browser demo: [lithophane-demo.stoatworks-labs.com](https://lithophane-demo.stoatworks-labs.com/)

## Reporting something

Something wrong, odd or missing? Open an issue at
[github.com/stoatworks-labs/lithophane/issues](https://github.com/stoatworks-labs/lithophane/issues).
The plugin keeps a short log of what the host did (the GL renderer, any shader that failed
to compile, and how well each material's blur was fitted), which helps a lot:

```
macOS    ~/Library/Logs/lithophane/
Windows  %LOCALAPPDATA%\lithophane\logs\
```
