#pragma once

#include <string>

/**
    The passes, in the order a frame runs them (SPEC-lithophane.md, The
    chain). Every one is a full-raster triangle into a buffer of its own;
    nothing is blended.

    1. **carve** -- per pixel: the clip decoded to linear, weighted by its
       alpha, re-encoded, the mapping's thickness d in mm, and each channel's
       column transmission a_c = exp(-k_c d). RGBA32F: (a_R, a_G, a_B, d).
    2. **spread** -- along x, from the carve texture: each tap's a times its
       thickness's weight for each scale of the group (the weight table read
       by d, rows interpolated by hand), either through the scale's sampled
       Gaussian (a group at full resolution) or through the quadratic
       B-spline decimation by the group's factor. Up to four scales at once,
       one render target each.
    3. **pass** -- along x or y, from the targets of the pass before: a
       sampled Gaussian per scale, or the B-spline decimation along y; the
       last pass of a group sums its scales into one texture.
    4. **composite** -- to the host: the scales under a quarter pixel as
       a * w(d); every group's texture, reconstructed by the B-spline at its
       factor (texelFetch, weights in float, by hand); times Back Light; plus
       the front light's Lambertian term off the carved face; encoded to sRGB;
       mixed with the clip, premultiplied.

    Past the frame's edge every pass clamps to the edge: the plate continues.
    No time reaches a shader. The weights and kernels are computed in double
    on the CPU (Plate.h). tools/glslc.sh's ASSEMBLED table mirrors Assemble().
*/
namespace lithophane::shaders
{

/// The kernels arrive as a uniform array, kKernelTaps per scale, the tap at
/// offset o stored at o + kKernelCentre. The GLSL says the same numbers.
constexpr int kKernelCentre = 20;
constexpr int kKernelTaps   = 2 * kKernelCentre + 1;
/// The composite's group textures (Plate.h, kMaxGroups).
constexpr int kMaxLevels = 8;

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kCarveFragment;
extern const char* const kSpreadFragment;
extern const char* const kPassFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Carve,
	Spread,
	Pass,
	Composite
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

} // namespace lithophane::shaders
