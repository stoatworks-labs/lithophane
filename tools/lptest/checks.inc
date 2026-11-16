// The checks, one flag each. Included into main.cpp's anonymous namespace.
//
// Every claim is measured out of the picture (the float readback of the
// host's framebuffer). What it is measured against is restated here from
// SPEC-lithophane.md with the harness's own constants and numerics: the
// materials' table, the multipole profile by direct summation with more
// image pairs than the plugin's, its line spread by Gauss-Legendre
// quadrature, the mappings, the Kubelka-Munk reflectance and Lambert's law.
// Nothing here calls the plugin's fit to predict a picture.

//===========================================================================
// Error sources, each named once. Every tolerance below is a sum of these.
//===========================================================================
/// One float rounding.
constexpr double kFloat = 1.0 / 16777216.0;
/// The intermediates are RGBA32F: a value stored rounds to nearest, relative
/// 2^-24. (They were RGBA16F, relative 2^-11: the half floats' rounding of the
/// 6 mm background put the line's second moment 9% off -- 21.7 against 23.8
/// px^2 at 1280x720 -- inside a tolerance that had to allow it.)
constexpr double kStore = 1.0 / 16777216.0;
/// The deepest chain of stored roundings a value meets: spread, the
/// decimation along y, the Gaussian along x, the group's sum (Shaders.h).
constexpr int kStoreRoundings = 4;
/// A float under 2^-126 may be flushed to zero by the GPU: 2^-126 absolute.
constexpr double kFlush = 1.1754943508222875e-38;
/// The sRGB encode in the shader (GPU float pow: exp2 of p log2 x, a few ulp
/// of a log2 near 10 amplified by p = 1/2.4) and the harness's decode in
/// double: 3e-6 relative at the darkest code used, so 2^-17 with a factor of
/// two to spare.
constexpr double kEncode = 1.0 / 131072.0;
/// The clip's codes through the carve's decode, alpha and re-encode (the same
/// GPU pow both ways) before the mapping reads them: 2^-17 likewise.
constexpr double kRecode = 1.0 / 131072.0;

/// The mixture's per-axis variance against the multipole's, relative, over
/// every material, channel and thickness on the table and midway: the fit's
/// error in the blur radius, measured by --model (which fails if it grows)
/// and used by --blur.
constexpr double kFitVariance = 0.001;
/// The B-spline decimation's L1 distance from the true Gaussian, every factor
/// and phase at sigma/f 2 .. 4 (measured by --model).
constexpr double kSplineL1 = 0.0020;

constexpr double kPi = 3.14159265358979323846;

/// One float's spacing at x.
double ulpOf( double x )
{
	const float f = static_cast< float >( std::fabs( x ) );
	return static_cast< double >( std::nextafter( f, std::numeric_limits< float >::infinity() ) - f );
}

/// The larger of two errors, NaN winning: std::max( worst, NaN ) keeps
/// worst, so a model that produced NaN would pass every "worst <= tolerance".
double worse( double a, double b )
{
	return ( a != a || b != b ) ? std::numeric_limits< double >::quiet_NaN() : std::max( a, b );
}
template< typename... T >
double worse( double a, double b, T... rest )
{
	return worse( worse( a, b ), rest... );
}

//===========================================================================
// The harness's own physics (SPEC-lithophane.md, The mechanism).
//===========================================================================
namespace hp
{
/// The materials' table, restated: k, K, S per channel R/G/B (650/550/450 nm).
struct Mat
{
	const char* name;
	double k[ 3 ], K[ 3 ], S[ 3 ];
	bool pla;
};
const Mat kMats[ 4 ] = {
	{ "Porcelain", { 0.90, 1.01, 1.42 }, { 0.064, 0.064, 0.101 }, { 3.74, 4.73, 6.43 }, false },
	{ "Feldspar Porcelain", { 0.58, 0.59, 0.75 }, { 0.022, 0.011, 0.012 }, { 2.80, 3.40, 4.35 }, false },
	{ "Clay Porcelain", { 1.49, 2.05, 2.95 }, { 0.193, 0.300, 0.481 }, { 4.74, 6.00, 8.51 }, false },
	{ "White PLA", { 0.293, 0.331, 0.429 }, { 0, 0, 0 }, { 0, 0, 0 }, true },
};
constexpr double kPlaMusp = 3.75;///< Ragunathan et al. extrapolated (the spec's choice)
constexpr double kPlaIndex = 1.4;

double fresnelDiffuse( double eta )
{
	return -1.4399 / ( eta * eta ) + 0.7099 / eta + 0.6681 + 0.0636 * eta;
}

struct Chan
{
	double mua, musp, zb, ell, alpha, sigtr, K, S;
};
Chan chan( int m, int c )
{
	const Mat& mat = kMats[ m ];
	Chan p;
	p.musp          = mat.pla ? kPlaMusp : 4.0 * mat.S[ c ] / 3.0;
	const double k  = mat.k[ c ];
	//sigma_tr = sqrt( 3 mua ( mua + musp ) ) = k, solved for mua.
	p.mua           = 0.5 * ( -p.musp + std::sqrt( p.musp * p.musp + 4.0 * k * k / 3.0 ) );
	const double t  = p.mua + p.musp;
	const double F  = mat.pla ? fresnelDiffuse( kPlaIndex ) : 0.5;
	const double A  = ( 1.0 + F ) / ( 1.0 - F );
	p.zb            = 2.0 * A / ( 3.0 * t );
	p.ell           = 1.0 / t;
	p.alpha         = p.musp / t;
	p.sigtr         = std::sqrt( 3.0 * p.mua * t );
	p.K             = mat.pla ? 2.0 * p.mua : mat.K[ c ];
	p.S             = mat.pla ? 0.75 * p.musp : mat.S[ c ];
	return p;
}

/// The image sources' signed axial distances and signs, n pairs each side.
template< typename F >
void sources( const Chan& p, double d, int n, F&& each )
{
	for( int i = -n; i <= n; ++i )
	{
		const double zr = 2.0 * i * ( d + 2.0 * p.zb ) + p.ell;
		const double zv = 2.0 * i * ( d + 2.0 * p.zb ) - p.ell - 2.0 * p.zb;
		each( d - zr, 1.0 );
		each( d - zv, -1.0 );
	}
}

/// Donner & Jensen eq. 13 by direct summation.
double T( const Chan& p, double d, double r, int n = 80 )
{
	double v = 0.0;
	sources( p, d, n, [ & ]( double h, double sg ) {
		const double rho = std::sqrt( r * r + h * h );
		v += sg * p.alpha * h * ( 1.0 + p.sigtr * rho ) * std::exp( -p.sigtr * rho ) / ( 4.0 * kPi * rho * rho * rho );
	} );
	return v;
}

/// The moments in closed form: per source, int f 2 pi r dr = alpha sgn(h)
/// e^(-s|h|) / 2 and int r^2 f 2 pi r dr = alpha h e^(-s|h|) / s (the
/// harness's own integration by parts: d/drho of -e^(-s rho)/rho is
/// e^(-s rho)(1 + s rho)/rho^2).
void moments( const Chan& p, double d, double& M0, double& M2, int n = 80 )
{
	M0 = M2 = 0.0;
	sources( p, d, n, [ & ]( double h, double sg ) {
		const double e = std::exp( -p.sigtr * std::fabs( h ) );
		M0 += sg * p.alpha * 0.5 * ( h > 0.0 ? 1.0 : -1.0 ) * e;
		M2 += sg * p.alpha * h * e / p.sigtr;
	} );
}
/// Per-axis variance of the normalised profile, mm^2.
double axisVariance( const Chan& p, double d )
{
	double M0, M2;
	moments( p, d, M0, M2 );
	return M2 / ( 2.0 * M0 );
}

/// Gauss-Legendre nodes on [-1, 1] by Newton's method on P_n.
struct Gauss
{
	std::vector< double > x, w;
	explicit Gauss( int n )
	{
		x.resize( static_cast< size_t >( n ) );
		w.resize( static_cast< size_t >( n ) );
		for( int i = 0; i < n; ++i )
		{
			double z = std::cos( kPi * ( i + 0.75 ) / ( n + 0.5 ) ), pp = 0.0;
			for( int it = 0; it < 100; ++it )
			{
				double p1 = 1.0, p2 = 0.0;
				for( int j = 1; j <= n; ++j )
				{
					const double p3 = p2;
					p2              = p1;
					p1              = ( ( 2.0 * j - 1.0 ) * z * p2 - ( j - 1.0 ) * p3 ) / j;
				}
				pp              = n * ( z * p1 - p2 ) / ( z * z - 1.0 );
				const double dz = p1 / pp;
				z -= dz;
				if( std::fabs( dz ) < 1e-15 )
					break;
			}
			x[ static_cast< size_t >( i ) ] = z;
			w[ static_cast< size_t >( i ) ] = 2.0 / ( ( 1.0 - z * z ) * pp * pp );
		}
	}
};

/// The line spread function: 2 int_0^inf T( sqrt( u^2 + v^2 ) ) dv, by
/// Gauss-Legendre on panels in v (T is smooth: the nearest source is d - l
/// >= 0.23 mm deep).
double lsfRaw( const Chan& p, double d, double u )
{
	static const Gauss g( 24 );
	static const double edges[] = { 0.0, 0.25, 1.0, 3.0, 8.0, 20.0, 60.0 };
	double sum = 0.0;
	for( int k = 0; k + 1 < 7; ++k )
	{
		const double a = edges[ k ], b = edges[ k + 1 ];
		for( size_t i = 0; i < g.x.size(); ++i )
		{
			const double v = 0.5 * ( a + b ) + 0.5 * ( b - a ) * g.x[ i ];
			sum += 0.5 * ( b - a ) * g.w[ i ] * T( p, d, std::sqrt( u * u + v * v ) );
		}
	}
	return 2.0 * sum;
}

/// The normalised line spread of a slab, tabulated (0.02 mm to 50 mm) and
/// read by Catmull-Rom.
struct Lsf
{
	double step = 0.02;
	std::vector< double > table;
	double total = 0.0;///< closed-form M0
	Lsf( int m, int c, double d )
	{
		const Chan p = chan( m, c );
		double M2;
		moments( p, d, total, M2 );
		table.resize( 2502 );
		for( size_t i = 0; i < table.size(); ++i )
			table[ i ] = lsfRaw( p, d, step * static_cast< double >( i ) ) / total;
	}
	double operator()( double u ) const
	{
		u              = std::fabs( u ) / step;
		const size_t i = static_cast< size_t >( u );
		if( i + 2 >= table.size() )
			return 0.0;
		const double t  = u - static_cast< double >( i );
		const double p0 = i == 0 ? table[ 1 ] : table[ i - 1 ], p1 = table[ i ], p2 = table[ i + 1 ], p3 = table[ i + 2 ];
		return p1 + 0.5 * t * ( p2 - p0 + t * ( 2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3 + t * ( 3.0 * ( p1 - p2 ) + p3 - p0 ) ) );
	}
};

/// Kubelka-Munk over a black backing, Sugiyama's boundary terms.
double reflectance( double K, double S, double d )
{
	const double kappa = std::sqrt( K * ( K + 2.0 * S ) );
	const double R     = S / ( K + S + kappa / std::tanh( kappa * d ) );
	return 0.06 + 0.94 * 0.5 * R / ( 1.0 - 0.5 * R );
}

/// The quadratic B-spline.
double bspline( double u )
{
	u = std::fabs( u );
	return u < 0.5 ? 0.75 - u * u : ( u < 1.5 ? 0.5 * ( u - 1.5 ) * ( u - 1.5 ) : 0.0 );
}

/// The spec's scales.
double scale( int j )
{
	return 0.0625 * std::pow( std::sqrt( 2.0 ), j );
}

/// The spec's discrete kernel of variance v px^2: exp( -i^2 / 2 s^2 ) over
/// |i| <= ceil( 4 sqrt v ), normalised, s found by bisection on the discrete
/// variance (the harness's own search, in long double, 400 halvings).
std::vector< double > exactKernel( double v )
{
	const int R = std::max( 1, static_cast< int >( std::ceil( 4.0 * std::sqrt( v ) ) ) );
	auto make   = [ R ]( long double s, std::vector< double >* out ) {
        long double sum = 0.0L, second = 0.0L;
        std::vector< long double > g( static_cast< size_t >( 2 * R + 1 ) );
        for( int i = -R; i <= R; ++i )
        {
            g[ static_cast< size_t >( i + R ) ] = std::exp( -0.5L * i * i / ( s * s ) );
            sum += g[ static_cast< size_t >( i + R ) ];
            second += static_cast< long double >( i ) * i * g[ static_cast< size_t >( i + R ) ];
        }
        if( out )
        {
            out->resize( g.size() );
            for( size_t k = 0; k < g.size(); ++k )
                ( *out )[ k ] = static_cast< double >( g[ k ] / sum );
        }
        return second / sum;
    };
	long double lo = 1e-4L, hi = 8.0L * std::sqrt( static_cast< long double >( v ) ) + 4.0L;
	for( int i = 0; i < 400; ++i )
	{
		const long double mid = 0.5L * ( lo + hi );
		( make( mid, nullptr ) < v ? lo : hi ) = mid;
	}
	std::vector< double > k;
	make( 0.5L * ( lo + hi ), &k );
	return k;
}

/// The spec's realisation of scale j at a pitch, as a 1-D line spread over
/// pixels (the 2-D kernel is separable, so a line one pixel wide spreads
/// as the 1-D kernel across it): a delta under a quarter pixel, otherwise
/// the discrete kernel of variance sigma_px^2 (exact through the decimation
/// too: the B-spline chain's variance is sigma^2, its shape within the
/// spline bound of this kernel).
std::vector< double > realised( int j, double pitch )
{
	const double sp = scale( j ) / pitch;
	if( sp < 0.25 )
		return { 1.0 };
	return exactKernel( sp * sp );
}
} // namespace hp

