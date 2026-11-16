#pragma once

#include <array>
#include <vector>

/**
    The plate's physics, on the CPU in double (SPEC-lithophane.md, The
    mechanism).

    The materials: Sugiyama, Horiuchi & Osaki 1996 (J. Ceram. Soc. Japan
    104[9] 877) bodies (a), (b), (c) -- the apparent absorption coefficient k
    (Fig. 5), the Kubelka-Munk K and S (Figs. 6, 7), digitised at 650/550/450
    nm for R/G/B -- and Wang & Zhou 2023's white PLA (k, the two-point slope of
    their Fig. 6), with Ragunathan et al.'s extrapolated reduced scattering
    (chosen).

    The blur: Donner & Jensen 2005's multipole transmittance profile of a slab,
    fitted per channel and per thickness (0.5..6 mm, 0.05 mm rows) by
    non-negative least squares as a mixture of 15 fixed Gaussians, sigma_j =
    0.0625 sqrt(2)^j mm, with the mixture's total and per-axis variance held
    to the profile's. The plate is then sum_j G_j * (a w_j(d)): exact for the
    mixture.

    The frame's plan: which scales are active for the plate's thickness range,
    which are under a quarter pixel (no blur), the decimation factor of each
    (the largest power of two with sigma_px / f >= 2), the groups of up to four
    scales sharing a factor, and every sampled kernel's sigma solved so its
    discrete variance is exactly the intended one.
*/
namespace lithophane::plate
{

constexpr int kScales      = 15;
constexpr double kScale0   = 0.0625;///< mm, sigma_0
constexpr double kRowOrigin = 0.5;  ///< mm, the table's first row
constexpr double kRowStep  = 0.05;  ///< mm
constexpr int kRows        = 111;   ///< 0.5 .. 6.0 mm
constexpr int kDeltaColumn = kScales;///< the weight table's column for the scales under a quarter pixel
constexpr int kTableWidth  = kScales + 1;
constexpr int kMaxGroupScales = 4;
constexpr int kMaxGroups      = 8;
constexpr double kDeltaBelowPx = 0.25;///< a scale narrower than this is the pixel itself
constexpr double kActiveWeight = 1e-5;///< a scale weighing less everywhere in the range is dropped

double ScaleSigmaMm( int j );

/// One material's data, per channel R, G, B (650, 550, 450 nm).
struct Material
{
	const char* name;
	double k[ 3 ];   ///< apparent absorption coefficient, 1/mm (transmission)
	double K[ 3 ];   ///< Kubelka-Munk absorption, 1/mm (front reflectance)
	double S[ 3 ];   ///< Kubelka-Munk scattering, 1/mm (front reflectance)
	double musp[ 3 ];///< reduced scattering, 1/mm (the blur)
	double Fdr;      ///< internal diffuse boundary reflectance
};
const Material& MaterialAt( int index );

/// The multipole's parameters for one channel.
struct Diffusion
{
	double mua, musp, zb, ell, alpha, sigtr;
};
Diffusion DiffusionOf( const Material& m, int channel );

/// Image pairs each side of the slab. The second moment converges slowest,
/// in the least absorbing material: PLA's far images still carry 1.4% of its
/// variance past 10 pairs, under 1e-9 past 30 (lptest --model).
constexpr int kImagePairs = 30;

/// The radial transmittance profile T(r) of a slab d thick, 1/mm^2 per unit
/// incident power (Donner & Jensen eq. 13), kImagePairs each side.
double Profile( const Diffusion& p, double d, double r );
/// The normalised profile's per-axis variance, mm^2, in closed form from the
/// same image sources.
double AxisVariance( const Diffusion& p, double d );

/// The fitted table for one material: weights[ (row * kScales + j) * 3 + c ],
/// each row and channel summing to 1; and the fit's worst 2-D L1 distance.
struct FitTable
{
	std::vector< double > weights;
	double worstL1 = 0.0;
	double worstL1At[ 2 ] = {};///< thickness mm, channel
};
const FitTable& TableFor( int material );

/// Lawson-Hanson non-negative least squares on the normal equations
/// G w = h (n x n, row-major). Returns w >= 0.
std::vector< double > Nnls( const std::vector< double >& G, const std::vector< double >& h, int n );

/// A sampled, truncated, normalised Gaussian whose discrete variance is
/// exactly `variance` (px^2): radius ceil( 4 sqrt( variance ) ), its width
/// solved by bisection.
std::vector< double > ExactKernel( double variance );

/// A frame's plan.
struct Group
{
	int factor = 1;               ///< decimation f
	int count  = 0;               ///< scales in the group
	int scale[ kMaxGroupScales ]  = {};
	int radius[ kMaxGroupScales ] = {};///< the sampled kernel's half-width, low-res texels
	int kernelRow[ kMaxGroupScales ] = {};
	int lowWidth = 0, lowHeight = 0;///< ceil( W / f ) + 4 (two texels of margin each side), or W, H at f = 1
};
struct Plan
{
	double pitch = 0.0;          ///< mm per pixel
	double dThin = 0.0, dThick = 0.0;
	int material = 0;
	std::vector< Group > groups;
	bool active[ kScales ] = {};
	bool delta[ kScales ]  = {};
	int factor[ kScales ]  = {};
	double sigmaPx[ kScales ] = {};
	/// The weight table as uploaded: kTableWidth x kRows RGBA (the delta
	/// column last), renormalised over what is kept.
	std::vector< float > table;
	/// The kernels: one row per kernel, 2 * kernelRadius + 1 wide, RGBA (r used).
	std::vector< float > kernels;
	int kernelRadius = 0;
	int kernelRows   = 0;
};
/// `hookOneKernel`, `hookSingle`: the harness's negative controls (every
/// thickness given the 1.5 mm row; every row given the variance-matched
/// single Gaussian's nearest scale pair).
Plan PlanFor( int material, double dThin, double dThick, double plateWidthMm, int width, int height, int kernelModel );

/// Kubelka-Munk diffuse reflectance over a black backing with Sugiyama's
/// boundary terms (r1 0.06, r2 0.5), for the spec and the harness's report.
double FrontReflectance( double K, double S, double d );

} // namespace lithophane::plate
