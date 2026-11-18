# Attributions

Lithophane is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness and the tooling — Stoatworks convergence, and intensifier, filings, fountain and ferrofluid before it

<https://github.com/stoatworks-labs/convergence>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, `--set` by display name, `--pipe`/`--film` (SIGPIPE ignored, a closed stdout an exit of 1), `--negative` with its Perturb, and `--offline` (tools/lptest is convergence's cvtest, which is intensifier's ittest, filings' fgtest, fountain's fttest and ferrofluid's fetest, under new checks); tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh, tools/sweep.py and tools/clips.py. All adapted.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of intensifier and convergence. source/GLState.h is radar's, which is vectrix's, from resolume-scopes, with convergence's additions (the pixel-unpack buffer and store, the negative control's skipped restore). source/MultiTarget.{h,cpp} is new here, in PassBuffer's shape.

### The release workflow and the DLL check — Stoatworks bolometer, penumbra and xenon

<https://github.com/stoatworks-labs/bolometer>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are bolometer's (penumbra's, xenon's, slitscan's before them): a universal bundle and a Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is penumbra's (pepper's). Adapted.

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in lptest and the picture under `--alpha` and `--robust`.

### The heightfield normal — Stoatworks repousse

<https://github.com/stoatworks-labs/repousse>  
Licence: MIT  
Copyright: Stoatworks Labs

The front light's normal follows repousse's convention (its shade pass in source/Shaders.cpp): a central difference of the height in the frame's own unit, one-sided at the frame's edge, n = normalize(vec3(−g, 1)), the viewer down +z. Matched, not copied.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### Measurement of Translucency of Porcelain Bodies — T. Sugiyama, T. Horiuchi and Y. Osaki, J. Ceram. Soc. Japan 104[9] 877–881 (1996)

Bodies (a)–(c): the apparent absorption coefficients k (Fig. 5), the Kubelka–Munk K and S (Figs. 6, 7) and the reflectances (Fig. 8), digitised from the J-STAGE page images — the three porcelain materials, their transmission and their front reflectance, with the K–M boundary terms r₁ = 0.06 and r₂ = 0.5 after Hogg (source/Plate.cpp). lptest --model, --attenuation and --front check the plugin against them. Read on 2026-11-16.

### Light Diffusion in Multi-Layered Translucent Materials — C. Donner and H. W. Jensen, ACM TOG 24(3) 1032–1039 (2005)

The multipole slab profile (eqs. 11–13) and the diffuse Fresnel fit F_dr: the scattering blur's true kernel, which the plugin fits by 15 Gaussians (source/Plate.cpp) and lptest --model and --blur integrate and compare. Read on 2026-11-16.

### Optical Properties and Lampshade Design Applications of PLA 3D Printing Materials — L. Wang and Y. Zhou, BioResources 18(1) 1545–1553 (2023)

Transmittance and haze of milky white PLA against wall thickness (Figs. 6, 7): White PLA's k, and why the diffusion model over-blurs it (haze 56–64%). Read on 2026-11-16.

### Printed PLA optical properties by spatial frequency domain imaging — S. Ragunathan et al., Sci. Rep. 15:9705 (2025)

The μs′ mixing fit, extrapolated to white PLA (3.75 mm⁻¹, chosen), and the index n = 1.4. Read on 2026-11-16.

### Kubelka–Munk and transport coefficients — Pham et al., arXiv 2408.04248v3 (2025)

K ≈ 2μa and S ≈ (3/4) μs′: the bridge between Sugiyama's Kubelka–Munk coefficients and the multipole's transport coefficients (source/Plate.cpp). Read on 2026-11-16.

### Ultimaker Cura's ImageReader and 3dp.rocks/lithophane — Ultimaker; 3dp.rocks

plugins/ImageReader/ImageReader.py (Cura's transparency model: the Linear Light mapping) and 3dp.rocks' js/main.js and data/layout.json (the luma height law and the default plate: Linear Depth, 0.8 / 3 mm, 100 mm). Read for the rule and the numbers; no code taken.

### Lithophanes: thickness, porcelain and which face is which — Wikipedia "Lithophane"; S. Osser, Ceramics Monthly (February 2022); the Meissen glossary; Museum Wales (NMW A 32036); Instructables; Omnvert

Plate thickness (3.2 mm, ⅛ inch), unglazed biscuit porcelain, "dark areas need to be applied thickly", a lithophane's colour lit and unlit, and which face is carved and which faces the lamp (Relief Side's default). Read on 2026-11-16.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — the clip's decoding and the output's encoding (source/Shaders.cpp; the harness's own copy in tools/lptest).
- **ITU-R BT.601** — the luma weights 0.299, 0.587, 0.114 of Linear Depth's thickness law, as 3dp.rocks uses them (source/Shaders.cpp).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