//===========================================================================
// The controls, by the harness's own conversions (SPEC-lithophane.md,
// Controls): a physical value to the host's 0..1, and the float the host
// stores back to the physical value the plugin will use.
//===========================================================================
float pThin( double mm )
{
	return static_cast< float >( ( mm - 0.5 ) / 2.5 );
}
double hThin( float v )
{
	return 0.5 + 2.5 * static_cast< double >( v );
}
float pThick( double mm )
{
	return static_cast< float >( ( mm - 0.5 ) / 5.5 );
}
double hThick( float v )
{
	return 0.5 + 5.5 * static_cast< double >( v );
}
float pPlate( double mm )
{
	return static_cast< float >( ( mm - 20.0 ) / 380.0 );
}
double hPlate( float v )
{
	return 20.0 + 380.0 * static_cast< double >( v );
}
float pBack( double level )
{
	return static_cast< float >( level / 16.0 );
}
double hBack( float v )
{
	return 16.0 * static_cast< double >( v );
}
float pFront( double level )
{
	return static_cast< float >( level / 2.0 );
}
double hFront( float v )
{
	return 2.0 * static_cast< double >( v );
}
float pAngle( double degrees )
{
	return static_cast< float >( degrees / 360.0 );
}
double hAngle( float v )
{
	return 360.0 * static_cast< double >( v );
}
float pElevation( double degrees )
{
	return static_cast< float >( ( degrees - 5.0 ) / 85.0 );
}
double hElevation( float v )
{
	return 5.0 + 85.0 * static_cast< double >( v );
}

/// The thickness the harness expects from 8-bit codes (straight, alpha 1).
double hLumaDepth( const double code[ 3 ], double thin, double thick )
{
	const double y = ( 0.299 * code[ 0 ] + 0.587 * code[ 1 ] + 0.114 * code[ 2 ] ) / 255.0;
	return thin + ( 1.0 - y ) * ( std::max( thick, thin ) - thin );
}
double hCuraLum( const double code[ 3 ] )
{
	return 0.299 * std::pow( code[ 0 ] / 255.0, 2.2 ) + 0.587 * std::pow( code[ 1 ] / 255.0, 2.2 ) + 0.114 * std::pow( code[ 2 ] / 255.0, 2.2 );
}
double hLightDepth( const double code[ 3 ], double thin, double thick, double kG )
{
	const double span = std::max( thick, thin ) - thin;
	const double minT = std::exp( -kG * span );
	return thin - std::log( minT + ( 1.0 - minT ) * hCuraLum( code ) ) / kG;
}

//===========================================================================
// Rigs and pictures.
//===========================================================================
/// Every check sets every control it depends on. These are the quiet ones:
/// the default plate, a lamp of 1 behind, no front light.
void defaults( Rig& rig )
{
	rig.Set( PT_THINNEST, pThin( 0.8 ) );
	rig.Set( PT_THICKEST, pThick( 3.0 ) );
	rig.Set( PT_MATERIAL, 0 );
	rig.Set( PT_MAPPING, 0 );
	rig.Set( PT_RELIEF_SIDE, 0 );
	rig.Set( PT_PLATE_WIDTH, pPlate( 100.0 ) );
	rig.Set( PT_BACK_LIGHT, pBack( 1.0 ) );
	rig.Set( PT_FRONT_LIGHT, 0.0f );
	rig.Set( PT_LIGHT_ANGLE, pAngle( 135.0 ) );
	rig.Set( PT_LIGHT_ELEVATION, pElevation( 45.0 ) );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.HooksForTest() = perturb;
	defaults( rig );
	return true;
}

/// The output's linear light per channel at one pixel.
double linAt( const Floats& out, int w, int x, int y, int c )
{
	return linearOf( out[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] );
}

/// 8-bit codes as the card's floats.
float code8( double c )
{
	return static_cast< float >( c / 255.0 );
}

/// The look card with every pixel quantised to an exact 8-bit code.
Floats lookCard( int w, int h )
{
	Floats card = buildCard( w, h );
	for( size_t i = 0; i < card.size(); ++i )
		card[ i ] = static_cast< float >( std::lround( card[ i ] * 255.0f ) ) / 255.0f;
	return card;
}

/// The largest decimation factor the plan uses, and its group count.
int widestFactor( const plate::Plan& plan )
{
	int f = 1;
	for( const plate::Group& g : plan.groups )
		f = std::max( f, g.factor );
	return f;
}

/// Float accumulation along the deepest path: the spread and decimation's
/// 3f taps each, two Gaussians of at most 41 taps, the 9-tap reconstruction,
/// and the composite's sum of the groups: relative n 2^-24 for a sum of
/// positive terms.
double accumulation( const plate::Plan& plan )
{
	const int f = widestFactor( plan );
	return ( 6.0 * f + 2.0 * 41.0 + 9.0 + plate::kMaxGroups ) * kFloat;
}

//===========================================================================
// --model (no GL)
//===========================================================================
int runModel( const Perturb& perturb )
{
	std::printf( "\n=== model: the plugin's materials against the spec's table; the controls; the multipole's closed-form moments against\n"
	             "    numerical integration of the plugin's profile and the spec's numbers; the mixture fit against the harness's profile;\n"
	             "    the kernels' variances; the B-spline decimation against the true Gaussian; the frame plans; the front reflectance\n" );

	//--- the table
	{
		double worst = 0.0;
		for( int m = 0; m < 4; ++m )
		{
			const plate::Material& got = plate::MaterialAt( m );
			for( int c = 0; c < 3; ++c )
			{
				const hp::Chan want = hp::chan( m, c );
				worst = worse( worst, std::fabs( got.k[ c ] - hp::kMats[ m ].k[ c ] ), std::fabs( got.K[ c ] - want.K ) / want.K,
				               std::fabs( got.S[ c ] - want.S ) / want.S, std::fabs( got.musp[ c ] - want.musp ) / want.musp );
			}
			worst = worse( worst, std::fabs( got.Fdr - ( hp::kMats[ m ].pla ? hp::fresnelDiffuse( 1.4 ) : 0.5 ) ) );
			worst = worse( worst, std::strcmp( got.name, kMaterialNames[ m ] ) == 0 && std::strcmp( got.name, hp::kMats[ m ].name ) == 0 ? 0.0 : 1.0 );
		}
		bool ordered = true;
		for( int m = 0; m < 4; ++m )
			ordered = ordered && hp::kMats[ m ].k[ 0 ] < hp::kMats[ m ].k[ 1 ] && hp::kMats[ m ].k[ 1 ] < hp::kMats[ m ].k[ 2 ];
		Check( worst <= 1e-12 && ordered,
		       fmt( "the four materials' k, K, S, mu_s' and F_dr against the spec's table (PLA's K = 2 mu_a, S = 0.75 mu_s' derived): worst difference "
		            "%.1e (tolerance 1e-12); names match the options; k_R < k_G < k_B in every material (red travels further): %s",
		            worst, ordered ? "yes" : "NO" ) );
	}

	//--- the diffusion parameters, and sigma_tr = k by construction
	{
		double worst = 0.0;
		for( int m = 0; m < 4; ++m )
			for( int c = 0; c < 3; ++c )
			{
				const plate::Diffusion got = plate::DiffusionOf( plate::MaterialAt( m ), c );
				const hp::Chan want        = hp::chan( m, c );
				worst = worse( worst, std::fabs( got.mua - want.mua ) / want.mua, std::fabs( got.zb - want.zb ) / want.zb, std::fabs( got.ell - want.ell ) / want.ell,
				               std::fabs( got.alpha - want.alpha ), std::fabs( got.sigtr - hp::kMats[ m ].k[ c ] ) / hp::kMats[ m ].k[ c ] );
			}
		const hp::Chan a = hp::chan( 0, 1 );
		const bool spec  = std::fabs( a.mua - 0.0535 ) <= 0.00005 && std::fabs( a.musp - 6.307 ) <= 0.0005 && std::fabs( a.zb - 0.3145 ) <= 0.00005;
		Check( worst <= 1e-12 && spec,
		       fmt( "mu_a, z_b, l, alpha' of every material and channel against the harness's; sigma_tr = k: worst %.1e (tolerance 1e-12); body (a) "
		            "at 550 nm mu_a %.5f, mu_s' %.4f, z_b %.4f /mm (the spec: 0.0535, 6.307, 0.3145, to their last digit)",
		            worst, a.mua, a.musp, a.zb ) );
	}

	//--- the profile: the plugin's sum against the harness's
	{
		double worst = 0.0, truncation = 0.0;
		for( int m = 0; m < 4; ++m )
			for( int c = 0; c < 3; ++c )
				for( const double d : { 0.5, 0.8, 1.5, 3.0, 6.0 } )
				{
					const plate::Diffusion pd = plate::DiffusionOf( plate::MaterialAt( m ), c );
					const hp::Chan p          = hp::chan( m, c );
					const double peak         = hp::T( p, d, 0.0 );
					for( double r = 0.0; r < 30.0; r += 0.05 )
					{
						worst      = worse( worst, std::fabs( plate::Profile( pd, d, r ) - hp::T( p, d, r, plate::kImagePairs ) ) / peak );
						truncation = worse( truncation, std::fabs( hp::T( p, d, r, plate::kImagePairs ) - hp::T( p, d, r, 80 ) ) / peak );
					}
				}
		Check( worst <= 1e-12 && truncation <= 1e-6,
		       fmt( "the plugin's profile against the harness's direct sum with the same %d image pairs: worst %.1e of the peak (tolerance 1e-12); "
		            "%d pairs against 80: %.1e of the peak (tolerance 1e-6, three orders under the fit)",
		            plate::kImagePairs, worst, plate::kImagePairs, truncation ) );
	}

	//--- the closed-form moments against numerical integration of the plugin's profile
	{
		double worst0 = 0.0, worst2 = 0.0;
		for( int m = 0; m < 4; ++m )
			for( int c = 0; c < 3; ++c )
				for( const double d : { 0.5, 0.8, 1.5, 3.0, 6.0 } )
				{
					const plate::Diffusion pd = plate::DiffusionOf( plate::MaterialAt( m ), c );
					const hp::Chan p          = hp::chan( m, c );
					//Trapezoid in u = ln r, 1e-5 .. 400 mm, 20000 steps.
					const int N     = 20000;
					const double u0 = std::log( 1e-5 ), u1 = std::log( 400.0 ), du = ( u1 - u0 ) / N;
					double n0 = 0.0, n2 = 0.0;
					for( int i = 0; i <= N; ++i )
					{
						const double r = std::exp( u0 + i * du ), wgt = ( i == 0 || i == N ) ? 0.5 : 1.0;
						const double f = plate::Profile( pd, d, r ) * 2.0 * kPi * r * r * du * wgt;
						n0 += f;
						n2 += f * r * r;
					}
					double M0, M2;
					hp::moments( p, d, M0, M2, plate::kImagePairs );
					worst0 = worse( worst0, std::fabs( n0 - M0 ) / M0 );
					worst2 = worse( worst2, std::fabs( n2 - M2 ) / M2 );
				}
		const hp::Chan g = hp::chan( 0, 1 ), r = hp::chan( 0, 0 ), b = hp::chan( 0, 2 );
		const double s08 = std::sqrt( hp::axisVariance( g, 0.8 ) ), s15 = std::sqrt( hp::axisVariance( g, 1.5 ) ),
		             s30 = std::sqrt( hp::axisVariance( g, 3.0 ) ), sr = std::sqrt( hp::axisVariance( r, 0.8 ) ),
		             sb = std::sqrt( hp::axisVariance( b, 0.8 ) );
		const bool spec = std::fabs( s08 - 0.658 ) <= 0.0005 && std::fabs( s15 - 1.008 ) <= 0.0005 && std::fabs( s30 - 1.565 ) <= 0.0005
		                  && std::fabs( sr - 0.695 ) <= 0.0005 && std::fabs( sb - 0.594 ) <= 0.0005;
		bool grows = true;
		for( int m = 0; m < 4; ++m )
			for( int c = 0; c < 3; ++c )
				for( double d = 0.5; d < 5.95; d += 0.05 )
					grows = grows && hp::axisVariance( hp::chan( m, c ), d + 0.05 ) > hp::axisVariance( hp::chan( m, c ), d );
		Check( worst0 <= 1e-5 && worst2 <= 1e-5 && spec && grows,
		       fmt( "the closed-form M0 and M2 against the trapezoid (in ln r, 20000 steps; error under 1e-6) of the plugin's profile: worst %.1e and "
		            "%.1e (tolerance 1e-5); body (a) per-axis spread at 550 nm %.4f / %.4f / %.4f mm through 0.8 / 1.5 / 3 mm, R/B %.4f / %.4f at "
		            "0.8 (the spec: 0.658 / 1.008 / 1.565, 0.695 / 0.594); the spread grows with thickness in every material and channel: %s",
		            worst0, worst2, s08, s15, s30, sr, sb, grows ? "yes" : "NO" ) );
	}

	//--- the fit: the plan's weights (the frame's, renormalised over the scales
	//kept) against the harness's profile, 2-D L1, at every row and midway
	{
		//The spec's measured bounds and the table's midpoint allowance.
		const double bound[ 4 ] = { 0.0052, 0.0011, 0.030, 0.0014 };
		const double midway     = 0.005;
		const int N             = 1200;
		const double u0 = std::log( 1e-4 ), u1 = std::log( 60.0 ), du = ( u1 - u0 ) / N;
		std::vector< double > r( N + 1 ), wr( N + 1 );
		for( int i = 0; i <= N; ++i )
		{
			r[ static_cast< size_t >( i ) ]  = std::exp( u0 + i * du );
			wr[ static_cast< size_t >( i ) ] = 2.0 * kPi * r[ static_cast< size_t >( i ) ] * r[ static_cast< size_t >( i ) ] * du * ( i == 0 || i == N ? 0.5 : 1.0 );
		}
		std::string line;
		bool ok = true;
		double varWorst = 0.0;
		std::string varWhere;
		for( int m = 0; m < 4; ++m )
		{
			//1280 px over 100 mm: no scale under a quarter pixel, so every
			//column of the plan's table is a scale.
			const plate::Plan plan = plate::PlanFor( m, 0.5, 6.0, 100.0, 1280, 720, perturb.kernelModel );
			double worstRow = 0.0, worstMid = 0.0;
			for( int c = 0; c < 3; ++c )
			{
				const hp::Chan p = hp::chan( m, c );
				for( int half = 0; half < 2 * plate::kRows - 1; ++half )
				{
					const int row  = half / 2;
					const bool mid = ( half & 1 ) != 0;
					const double d = 0.5 + 0.05 * row + ( mid ? 0.025 : 0.0 );
					double w[ 16 ] = {};
					for( int j = 0; j < 16; ++j )
					{
						const double lo = plan.table[ ( static_cast< size_t >( row ) * plate::kTableWidth + j ) * 4 + c ];
						const double hi = mid ? plan.table[ ( static_cast< size_t >( row + 1 ) * plate::kTableWidth + j ) * 4 + c ] : lo;
						w[ j ]          = 0.5 * ( lo + hi );
					}
					std::vector< double > t( N + 1 );
					double total = 0.0;
					for( int i = 0; i <= N; ++i )
					{
						t[ static_cast< size_t >( i ) ] = hp::T( p, d, r[ static_cast< size_t >( i ) ] );
						total += t[ static_cast< size_t >( i ) ] * wr[ static_cast< size_t >( i ) ];
					}
					double l1 = 0.0, mixVar = 0.0;
					for( int j = 0; j < 15; ++j )
						mixVar += w[ j ] * hp::scale( j ) * hp::scale( j );
					for( int i = 0; i <= N; ++i )
					{
						const double rr = r[ static_cast< size_t >( i ) ];
						double f        = 0.0;
						for( int j = 0; j < 15; ++j )
						{
							const double s = hp::scale( j );
							f += w[ j ] * std::exp( -0.5 * rr * rr / ( s * s ) ) / ( 2.0 * kPi * s * s );
						}
						l1 += std::fabs( f - t[ static_cast< size_t >( i ) ] / total ) * wr[ static_cast< size_t >( i ) ];
					}
					( mid ? worstMid : worstRow ) = worse( mid ? worstMid : worstRow, l1 );
					const double V = hp::axisVariance( p, d );
					if( std::fabs( mixVar - V ) / V > varWorst )
						varWhere = fmt( "%s, channel %d, %.3f mm%s", hp::kMats[ m ].name, c, d, mid ? " (midway)" : "" );
					varWorst       = worse( varWorst, std::fabs( mixVar - V ) / V );
				}
			}
			ok = ok && worstRow <= bound[ m ] && worstMid <= bound[ m ] + midway;
			line += fmt( "%s%s %.5f / %.5f (%.4f / %.4f)", m ? "; " : "", hp::kMats[ m ].name, worstRow, worstMid, bound[ m ], bound[ m ] + midway );
		}
		Check( ok, "the frame's mixture against the multipole, worst 2-D L1 at the rows / midway (the spec's measured bounds, + 0.005 midway): " + line );
		Check( varWorst <= kFitVariance,
		       fmt( "the mixture's per-axis variance against the profile's, every material, channel, row and midpoint: worst %.2e relative at %s "
		            "(the stated bound %.3f, measured here and used by --blur)",
		            varWorst, varWhere.c_str(), kFitVariance ) );
	}

	//--- the kernels: exact discrete variance
	{
		double worst = 0.0, sum = 0.0;
		int widest   = 0;
		for( double v = 0.0625; v < 16.5; v *= 1.0137 )
		{
			const std::vector< double > g = plate::ExactKernel( v );
			const int R                   = static_cast< int >( g.size() / 2 );
			double s0 = 0.0, s2 = 0.0;
			for( int i = -R; i <= R; ++i )
			{
				s0 += g[ static_cast< size_t >( i + R ) ];
				s2 += i * i * g[ static_cast< size_t >( i + R ) ];
			}
			worst  = worse( worst, std::fabs( s2 / s0 - v ) / v );
			sum    = worse( sum, std::fabs( s0 - 1.0 ) );
			widest = std::max( widest, R );
		}
		Check( worst <= 1e-12 && sum <= 1e-12 && widest <= shaders::kKernelCentre,
		       fmt( "the sampled kernels, variance 0.0625 .. 16.5 px^2: discrete variance off by %.1e relative, sum off 1 by %.1e (tolerance 1e-12); widest "
		            "radius %d (the shader's array holds %d)",
		            worst, sum, widest, shaders::kKernelCentre ) );
	}

	//--- the decimation: B-spline down, the low-res Gaussian, B-spline up
	{
		double worstL1 = 0.0, worstVar = 0.0, worstMean = 0.0;
		for( const int f : { 2, 4, 8, 16, 32 } )
			for( const double ratio : { 2.0, 2.83, 3.99 } )
			{
				const double sigma = ratio * f;
				const std::vector< double > g = plate::ExactKernel( ratio * ratio - 0.5 );
				const int R = static_cast< int >( g.size() / 2 );
				for( int phase = 0; phase < f; ++phase )
				{
					//A unit impulse at pixel x0 = phase; low-res texel b centred on b f + (f - 1)/2.
					const int x0 = phase;
					std::map< int, double > low;
					for( int b = -4; b <= 4; ++b )
					{
						const double cb = b * f + 0.5 * ( f - 1 );
						const double wb = hp::bspline( ( x0 - cb ) / f ) / f;
						if( wb != 0.0 )
							for( int i = -R; i <= R; ++i )
								low[ b + i ] += wb * g[ static_cast< size_t >( i + R ) ];
					}
					std::map< int, double > out;
					for( const auto& e : low )
					{
						const double cb = e.first * f + 0.5 * ( f - 1 );
						for( int x = static_cast< int >( std::floor( cb - 1.5 * f ) ); x <= static_cast< int >( std::ceil( cb + 1.5 * f ) ); ++x )
							out[ x ] += e.second * hp::bspline( ( x - cb ) / f );
					}
					double s0 = 0.0, s1 = 0.0, s2 = 0.0, gs = 0.0;
					for( const auto& e : out )
						gs += std::exp( -0.5 * ( e.first - x0 ) * ( e.first - x0 ) / ( sigma * sigma ) );
					double l1 = 0.0;
					for( int x = x0 - static_cast< int >( 8 * sigma ); x <= x0 + static_cast< int >( 8 * sigma ); ++x )
					{
						const auto it  = out.find( x );
						const double v = it == out.end() ? 0.0 : it->second;
						s0 += v;
						s1 += v * ( x - x0 );
						s2 += v * ( x - x0 ) * ( x - x0 );
						l1 += std::fabs( v - std::exp( -0.5 * ( x - x0 ) * ( x - x0 ) / ( sigma * sigma ) ) / gs );
					}
					worstL1   = worse( worstL1, l1 );
					worstMean = worse( worstMean, std::fabs( s1 / s0 ) );
					worstVar  = worse( worstVar, std::fabs( s2 / s0 - sigma * sigma ) / ( sigma * sigma ) );
				}
			}
		Check( worstL1 <= kSplineL1 && worstMean <= 1e-9 && worstVar <= 1e-9,
		       fmt( "B-spline decimation by 2 .. 32, sigma/f 2 .. 3.99, every phase: L1 from the sampled Gaussian %.6f (the spec's %.4f), centroid off "
		            "%.1e px, variance off %.1e relative (exact: tolerance 1e-9)",
		            worstL1, kSplineL1, worstMean, worstVar ) );
	}

	//--- the plans
	{
		int bad = 0, plans = 0;
		size_t most = 0;
		double sumWorst = 0.0;
		for( int m = 0; m < 4; ++m )
			for( const double plateWidth : { 20.0, 37.0, 100.0, 250.0, 400.0 } )
				for( const int width : { 64, 320, 1280, 1920, 3840 } )
					for( const double thick : { 0.5, 3.0, 6.0 } )
					{
						const plate::Plan plan = plate::PlanFor( m, 0.5, thick, plateWidth, width, width * 9 / 16, 0 );
						++plans;
						most = std::max( most, plan.groups.size() );
						bad += plan.groups.size() > plate::kMaxGroups;
						for( int j = 0; j < plate::kScales; ++j )
						{
							if( !plan.active[ j ] )
								continue;
							const double s = hp::scale( j ) / ( plateWidth / width );
							const int f    = plan.factor[ j ];
							bad += ( f & ( f - 1 ) ) != 0;
							bad += plan.delta[ j ] != ( s < 0.25 );
							if( !plan.delta[ j ] )
								bad += f == 1 ? !( s < 4.0 ) : !( s / f >= 2.0 && s / f < 4.0 );
						}
						for( int row = 0; row < plate::kRows; ++row )
							for( int c = 0; c < 3; ++c )
							{
								double s = 0.0;
								for( int j = 0; j < plate::kTableWidth; ++j )
									s += plan.table[ ( static_cast< size_t >( row ) * plate::kTableWidth + j ) * 4 + c ];
								sumWorst = worse( sumWorst, std::fabs( s - 1.0 ) );
							}
					}
		Check( bad == 0 && sumWorst <= 16 * kFloat,
		       fmt( "%d plans (4 materials, Plate Width 20 .. 400 mm, 64 .. 3840 px, Thickest 0.5 .. 6 mm): %d broken rules (a factor a power of two "
		            "with sigma/f in [2, 4), f = 1 under 4 px, the delta column exactly under a quarter pixel, at most %d groups; most used %zu); every "
		            "row's weights sum to 1 within %.1e (tolerance 16 float roundings)",
		            plans, bad, plate::kMaxGroups, most, sumWorst ) );
	}

	//--- the front reflectance
	{
		double worst = 0.0;
		for( int m = 0; m < 4; ++m )
			for( int c = 0; c < 3; ++c )
				for( const double d : { 0.5, 0.8, 1.5, 3.0, 6.0 } )
				{
					const hp::Chan p = hp::chan( m, c );
					worst            = worse( worst, std::fabs( plate::FrontReflectance( p.K, p.S, d ) - hp::reflectance( p.K, p.S, d ) ) );
				}
		const hp::Chan g = hp::chan( 0, 1 );
		const double r08 = hp::reflectance( g.K, g.S, 0.8 ), r15 = hp::reflectance( g.K, g.S, 1.5 ), r30 = hp::reflectance( g.K, g.S, 3.0 ),
		             rInf = hp::reflectance( g.K, g.S, 60.0 );
		const bool spec = std::fabs( r08 - 0.64 ) <= 0.005 && std::fabs( r15 - 0.72 ) <= 0.005 && std::fabs( r30 - 0.75 ) <= 0.005 && std::fabs( rInf - 0.75 ) <= 0.005;
		Check( worst <= 1e-12 && spec,
		       fmt( "Kubelka-Munk reflectance, the plugin's against the harness's: worst %.1e (tolerance 1e-12); body (a) at 550 nm %.3f / %.3f / %.3f "
		            "through 0.8 / 1.5 / 3 mm, %.3f thick (the spec: 0.64 / 0.72 / 0.75, 0.75)",
		            worst, r08, r15, r30, rInf ) );
	}

	//--- the controls
	{
		LithophanePlugin plugin;
		double worst = 0.0;
		for( const float v : { 0.0f, 0.25f, 0.5f, 1.0f } )
			worst = worse( worst, std::fabs( ThinnestFromParam( v ) - hThin( v ) ), std::fabs( ThickestFromParam( v ) - hThick( v ) ),
			               std::fabs( PlateWidthFromParam( v ) - hPlate( v ) ), std::fabs( BackLightFromParam( v ) - hBack( v ) ),
			               std::fabs( FrontLightFromParam( v ) - hFront( v ) ), std::fabs( LightAngleFromParam( v ) - hAngle( v ) ),
			               std::fabs( LightElevationFromParam( v ) - hElevation( v ) ) );
		const double defaultsOff = worse( std::fabs( hThin( plugin.GetById( PT_THINNEST ) ) - 0.8 ), std::fabs( hThick( plugin.GetById( PT_THICKEST ) ) - 3.0 ),
		                                  std::fabs( hPlate( plugin.GetById( PT_PLATE_WIDTH ) ) - 100.0 ), std::fabs( hAngle( plugin.GetById( PT_LIGHT_ANGLE ) ) - 135.0 ),
		                                  std::fabs( hElevation( plugin.GetById( PT_LIGHT_ELEVATION ) ) - 45.0 ),
		                                  std::fabs( hBack( plugin.GetById( PT_BACK_LIGHT ) ) - kDefaultBackLight ),
		                                  static_cast< double >( plugin.GetById( PT_FRONT_LIGHT ) ), std::fabs( plugin.GetById( PT_MIX ) - 1.0 ),
		                                  static_cast< double >( plugin.GetById( PT_MATERIAL ) + plugin.GetById( PT_MAPPING ) + plugin.GetById( PT_RELIEF_SIDE ) ) );
		Check( worst <= 1e-9 && defaultsOff <= 1e-5,
		       fmt( "the controls' conversions against the spec's ranges: worst %.1e (tolerance 1e-9); the defaults (0.8 mm, 3 mm, Porcelain, Linear "
		            "Depth, Facing Viewer, 100 mm, Back Light %.4f, Front 0, 135 deg, 45 deg, Mix 1) off by %.1e (tolerance 1e-5, a float's 0..1)",
		            worst, kDefaultBackLight, defaultsOff ) );
	}
	return Verdict();
}

//===========================================================================
// --attenuation
//===========================================================================
int runAttenuation( const Perturb& perturb )
{
	std::printf( "\n=== attenuation: flat cards of eight coloured 8-bit codes (so eight known thicknesses), every material, both mappings, Back\n"
	             "    Light 1: each channel's output is exp(-k d); ln(out) against d is a line of slope -k; R > G > B; Linear Light's green is\n"
	             "    linear in Cura's luminance\n" );
	const double codes[ 8 ][ 3 ] = { { 255, 255, 255 }, { 255, 214, 40 }, { 40, 180, 220 }, { 200, 60, 160 },
		                             { 128, 128, 128 }, { 90, 30, 10 },   { 20, 60, 30 },   { 0, 0, 0 } };
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		for( int mapping = 0; mapping < 2; ++mapping )
		{
			std::string line;
			bool ok = true;
			for( int m = 0; m < 4; ++m )
			{
				const hp::Mat& mat = hp::kMats[ m ];
				double d[ 8 ], y[ 8 ][ 3 ], tol[ 8 ][ 3 ], lum[ 8 ];
				double worstResidual = 0.0;
				bool order           = true;
				for( int i = 0; i < 8; ++i )
				{
					const Floats card = flatCard( w, h, code8( codes[ i ][ 0 ] ), code8( codes[ i ][ 1 ] ), code8( codes[ i ][ 2 ] ) );
					Rig rig;
					if( !startRig( rig, w, h, perturb, &card ) )
						return 1;
					rig.Set( PT_MATERIAL, m );
					rig.Set( PT_MAPPING, mapping );
					if( !rig.Render( 1 ) )
						return 1;
					const double thin = hThin( pThin( 0.8 ) ), thick = hThick( pThick( 3.0 ) );
					d[ i ]   = mapping == 0 ? hLumaDepth( codes[ i ], thin, thick ) : hLightDepth( codes[ i ], thin, thick, mat.k[ 1 ] );
					lum[ i ] = hCuraLum( codes[ i ] );
					const Floats out = rig.Output();
					const plate::Plan& plan = rig.plugin.LastPlan();
					const double groups     = static_cast< double >( plan.groups.size() );
					for( int c = 0; c < 3; ++c )
					{
						y[ i ][ c ]       = linAt( out, w, w / 2, h / 2, c );
						const double want = std::exp( -mat.k[ c ] * d[ i ] );
						//The recoded clip moves d by its luma's error times the span.
						const double dd = kRecode * ( thick - thin ) + 2.0 * ulpOf( d[ i ] );
						tol[ i ][ c ]   = kStoreRoundings * kStore + kEncode + accumulation( plan ) + mat.k[ c ] * dd + 4.0 * kFloat
						                + groups * kStoreRoundings * kFlush / want;
						worstResidual   = worse( worstResidual, std::fabs( std::log( y[ i ][ c ] / want ) ) / tol[ i ][ c ] );
					}
					order = order && y[ i ][ 0 ] > y[ i ][ 1 ] && y[ i ][ 1 ] > y[ i ][ 2 ];
				}
				//The fitted slope per channel, and its tolerance from the
				//points' own: |error| <= sum |d_i - mean| tol_i / sum (d_i - mean)^2.
				double slopeWorst = 0.0, slope[ 3 ];
				double mean = 0.0;
				for( double v : d )
					mean += v / 8.0;
				double sxx = 0.0;
				for( double v : d )
					sxx += ( v - mean ) * ( v - mean );
				for( int c = 0; c < 3; ++c )
				{
					double sxy = 0.0, bound = 0.0;
					for( int i = 0; i < 8; ++i )
					{
						sxy += ( d[ i ] - mean ) * std::log( y[ i ][ c ] );
						bound += std::fabs( d[ i ] - mean ) * tol[ i ][ c ];
					}
					slope[ c ]  = sxy / sxx;
					slopeWorst  = worse( slopeWorst, std::fabs( -slope[ c ] - mat.k[ c ] ) / ( bound / sxx ) );
				}
				//Linear Light: the green output against Cura's luminance.
				double lightWorst = 0.0;
				if( mapping == 1 )
				{
					const double thin = hThin( pThin( 0.8 ) ), span = hThick( pThick( 3.0 ) ) - thin;
					const double minT = std::exp( -mat.k[ 1 ] * span );
					for( int i = 0; i < 8; ++i )
					{
						const double want = std::exp( -mat.k[ 1 ] * thin ) * ( minT + ( 1.0 - minT ) * lum[ i ] );
						lightWorst        = worse( lightWorst, std::fabs( y[ i ][ 1 ] - want ) / ( want * tol[ i ][ 1 ] ) );
					}
				}
				ok = ok && worstResidual <= 1.0 && slopeWorst <= 1.0 && order && lightWorst <= 1.0;
				line += fmt( "\n        %-18s slopes %.4f / %.4f / %.4f (k %.3f / %.3f / %.3f), worst error/tolerance: points %.3f, slopes %.3f%s; R > G > B: %s",
				             mat.name, -slope[ 0 ], -slope[ 1 ], -slope[ 2 ], mat.k[ 0 ], mat.k[ 1 ], mat.k[ 2 ], worstResidual, slopeWorst,
				             mapping == 1 ? fmt( ", green against Cura's luminance %.3f", lightWorst ).c_str() : "", order ? "yes" : "NO" );
			}
			Check( ok, fmt( "%4dx%-4d %s (tolerance per point: %d float roundings of 2^-24 stored, the encode 2^-17, the float sums, the recode's "
			                "thickness, and the flush to zero against the value):",
			                w, h, mapping == 0 ? "Linear Depth" : "Linear Light", kStoreRoundings )
			               + line );
		}
	}
	return Verdict();
}

//===========================================================================
// --blur
//===========================================================================
/// One line at a phase: the excess over the blank card, per channel, in linear
/// light, row-averaged; and its measures against the prediction.
struct LineMeasure
{
	double l1[ 3 ], l1Tol[ 3 ], m2[ 3 ], m2Want[ 3 ], m2Tol[ 3 ], sigmaPx[ 3 ], sigmaWantPx[ 3 ], l1Mix[ 3 ], l1MixTol[ 3 ];
};

int runBlur( const Perturb& perturb )
{
	std::printf( "\n=== blur: a one-pixel white line (Thinnest d_l = 0.6, 0.8, 1.125, 1.5, 3 mm) on black (Thickest 6 mm), Porcelain, minus the blank\n"
	             "    card: the excess against the column model's a(d_l) LSF(d_l) - a(6) LSF(6), the LSFs by quadrature of the multipole; its second\n"
	             "    moment (the blur radius, growing with d) and its L1 distance; then its L1 distance from the plugin's own mixture (its table's\n"
	             "    weights, interpolated, through the spec's realisation of each Gaussian); a column at every phase 0..63 at 0.8 mm and at eight\n"
	             "    phases otherwise, a row at eight phases, measured along y\n" );
	//1.125 mm is midway between two rows of the weight table (0.5 + 12.5 x
	//0.05): the GPU's interpolation between rows is measured there.
	const double lines[] = { 0.6, 0.8, 1.125, 1.5, 3.0 };
	//The predictions are physical: computed once, in mm, for every raster.
	struct Pred
	{
		double d;
		std::vector< hp::Lsf > lsf;
	};
	std::vector< Pred > preds;
	const double dBack = hThick( pThick( 6.0 ) );
	for( const double dl : lines )
	{
		Pred p;
		p.d = hThin( pThin( dl ) );
		for( int c = 0; c < 3; ++c )
			p.lsf.emplace_back( 0, c, p.d );
		preds.push_back( std::move( p ) );
	}
	Pred back;
	back.d = dBack;
	for( int c = 0; c < 3; ++c )
		back.lsf.emplace_back( 0, c, dBack );
	const hp::Mat& mat = hp::kMats[ 0 ];
	//Error sources (per unit of a): the spec's fit bound for Porcelain, the
	//table's midway allowance, the B-spline's L1 bound.
	const double fitL1 = 0.0052, tableL1 = 0.005, splineL1 = kSplineL1;

	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double pitch = hPlate( pPlate( 100.0 ) ) / w;
		const Floats blank = flatCard( w, h, 0.0f, 0.0f, 0.0f );
		std::vector< std::vector< double > > kernels( 15 );
		for( int j = 0; j < 15; ++j )
			kernels[ static_cast< size_t >( j ) ] = hp::realised( j, pitch );
		double sigmaAt[ 5 ][ 3 ] = {};
		for( size_t li = 0; li < preds.size(); ++li )
		{
			const Pred& pr = preds[ li ];
			Rig rb;
			if( !startRig( rb, w, h, perturb, &blank ) )
				return 1;
			rb.Set( PT_THINNEST, pThin( lines[ li ] ) );
			rb.Set( PT_THICKEST, pThick( 6.0 ) );
			if( !rb.Render( 1 ) )
				return 1;
			const Floats ob = rb.Output();
			const plate::Plan plan = rb.plugin.LastPlan();
			const double eps       = kStoreRoundings * kStore + kEncode + accumulation( plan );

			//The plugin's own mixture at the line's and the background's
			//thickness: its table's weights (the plugin's data, the one thing
			//read from it), interpolated between rows as the spec says, through
			//the spec's realisation of each scale (the harness's kernels).
			//Against this the GPU's passes are measured tightly; against the
			//multipole above, the physics with the fit's allowance.
			const plate::FitTable& table = plate::TableFor( 0 );
			auto weightsAt = [ & ]( double d, int c ) {
				std::array< double, 15 > wv {};
				const double x = ( d - 0.5 ) / 0.05;
				const int r    = std::clamp( static_cast< int >( std::floor( x ) ), 0, 109 );
				const double t = std::clamp( x - r, 0.0, 1.0 );
				for( int j = 0; j < 15; ++j )
				{
					const double lo = table.weights[ ( static_cast< size_t >( r ) * 15 + static_cast< size_t >( j ) ) * 3 + static_cast< size_t >( c ) ];
					const double hi = table.weights[ ( static_cast< size_t >( r + 1 ) * 15 + static_cast< size_t >( j ) ) * 3 + static_cast< size_t >( c ) ];
					wv[ static_cast< size_t >( j ) ] = lo + t * ( hi - lo );
				}
				return wv;
			};
			const int reach = std::max( w, h );
			std::vector< double > mixL[ 3 ], mixB[ 3 ];
			for( int c = 0; c < 3; ++c )
			{
				const auto wl = weightsAt( pr.d, c ), wb = weightsAt( dBack, c );
				mixL[ c ].assign( static_cast< size_t >( 2 * reach + 1 ), 0.0 );
				mixB[ c ].assign( static_cast< size_t >( 2 * reach + 1 ), 0.0 );
				for( int j = 0; j < 15; ++j )
				{
					const std::vector< double >& k = kernels[ static_cast< size_t >( j ) ];
					const int R                    = static_cast< int >( k.size() / 2 );
					for( int i = std::max( -R, -reach ); i <= std::min( R, reach ); ++i )
					{
						mixL[ c ][ static_cast< size_t >( i + reach ) ] += wl[ static_cast< size_t >( j ) ] * k[ static_cast< size_t >( i + R ) ];
						mixB[ c ][ static_cast< size_t >( i + reach ) ] += wb[ static_cast< size_t >( j ) ] * k[ static_cast< size_t >( i + R ) ];
					}
				}
			}

			std::vector< int > phases;
			if( lines[ li ] == 0.8 )
				for( int p = 0; p < 64; ++p )
					phases.push_back( p );
			else
				phases = { 0, 7, 13, 22, 31, 40, 51, 63 };

			double worstL1 = 0.0, worstM2 = 0.0, worstM2Rel = 0.0, worstM2TolRel = 0.0, worstMix = 0.0, l1Range[ 2 ] = { 1e9, 0.0 }, sigRange[ 2 ] = { 1e9, 0.0 };
			LineMeasure first {}, firstY {};
			//Orientation 0: a column, measured along x (every phase above).
			//Orientation 1: a row, measured along y (the eight phases): the
			//passes along y are as much the plugin as those along x.
			for( int orient = 0; orient < 2; ++orient )
			{
				const std::vector< int > usePhases = orient == 0 ? phases : std::vector< int > { 0, 7, 13, 22, 31, 40, 51, 63 };
				const int n = orient == 0 ? w : h, m = orient == 0 ? h : w;
				//The window: the whole frame but 80 px at each end (an eighth of
				//the height at most along y, so 320x180 keeps a window).
				const int margin = orient == 0 ? 80 : std::min( 80, h / 8 );
				for( const int phase : usePhases )
				{
					const int x0 = n / 2 - 32 + phase;
					Floats card  = blank;
					for( int u = 0; u < m; ++u )
						for( int c = 0; c < 3; ++c )
						{
							const int px = orient == 0 ? x0 : u, py = orient == 0 ? u : x0;
							card[ ( static_cast< size_t >( py ) * w + px ) * 4 + c ] = 1.0f;
						}
					Rig rl;
					if( !startRig( rl, w, h, perturb, &card ) )
						return 1;
					rl.Set( PT_THINNEST, pThin( lines[ li ] ) );
					rl.Set( PT_THICKEST, pThick( 6.0 ) );
					if( !rl.Render( 1 ) )
						return 1;
					const Floats ol = rl.Output();
					LineMeasure lm {};
					const int lo = margin, hi = n - margin;
					for( int c = 0; c < 3; ++c )
					{
						const double al = std::exp( -mat.k[ c ] * pr.d ), ab = std::exp( -mat.k[ c ] * dBack );
						double l1 = 0.0, m2 = 0.0, m2w = 0.0, round1 = 0.0, round2 = 0.0, disc = 0.0, discB = 0.0, outside = 0.0;
						double l1Mix = 0.0, outsideMix = 0.0;
						double e0 = 0.0, e0w = 0.0;
						for( int x = 0; x < n; ++x )
						{
							const double dx = x - x0;
							const double ps = pitch * pr.lsf[ static_cast< size_t >( c ) ]( dx * pitch );
							const double pb = pitch * back.lsf[ static_cast< size_t >( c ) ]( dx * pitch );
							const size_t mi = static_cast< size_t >( x - x0 + reach );
							if( x < lo || x >= hi )
							{
								outside += al * ps + ab * pb;
								outsideMix += al * mixL[ c ][ mi ] + ab * mixB[ c ][ mi ];
								continue;
							}
							double excess = 0.0, sumL = 0.0, sumB = 0.0;
							for( int u = 0; u < m; ++u )
							{
								const int px = orient == 0 ? x : u, py = orient == 0 ? u : x;
								const double vl = linAt( ol, w, px, py, c ), vb = linAt( ob, w, px, py, c );
								excess += vl - vb;
								sumL += vl;
								sumB += vb;
							}
							excess /= m;
							sumL /= m;
							sumB /= m;
							//The pixel-averaged LSF (the line a pixel wide, seen a
							//pixel wide: the LSF through a unit tent of half-width 1 px).
							static const hp::Gauss g16( 16 );
							double pa = 0.0, pab = 0.0;
							for( int side = 0; side < 2; ++side )
								for( size_t i = 0; i < g16.x.size(); ++i )
								{
									const double tau = side == 0 ? 0.5 * ( g16.x[ i ] - 1.0 ) : 0.5 * ( g16.x[ i ] + 1.0 );
									const double wt  = 0.5 * g16.w[ i ] * ( 1.0 - std::fabs( tau ) );
									pa += wt * pitch * pr.lsf[ static_cast< size_t >( c ) ]( ( dx - tau ) * pitch );
									pab += wt * pitch * back.lsf[ static_cast< size_t >( c ) ]( ( dx - tau ) * pitch );
								}
							const double want = al * ps - ab * pb;
							l1 += std::fabs( excess - want );
							l1Mix += std::fabs( excess - ( al * mixL[ c ][ mi ] - ab * mixB[ c ][ mi ] ) );
							m2 += dx * dx * excess;
							m2w += dx * dx * want;
							e0 += excess;
							e0w += want;
							round1 += eps * ( sumL + sumB );
							round2 += eps * dx * dx * ( sumL + sumB );
							disc += std::fabs( ps - pa );
							discB += std::fabs( pb - pab );
						}
						const double Vl = hp::axisVariance( hp::chan( 0, c ), pr.d ) / ( pitch * pitch );
						const double Vb = hp::axisVariance( hp::chan( 0, c ), dBack ) / ( pitch * pitch );
						lm.l1[ c ]    = l1;
						//The decimated scales' spline bound (the whole weight, to
						//be safe), the scales the plan drops under 1e-5 of weight
						//(15 of them at most, each counted twice), rounding, and
						//what falls outside the window.
						lm.l1Mix[ c ]    = l1Mix;
						lm.l1MixTol[ c ] = ( al + ab ) * ( splineL1 + 2.0 * 15.0 * 1e-5 ) + round1 + outsideMix;
						lm.l1Tol[ c ] = al * ( fitL1 + tableL1 + splineL1 + disc ) + ab * ( fitL1 + tableL1 + splineL1 + discB ) + round1 + outside;
						lm.m2[ c ]     = m2;
						lm.m2Want[ c ] = m2w;
						//The fit's variance bound (verified in --model), a sixth of
						//a pixel squared each for sampled against averaged, rounding.
						lm.m2Tol[ c ] = al * ( kFitVariance * Vl + 1.0 / 6.0 ) + ab * ( kFitVariance * Vb + 1.0 / 6.0 ) + round2;
						lm.sigmaPx[ c ]     = std::sqrt( std::max( m2 / std::max( e0, 1e-30 ), 0.0 ) );
						lm.sigmaWantPx[ c ] = std::sqrt( std::max( m2w / std::max( e0w, 1e-30 ), 0.0 ) );
						worstL1         = worse( worstL1, lm.l1[ c ] / lm.l1Tol[ c ] );
						worstMix        = worse( worstMix, lm.l1Mix[ c ] / lm.l1MixTol[ c ] );
						worstM2         = worse( worstM2, std::fabs( lm.m2[ c ] - lm.m2Want[ c ] ) / lm.m2Tol[ c ] );
						worstM2Rel      = worse( worstM2Rel, std::fabs( lm.m2[ c ] - lm.m2Want[ c ] ) / std::fabs( lm.m2Want[ c ] ) );
						worstM2TolRel   = std::max( worstM2TolRel, lm.m2Tol[ c ] / std::fabs( lm.m2Want[ c ] ) );
					}
					if( phase == usePhases.front() )
						( orient == 0 ? first : firstY ) = lm;
					l1Range[ 0 ]  = std::min( l1Range[ 0 ], lm.l1[ 1 ] );
					l1Range[ 1 ]  = std::max( l1Range[ 1 ], lm.l1[ 1 ] );
					sigRange[ 0 ] = std::min( sigRange[ 0 ], lm.sigmaPx[ 1 ] );
					sigRange[ 1 ] = std::max( sigRange[ 1 ], lm.sigmaPx[ 1 ] );
				}
			}
			for( int c = 0; c < 3; ++c )
				sigmaAt[ li ][ c ] = first.sigmaPx[ c ] * pitch;
			const double al = std::exp( -mat.k[ 1 ] * pr.d );
			Check( worstL1 <= 1.0 && worstM2 <= 1.0 && worstMix <= 1.0,
			       fmt( "%4dx%-4d d_l %.3f mm, %zu phases along x and 8 along y, %zu groups, factors to %d: green L1 %.4f (%.4f of a: tolerance %.4f of a), its spread "
			            "over the phases %.4f .. %.4f; R/G/B blur radius of the excess %.3f / %.3f / %.3f mm along x, %.3f / %.3f / %.3f along y (predicted "
			            "%.3f / %.3f / %.3f; the line's own multipole spread %.3f / %.3f / %.3f), green over the phases %.3f .. %.3f px; worst error/tolerance: "
			            "L1 %.3f, second moment %.3f; the second moment off by %.1e relative at worst (tolerance up to %.2f relative: the encode and the float "
			            "sums, bounded pixel by pixel over the window, weighted by distance squared); against the plugin's own mixture green L1 %.6f of a "
			            "(tolerance %.6f of a: the B-spline bound, the dropped scales, rounding), worst error/tolerance %.3f",
			            w, h, pr.d, phases.size(), plan.groups.size(), widestFactor( plan ), first.l1[ 1 ], first.l1[ 1 ] / al, first.l1Tol[ 1 ] / al, l1Range[ 0 ], l1Range[ 1 ],
			            sigmaAt[ li ][ 0 ], sigmaAt[ li ][ 1 ], sigmaAt[ li ][ 2 ], firstY.sigmaPx[ 0 ] * pitch, firstY.sigmaPx[ 1 ] * pitch,
			            firstY.sigmaPx[ 2 ] * pitch, first.sigmaWantPx[ 0 ] * pitch, first.sigmaWantPx[ 1 ] * pitch, first.sigmaWantPx[ 2 ] * pitch,
			            std::sqrt( hp::axisVariance( hp::chan( 0, 0 ), pr.d ) ),
			            std::sqrt( hp::axisVariance( hp::chan( 0, 1 ), pr.d ) ), std::sqrt( hp::axisVariance( hp::chan( 0, 2 ), pr.d ) ), sigRange[ 0 ],
			            sigRange[ 1 ], worstL1, worstM2, worstM2Rel, worstM2TolRel, first.l1Mix[ 1 ] / al, first.l1MixTol[ 1 ] / al, worstMix ) );
		}
		bool grows = true;
		for( int c = 0; c < 3; ++c )
			for( size_t li = 0; li + 1 < preds.size(); ++li )
				grows = grows && sigmaAt[ li + 1 ][ c ] > sigmaAt[ li ][ c ];
		Check( grows, fmt( "%4dx%-4d the measured blur radius grows with thickness in every channel: green %.3f < %.3f < %.3f < %.3f < %.3f mm", w, h,
		                   sigmaAt[ 0 ][ 1 ], sigmaAt[ 1 ][ 1 ], sigmaAt[ 2 ][ 1 ], sigmaAt[ 3 ][ 1 ], sigmaAt[ 4 ][ 1 ] ) );
	}
	return Verdict();
}

//===========================================================================
// --front
//===========================================================================
int runFront( const Perturb& perturb )
{
	std::printf( "\n=== front: Back Light 0, Front Light 1. Equal luma in different colours reflects the same; opposite ramps under a lamp from +x\n"
	             "    at 30 deg shade in Lambert's ratio (sin e - s cos e)/(sin e + s cos e), and along y under a lamp from +y; facing the lamp\n"
	             "    they do not shade; thin reflects less than thick by the Kubelka-Munk ratio\n" );
	//Codes with 299 R + 587 G + 114 B = 128000 exactly: luma 128 in every one.
	std::vector< std::array< int, 3 > > equal;
	for( int r = 0; r <= 255 && equal.size() < 4; r += 1 )
		for( int g = 0; g <= 255; ++g )
		{
			const int rest = 128000 - 299 * r - 587 * g;
			if( rest >= 0 && rest % 114 == 0 && rest / 114 <= 255 && ( equal.empty() || std::abs( r - equal.back()[ 0 ] ) > 50 ) )
			{
				equal.push_back( { r, g, rest / 114 } );
				break;
			}
		}
	equal.push_back( { 128, 128, 128 } );
	const double e = 30.0 * kPi / 180.0;
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double pitch = hPlate( pPlate( 100.0 ) ) / w;
		const double thin = hThin( pThin( 0.8 ) ), thick = hThick( pThick( 3.0 ) ), span = thick - thin;
		auto lit = [ & ]( Rig& r, int relief ) {
			r.Set( PT_BACK_LIGHT, 0.0f );
			r.Set( PT_FRONT_LIGHT, pFront( 1.0 ) );
			r.Set( PT_LIGHT_ANGLE, pAngle( 0.0 ) );
			r.Set( PT_LIGHT_ELEVATION, pElevation( 30.0 ) );
			r.Set( PT_RELIEF_SIDE, relief );
		};
		const double eUsed = hElevation( pElevation( 30.0 ) ) * kPi / 180.0;
		( void )e;

		//(i) equal luma: patches across the frame, measured 8 px inside each.
		{
			const int n = static_cast< int >( equal.size() );
			Floats card( static_cast< size_t >( w ) * h * 4 );
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
				{
					const int k = std::min( n - 1, x * n / w );
					for( int c = 0; c < 3; ++c )
						card[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] = code8( equal[ static_cast< size_t >( k ) ][ static_cast< size_t >( c ) ] );
					card[ ( static_cast< size_t >( y ) * w + x ) * 4 + 3 ] = 1.0f;
				}
			Rig r;
			if( !startRig( r, w, h, perturb, &card ) )
				return 1;
			lit( r, 0 );
			if( !r.Render( 1 ) )
				return 1;
			const Floats out = r.Output();
			double worst     = 0.0;
			for( int c = 0; c < 3; ++c )
			{
				const double ref = linAt( out, w, w / ( 2 * n ), h / 2, c );
				for( int k = 1; k < n; ++k )
				{
					const double v = linAt( out, w, ( 2 * k + 1 ) * w / ( 2 * n ), h / 2, c );
					//The recoded codes move d by kRecode x span; dr/dd is under
					//0.2 /mm in every material here (the harness's difference).
					const hp::Chan p  = hp::chan( 0, c );
					const double dr   = std::fabs( hp::reflectance( p.K, p.S, 1.5 + 1e-4 ) - hp::reflectance( p.K, p.S, 1.5 - 1e-4 ) ) / 2e-4;
					const double tol  = 2.0 * ( dr * kRecode * span + kEncode * ref + 8.0 * kFloat * ref );
					worst             = worse( worst, std::fabs( v - ref ) / tol );
				}
			}
			std::string list;
			for( const auto& q : equal )
				list += fmt( " (%d,%d,%d)", q[ 0 ], q[ 1 ], q[ 2 ] );
			Check( worst <= 1.0, fmt( "%4dx%-4d (i) luma 128 as%s: worst difference/tolerance %.3f (the recode's thickness through dR/dd, the encode)", w, h,
			                          list.c_str(), worst ) );
		}

		//(ii) and (iii): two grey ramps, k codes a pixel; left the thickness
		//rising with x, right falling, the same codes measured in each.
		const int k = std::max( 1, static_cast< int >( std::lround( 0.11 * 255.0 * pitch / span ) ) );
		const double s = k * span / 255.0 / pitch;
		const int len  = 180 / k;
		Floats ramps   = flatCard( w, h, code8( 128 ), code8( 128 ), code8( 128 ) );
		const int leftStart = w / 4 - len / 2, rightStart = 3 * w / 4 - len / 2;
		for( int y = 0; y < h; ++y )
			for( int i = 0; i < len; ++i )
			{
				const float up = code8( 220 - k * i ), down = code8( 220 - k * ( len - 1 - i ) );
				for( int c = 0; c < 3; ++c )
				{
					ramps[ ( static_cast< size_t >( y ) * w + leftStart + i ) * 4 + c ]  = up;
					ramps[ ( static_cast< size_t >( y ) * w + rightStart + i ) * 4 + c ] = down;
				}
			}
		double ratioWorst[ 2 ] = {}, ratioSeen[ 2 ] = {};
		const double want = ( std::sin( eUsed ) - s * std::cos( eUsed ) ) / ( std::sin( eUsed ) + s * std::cos( eUsed ) );
		for( int relief = 0; relief < 2; ++relief )
		{
			Rig r;
			if( !startRig( r, w, h, perturb, &ramps ) )
				return 1;
			lit( r, relief );
			if( !r.Render( 1 ) )
				return 1;
			const Floats out = r.Output();
			for( int i = 2; i < len - 2; i += std::max( 1, len / 12 ) )
			{
				//Code 220 - k i sits at leftStart + i and at rightStart + len - 1 - i.
				for( int c = 0; c < 3; ++c )
				{
					const double a = linAt( out, w, leftStart + i, h / 2, c ), b = linAt( out, w, rightStart + len - 1 - i, h / 2, c );
					const double ratio = a / b;
					const double target = relief == 0 ? want : 1.0;
					//d's float error and the recode move each step; s by
					//their sum over a pixel; the ratio by its slope in s.
					const double dd     = 2.0 * ulpOf( thick ) + kRecode * span;
					const double ds     = dd / pitch;
					const double slope  = relief == 0 ? 2.0 * std::sin( eUsed ) * std::cos( eUsed ) / std::pow( std::sin( eUsed ) + s * std::cos( eUsed ), 2.0 ) : 0.0;
					const double tol    = slope * ds + 2.0 * kEncode + 32.0 * kFloat;
					ratioWorst[ relief ] = worse( ratioWorst[ relief ], std::fabs( ratio - target ) / tol );
					ratioSeen[ relief ]  = ratio;
				}
			}
		}
		Check( ratioWorst[ 0 ] <= 1.0, fmt( "%4dx%-4d (ii) facing the viewer, slope %.4f (%d code%s a pixel): up/down ratio %.6f, Lambert %.6f; worst error/tolerance %.3f",
		                                    w, h, s, k, k == 1 ? "" : "s", ratioSeen[ 0 ], want, ratioWorst[ 0 ] ) );

		//(v) the same ramps along y (GL's y, up), under a lamp from +y (Light
		//Angle 90 deg): the normal's y and the lamp's y, which (ii) cannot see.
		{
			const int lenY      = std::min( len, h / 2 - 8 );
			Floats rampsY       = flatCard( w, h, code8( 128 ), code8( 128 ), code8( 128 ) );
			const int lowStart  = h / 4 - lenY / 2, highStart = 3 * h / 4 - lenY / 2;
			for( int x = 0; x < w; ++x )
				for( int i = 0; i < lenY; ++i )
				{
					const float up = code8( 220 - k * i ), down = code8( 220 - k * ( lenY - 1 - i ) );
					for( int c = 0; c < 3; ++c )
					{
						rampsY[ ( static_cast< size_t >( lowStart + i ) * w + x ) * 4 + c ]  = up;
						rampsY[ ( static_cast< size_t >( highStart + i ) * w + x ) * 4 + c ] = down;
					}
				}
			Rig r;
			if( !startRig( r, w, h, perturb, &rampsY ) )
				return 1;
			lit( r, 0 );
			r.Set( PT_LIGHT_ANGLE, pAngle( 90.0 ) );
			if( !r.Render( 1 ) )
				return 1;
			const Floats out = r.Output();
			//The lamp at 90 deg is (cos e cos 90, cos e sin 90, sin e): its x
			//is cos e times a float's cos( pi / 2 ), 6e-17 in double, below
			//every term here.
			double worstY = 0.0, seenY = 0.0;
			for( int i = 2; i < lenY - 2; i += std::max( 1, lenY / 12 ) )
				for( int c = 0; c < 3; ++c )
				{
					const double a = linAt( out, w, w / 2, lowStart + i, c ), b = linAt( out, w, w / 2, highStart + lenY - 1 - i, c );
					const double dd    = 2.0 * ulpOf( thick ) + kRecode * span;
					const double slope = 2.0 * std::sin( eUsed ) * std::cos( eUsed ) / std::pow( std::sin( eUsed ) + s * std::cos( eUsed ), 2.0 );
					const double tol   = slope * dd / pitch + 2.0 * kEncode + 32.0 * kFloat;
					worstY             = worse( worstY, std::fabs( a / b - want ) / tol );
					seenY              = a / b;
				}
			Check( worstY <= 1.0, fmt( "%4dx%-4d (v) the ramps along y under a lamp at 90 deg: up/down ratio %.6f, Lambert %.6f; worst error/tolerance %.3f", w, h,
			                           seenY, want, worstY ) );
		}
		Check( ratioWorst[ 1 ] <= 1.0, fmt( "%4dx%-4d (iii) facing the lamp: up/down ratio %.6f (no shading: 1); worst error/tolerance %.3f", w, h, ratioSeen[ 1 ],
		                                    ratioWorst[ 1 ] ) );

		//(iv) thin against thick, flat cards: each is F sin(e) R(d), and their ratio.
		{
			double worst = 0.0, seen[ 3 ] = {}, wantRatio[ 3 ] = {};
			Floats out[ 2 ];
			for( int which = 0; which < 2; ++which )
			{
				const Floats card = which == 0 ? flatCard( w, h, 1.0f, 1.0f, 1.0f ) : flatCard( w, h, 0.0f, 0.0f, 0.0f );
				Rig r;
				if( !startRig( r, w, h, perturb, &card ) )
					return 1;
				lit( r, 0 );
				if( !r.Render( 1 ) )
					return 1;
				out[ which ] = r.Output();
			}
			for( int c = 0; c < 3; ++c )
			{
				const hp::Chan p  = hp::chan( 0, c );
				const double rt   = hp::reflectance( p.K, p.S, thin ), rk = hp::reflectance( p.K, p.S, thick );
				const double a    = linAt( out[ 0 ], w, w / 2, h / 2, c ), b = linAt( out[ 1 ], w, w / 2, h / 2, c );
				const double tol  = 2.0 * kEncode + 64.0 * kFloat;
				seen[ c ]         = a / b;
				wantRatio[ c ]    = rt / rk;
				worst             = worse( worst, std::fabs( a / b - rt / rk ) / ( rt / rk ) / tol,
                                   std::fabs( a - hFront( pFront( 1.0 ) ) * std::sin( eUsed ) * rt ) / a / tol );
			}
			Check( worst <= 1.0 && seen[ 1 ] < 1.0,
			       fmt( "%4dx%-4d (iv) thin (%.2f mm) over thick (%.2f mm): R/G/B %.5f / %.5f / %.5f, Kubelka-Munk %.5f / %.5f / %.5f; each card F sin(e) R(d); "
			            "worst error/tolerance %.3f (the encode, float)",
			            w, h, thin, thick, seen[ 0 ], seen[ 1 ], seen[ 2 ], wantRatio[ 0 ], wantRatio[ 1 ], wantRatio[ 2 ], worst ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --edges
//===========================================================================
int runEdges( const Perturb& perturb )
{
	std::printf( "\n=== edges: a flat coloured card renders flat to the frame's edges (the plate continues past the frame); a ramp into each\n"
	             "    edge shades under a front lamp at the edge pixel as inside (one-sided differences there)\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = flatCard( w, h, code8( 200 ), code8( 120 ), code8( 60 ) );
		for( const double plateWidth : { 20.0, 100.0, 400.0 } )
		{
			Rig r;
			if( !startRig( r, w, h, perturb, &card ) )
				return 1;
			r.Set( PT_PLATE_WIDTH, pPlate( plateWidth ) );
			r.Set( PT_THICKEST, pThick( 6.0 ) );
			if( !r.Render( 1 ) )
				return 1;
			const Floats out = r.Output();
			const double tol = kStoreRoundings * kStore + kEncode + accumulation( r.plugin.LastPlan() );
			double worst     = 0.0;
			for( int c = 0; c < 3; ++c )
			{
				const double ref = linAt( out, w, w / 2, h / 2, c );
				for( int y = 0; y < h; ++y )
					for( int x = 0; x < w; ++x )
						worst = worse( worst, std::fabs( linAt( out, w, x, y, c ) - ref ) / ref );
			}
			Check( worst <= tol, fmt( "%4dx%-4d Plate Width %3.0f mm (factors to %d): worst departure from the centre %.2e relative (tolerance %.2e: the "
			                          "stored roundings, the encode, the float sums)",
			                          w, h, plateWidth, widestFactor( r.plugin.LastPlan() ), worst, tol ) );
		}

		//A ramp running into each edge of the frame, under a front lamp along
		//it (Back Light 0, 30 deg): the slope at the edge pixel is a one-sided
		//difference (repousse's), the same slope as the central difference
		//inside, so the shading there is the inside's. Measured as out / R(d)
		//(the Kubelka-Munk factor of the pixel's own thickness divided out),
		//the edge pixel against the ramp's fourth.
		const double pitch = hPlate( pPlate( 100.0 ) ) / w;
		const double thin = hThin( pThin( 0.8 ) ), thick = hThick( pThick( 3.0 ) ), span = thick - thin;
		const int k   = std::max( 1, static_cast< int >( std::lround( 0.3 * 255.0 * pitch / span ) ) );
		const int len = std::min( 24, 200 / k );
		const double s = k * span / 255.0 / pitch;
		const double e = hElevation( pElevation( 30.0 ) ) * kPi / 180.0;
		double worstEdge = 0.0;
		std::string seen;
		for( int edge = 0; edge < 4; ++edge )//left, right, bottom, top
		{
			const bool alongX = edge < 2;
			Floats card       = flatCard( w, h, code8( 128 ), code8( 128 ), code8( 128 ) );
			auto at = [ & ]( int i, int u ) {
				//i: the distance from the edge along the ramp; u: across it.
				const int n = alongX ? w : h;
				const int t = edge % 2 == 0 ? i : n - 1 - i;
				return alongX ? std::make_pair( t, u ) : std::make_pair( u, t );
			};
			const int across = alongX ? h : w;
			for( int u = 0; u < across; ++u )
				for( int i = 0; i < len; ++i )
				{
					const auto [ x, y ] = at( i, u );
					for( int c = 0; c < 3; ++c )
						card[ ( static_cast< size_t >( y ) * w + x ) * 4 + c ] = code8( 220 - k * i );
				}
			Rig r;
			if( !startRig( r, w, h, perturb, &card ) )
				return 1;
			r.Set( PT_BACK_LIGHT, 0.0f );
			r.Set( PT_FRONT_LIGHT, pFront( 1.0 ) );
			r.Set( PT_LIGHT_ANGLE, pAngle( alongX ? 0.0 : 90.0 ) );
			r.Set( PT_LIGHT_ELEVATION, pElevation( 30.0 ) );
			if( !r.Render( 1 ) )
				return 1;
			const Floats out = r.Output();
			//The slope along the lamp: thickness rises away from the left and
			//bottom edges (with x or y), and toward the right and top ones.
			const double sl  = edge % 2 == 0 ? s : -s;
			auto lambert     = [ & ]( double q ) { return ( std::sin( e ) - q * std::cos( e ) ) / std::sqrt( 1.0 + q * q ); };
			const double nl  = lambert( sl );
			const double dnl = std::fabs( lambert( sl + 1e-6 ) - lambert( sl - 1e-6 ) ) / 2e-6;
			for( int c = 0; c < 3; ++c )
			{
				const hp::Chan p = hp::chan( 0, c );
				double q[ 2 ]    = {};
				for( int j = 0; j < 2; ++j )
				{
					const int i         = j == 0 ? 0 : 4;
					const double d      = thin + ( 1.0 - ( 220.0 - k * i ) / 255.0 ) * span;
					const auto [ x, y ] = at( i, across / 2 );
					q[ j ]              = linAt( out, w, x, y, c ) / hp::reflectance( p.K, p.S, d );
				}
				//d's float error and the recode move each slope by dd / pitch;
				//R(d)'s by its own slope; the encode, float.
				const double dd  = 2.0 * ulpOf( thick ) + kRecode * span;
				const double dRr = std::fabs( hp::reflectance( p.K, p.S, 2.0 + 1e-4 ) - hp::reflectance( p.K, p.S, 2.0 - 1e-4 ) ) / 2e-4
				                   / hp::reflectance( p.K, p.S, 2.0 );
				const double tol = 2.0 * dnl * dd / pitch / nl + 2.0 * dRr * dd + 2.0 * kEncode + 32.0 * kFloat;
				worstEdge        = worse( worstEdge, std::fabs( q[ 0 ] / q[ 1 ] - 1.0 ) / tol );
				if( c == 1 )
					seen += fmt( " %s %.6f", edge == 0 ? "left" : edge == 1 ? "right" : edge == 2 ? "bottom" : "top", q[ 0 ] / q[ 1 ] );
			}
		}
		Check( worstEdge <= 1.0, fmt( "%4dx%-4d a ramp of slope %.3f into each edge under a front lamp along it: the edge pixel's shading over the inside's (green)%s "
		                              "(want 1); worst error/tolerance %.3f (d's float error and the recode through the slope and R(d), the encode)",
		                              w, h, s, seen.c_str(), worstEdge ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: output alpha = mix( a, 1, Mix ); white under alpha 0 changes nothing (the thickness from the premultiplied clip);\n"
	             "    Mix 0 hands the clip through; Mix 0.5 mixes premultiplied against the Mix 1 render\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats look = lookCard( w, h );
		Floats white = look, black = look;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w; ++x )
			{
				const size_t p = ( static_cast< size_t >( y ) * w + x ) * 4;
				if( x < w / 2 )
				{
					white[ p ] = white[ p + 1 ] = white[ p + 2 ] = 1.0f;
					black[ p ] = black[ p + 1 ] = black[ p + 2 ] = 0.0f;
					white[ p + 3 ] = black[ p + 3 ] = 0.0f;
				}
				else
					white[ p + 3 ] = black[ p + 3 ] = std::round( 255.0f * ( 2.0f * x / w - 1.0f ) ) / 255.0f;
			}
		Rig rw, rb;
		if( !startRig( rw, w, h, perturb, &white ) || !startRig( rb, w, h, perturb, &black ) )
			return 1;
		for( Rig* r : { &rw, &rb } )
			r->Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
		double worst[ 2 ]      = {};
		size_t shows[ 2 ]      = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		Floats mixed[ 2 ];
		for( int k = 0; k < 2; ++k )
		{
			for( Rig* r : { &rw, &rb } )
			{
				r->Set( PT_MIX, mixes[ k ] );
				if( !r->Render( 1 ) )
					return 1;
			}
			const Floats ow = rw.Output(), ob = rb.Output();
			mixed[ k ]      = ow;
			for( size_t p = 0; p < ow.size() / 4; ++p )
			{
				const double a = white[ 4 * p + 3 ];
				worst[ k ]     = worse( worst[ k ], std::fabs( ow[ 4 * p + 3 ] - ( a + ( 1.0 - a ) * mixes[ k ] ) ) );
			}
			shows[ k ] = differing( ow, ob );
		}
		rw.Set( PT_MIX, 0.0f );
		if( !rw.Render( 1 ) )
			return 1;
		const size_t through = differing( rw.Output(), white );
		double worstMix      = 0.0;
		for( size_t p = 0; p < white.size() / 4; ++p )
		{
			const double a = white[ 4 * p + 3 ];
			for( int c = 0; c < 3; ++c )
			{
				const double want = ( white[ 4 * p + c ] * a * 0.5 + mixed[ 0 ][ 4 * p + c ] * 0.5 ) / ( a * 0.5 + 0.5 );
				worstMix          = worse( worstMix, std::fabs( mixed[ 1 ][ 4 * p + c ] - want ) );
			}
		}
		Check( worst[ 0 ] <= 1e-6 && worst[ 1 ] <= 1e-6 && shows[ 0 ] == 0 && shows[ 1 ] == 0 && through == 0 && worstMix <= 1e-6,
		       fmt( "%4dx%-4d alpha off mix( a, 1, Mix ) by %.2g at Mix 1 and %.2g at Mix 0.5 (tolerance 1e-6, a few float operations on numbers <= 1); "
		            "white rather than black under alpha 0 changes %zu values at Mix 1 and %zu at Mix 0.5 (bit-exact); Mix 0: %zu values differ from "
		            "the clip (bit-exact); Mix 0.5's colour off the premultiplied mix by %.2g (tolerance 1e-6)",
		            w, h, worst[ 0 ], worst[ 1 ], shows[ 0 ], shows[ 1 ], through, worstMix ) );
	}
	return Verdict();
}

//===========================================================================
// --clock and --resize: nothing moves, nothing is kept
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: the same clip at host times from 0 s, from 4.99e8 ms, and jumping back renders the same values, bit for bit\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats card = lookCard( raster.w, raster.h );
		Rig zero, late, back;
		if( !startRig( zero, raster.w, raster.h, perturb, &card ) || !startRig( late, raster.w, raster.h, perturb, &card )
		    || !startRig( back, raster.w, raster.h, perturb, &card ) )
			return 1;
		late.timeOffset = 4.99e8;
		late.timeScale  = 1000.0;
		back.beforeFrame = [ &back ]( int f ) { back.timeOffset = f % 2 == 0 ? 0.0 : -100.0; };
		for( Rig* r : { &zero, &late, &back } )
		{
			r->Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
			r->Set( PT_PLATE_WIDTH, pPlate( 60.0 ) );
		}
		size_t differ = 0, values = 0;
		for( int f = 0; f < 6; ++f )
		{
			if( !zero.Render( 1 ) || !late.Render( 1 ) || !back.Render( 1 ) )
				return 1;
			const Floats a = zero.Output(), b = late.Output(), c = back.Output();
			differ += differing( a, b ) + differing( a, c );
			values += 2 * a.size();
		}
		Check( differ == 0, fmt( "%4dx%-4d six frames each, Front Light on: %zu of %zu values differ (bit-exact)", raster.w, raster.h, differ, values ) );
	}
	return Verdict();
}

int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: render at 1280x720, resize to 320x180, render: the same values as a fresh instance at 320x180 (there is no state)\n" );
	const Floats big = lookCard( 1280, 720 ), small = lookCard( 320, 180 );
	Rig moved, fresh;
	if( !startRig( moved, 1280, 720, perturb, &big ) )
		return 1;
	moved.Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
	if( !moved.Render( 2 ) || !moved.Resize( 320, 180, &small ) || !moved.Render( 1 ) )
		return 1;
	if( !startRig( fresh, 320, 180, perturb, &small ) )
		return 1;
	fresh.Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
	if( !fresh.Render( 1 ) )
		return 1;
	const size_t differ = differing( moved.Output(), fresh.Output() );
	Check( differ == 0, fmt( "resized against fresh, Front Light on: %zu of %zu values differ (bit-exact)", differ, fresh.Output().size() ) );
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
		GLuint vao = 0, buffer = 0, unpack = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		glGenBuffers( 1, &unpack );
		glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack );
		glBufferData( GL_PIXEL_UNPACK_BUFFER, 64, nullptr, GL_STREAM_DRAW );
		glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
		int wrong = 0;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, unpack );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
			glPixelStorei( GL_UNPACK_ROW_LENGTH, 7 );
			glActiveTexture( GL_TEXTURE3 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0, ub = 0, ua = 0, url = 0, fb = 0, prog = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			glGetIntegerv( GL_PIXEL_UNPACK_BUFFER_BINDING, &ub );
			glGetIntegerv( GL_UNPACK_ALIGNMENT, &ua );
			glGetIntegerv( GL_UNPACK_ROW_LENGTH, &url );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fb );
			glGetIntegerv( GL_CURRENT_PROGRAM, &prog );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += ub != static_cast< GLint >( unpack ) || ua != 1 || url != 7;
			wrong += fb != static_cast< GLint >( rig.outputFBO ) || prog != 0;
			wrong += glGetError() != GL_NO_ERROR;
			glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
			glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
			glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
		}
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		glDeleteBuffers( 1, &unpack );
		Check( wrong == 0, fmt( "%4dx%-4d three frames with a host viewport, blend, clear colour, vertex array, buffer, texture unit and pixel-unpack "
		                        "buffer and store of its own: %d things not put back (the host's framebuffer bound, no program), GL errors included",
		                        raster.w, raster.h, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// --robust
//===========================================================================
int runRobust( const Perturb& perturb )
{
	std::printf( "\n=== robust: a clip out of range (-1, 2, 1e30, +-inf and NaN in a float texture, alpha too) is the clip tamed (NaN 0, then\n"
	             "    clamped), bit for bit at Mix 1; every control at its ends gives finite output everywhere\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats wild = lookCard( w, h ), tame;
		const float inf = std::numeric_limits< float >::infinity(), nan = std::numeric_limits< float >::quiet_NaN();
		const float values[] = { -1.0f, 2.0f, 1e30f, inf, -inf, nan, 0.5f, 0.0f, 1.0f };
		for( size_t p = 0; p < wild.size(); p += 4 )
		{
			const uint32_t k = static_cast< uint32_t >( p / 4 );
			if( hash01( k, 3 ) < 0.3 )
				for( int c = 0; c < 4; ++c )
					wild[ p + c ] = values[ pcg( k * 4 + static_cast< uint32_t >( c ) ) % 9 ];
		}
		tame = wild;
		for( float& v : tame )
			v = v != v ? 0.0f : std::clamp( v, 0.0f, 1.0f );
		Rig a, b;
		if( !startRig( a, w, h, perturb, &wild ) || !startRig( b, w, h, perturb, &tame ) )
			return 1;
		for( Rig* r : { &a, &b } )
			r->Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
		if( !a.Render( 1 ) || !b.Render( 1 ) )
			return 1;
		const size_t differ = differing( a.Output(), b.Output() );
		int nonFinite = 0, tried = 0;
		bool passedThrough = false;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			const ControlInfo& info = InfoOf( id );
			for( const float end : { 0.0f, info.kind == Kind::Option ? static_cast< float >( info.optionCount - 1 ) : 1.0f } )
			{
				Rig r;
				if( !startRig( r, w, h, perturb, &wild ) )
					return 1;
				r.Set( PT_FRONT_LIGHT, pFront( 2.0 ) );
				r.Set( id, end );
				if( !r.Render( 1 ) )
					return 1;
				const Floats out = r.Output();
				//Mix 0 hands the clip through untouched, NaN and all: the same
				//bytes, not finite ones.
				if( id == PT_MIX && end == 0.0f )
				{
					passedThrough = std::memcmp( out.data(), wild.data(), wild.size() * sizeof( float ) ) == 0;
					continue;
				}
				for( const float v : out )
					if( !std::isfinite( v ) )
					{
						++nonFinite;
						break;
					}
				++tried;
			}
		}
		Check( differ == 0 && nonFinite == 0 && passedThrough,
		       fmt( "%4dx%-4d the wild clip against the tamed one: %zu values differ (bit-exact); %d renders with one control at an end (Front Light 2): "
		            "%d with any non-finite value; Mix 0 hands the wild clip through byte for byte: %s",
		            w, h, differ, tried, nonFinite, passedThrough ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "model", runModel, true },     { "attenuation", runAttenuation, false }, { "blur", runBlur, false },
		{ "front", runFront, false },    { "edges", runEdges, false },             { "alpha", runAlpha, false },
		{ "clock", runClock, false },    { "resize", runResize, false },           { "glstate", runGLState, false },
		{ "robust", runRobust, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "model", runModel, "every thickness given the 1.5 mm row's kernel", []( Perturb& p ) { p.kernelModel = 1; } );
	add( "attenuation", runAttenuation, "red and blue coefficients swapped", []( Perturb& p ) { p.attenuationModel = 1; } );
	add( "attenuation", runAttenuation, "a linear 1 - k d law", []( Perturb& p ) { p.attenuationModel = 2; } );
	add( "blur", runBlur, "every thickness given the 1.5 mm row's kernel", []( Perturb& p ) { p.kernelModel = 1; } );
	add( "blur", runBlur, "a variance-matched single Gaussian per thickness", []( Perturb& p ) { p.kernelModel = 2; } );
	add( "blur", runBlur, "a box decimation (nearest reconstruction)", []( Perturb& p ) { p.decimationModel = 1; } );
	add( "front", runFront, "the clip's colour let into the front term", []( Perturb& p ) { p.frontModel = 1; } );
	add( "front", runFront, "the normal's sign flipped", []( Perturb& p ) { p.frontModel = 2; } );
	add( "front", runFront, "the relief side ignored", []( Perturb& p ) { p.reliefIgnored = true; } );
	add( "edges", runEdges, "black past the frame's edge", []( Perturb& p ) { p.edgeModel = 1; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.alphaModel = 1; } );
	add( "alpha", runAlpha, "the thickness from the clip's straight colour", []( Perturb& p ) { p.premultiply = false; } );
	add( "clock", runClock, "the light's angle turned by the host's clock", []( Perturb& p ) { p.timeLeak = true; } );
	add( "resize", runResize, "the first raster's pitch kept across a resize", []( Perturb& p ) { p.planKeptOnResize = true; } );
	add( "glstate", runGLState, "the host's GL state not put back", []( Perturb& p ) { p.leakGLState = true; } );
	add( "robust", runRobust, "the clip's NaN and range not tamed", []( Perturb& p ) { p.clampOn = false; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
		int warm, timed;
		bool widest;
	};
	const Size sizes[] = {
		{ 1280, 720, "720p", 20, 60, false }, { 1920, 1080, "1080p", 20, 60, false }, { 3840, 2160, "4K", 5, 10, false },
		{ 1280, 720, "720p", 10, 30, true },  { 1920, 1080, "1080p", 10, 30, true },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) on the card: the defaults, then the most groups (Plate Width 20 mm, Thickest 6 mm,\n"
	             "    Front Light on)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Size& size : sizes )
	{
		Rig rig;
		const Floats card = lookCard( size.w, size.h );
		if( !rig.Init( size.w, size.h, &card ) )
			return 1;
		if( size.widest )
		{
			rig.Set( PT_PLATE_WIDTH, pPlate( 20.0 ) );
			rig.Set( PT_THICKEST, pThick( 6.0 ) );
			rig.Set( PT_FRONT_LIGHT, pFront( 0.5 ) );
		}
		if( !rig.Render( size.warm ) )
			return 1;
		std::vector< double > gpu;
		for( int f = 0; f < size.timed; ++f )
		{
			glBeginQuery( GL_TIME_ELAPSED, query );
			if( !rig.Render( 1 ) )
				return 1;
			glEndQuery( GL_TIME_ELAPSED );
			GLuint64 ns = 0;
			glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
			gpu.push_back( static_cast< double >( ns ) * 1e-6 );
		}
		std::sort( gpu.begin(), gpu.end() );
		const plate::Plan& plan = rig.plugin.LastPlan();
		std::printf( "  %-6s %-8s %zu groups, factors to %3d  GPU median %6.2f ms, worst %6.2f (%5.1f%% of a 60 fps frame)\n", size.name,
		             size.widest ? "widest" : "defaults", plan.groups.size(), widestFactor( plan ), gpu[ gpu.size() / 2 ], gpu.back(),
		             100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ) );
	}
	glDeleteQueries( 1, &query );
	return 0;
}
