#include "Plate.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace lithophane::plate
{
namespace
{
constexpr double kPi = 3.14159265358979323846;
/// The weight of the fit's two constraint rows (total, variance) against its
/// normal equations, whose entries run 1e-3 .. 20. Chosen: 0.01 already holds
/// the variance to 1e-3; 100 holds it to 1e-4 and changes the L1 distance by
/// under 0.001 (measured, every material).
constexpr double kHold = 100.0;

/// Donner & Jensen's polynomial fit of the diffuse Fresnel reflectance, for
/// eta > 1 (their eq. under 7): PLA at Ragunathan et al.'s n = 1.4.
double diffuseFresnel( double eta )
{
	return -1.4399 / ( eta * eta ) + 0.7099 / eta + 0.6681 + 0.0636 * eta;
}

/// sigma_a from the measured apparent coefficient k: the multipole's
/// thick-slab decay sqrt( 3 mua ( mua + musp ) ) set equal to k.
double absorptionFromApparent( double k, double musp )
{
	return 0.5 * ( -musp + std::sqrt( musp * musp + 4.0 * k * k / 3.0 ) );
}

Material makePla()
{
	//Wang & Zhou 2023, Fig. 6: the two-point slope between 0.8 and 2.4 mm.
	//Ragunathan et al.'s reduced scattering extrapolated to pure white,
	//3.75 /mm at 635 nm, used for all three channels (chosen). K = 2 mua and
	//S = 3/4 musp (Pham et al.): derived, for the front reflectance.
	Material m { "White PLA", { 0.293, 0.331, 0.429 }, {}, {}, { 3.75, 3.75, 3.75 }, diffuseFresnel( 1.4 ) };
	for( int c = 0; c < 3; ++c )
	{
		m.K[ c ] = 2.0 * absorptionFromApparent( m.k[ c ], m.musp[ c ] );
		m.S[ c ] = 0.75 * m.musp[ c ];
	}
	return m;
}

Material porcelain( const char* name, std::array< double, 3 > k, std::array< double, 3 > K, std::array< double, 3 > S )
{
	//musp = 4 S / 3 (Pham et al.); F_dr = 0.5, Sugiyama's (after Hogg) inside
	//diffuse boundary reflection.
	Material m { name, { k[ 0 ], k[ 1 ], k[ 2 ] }, { K[ 0 ], K[ 1 ], K[ 2 ] }, { S[ 0 ], S[ 1 ], S[ 2 ] }, {}, 0.5 };
	for( int c = 0; c < 3; ++c )
		m.musp[ c ] = 4.0 * S[ c ] / 3.0;
	return m;
}

const std::array< Material, 4 >& materials()
{
	//Sugiyama et al. 1996: k from Fig. 5, K from Fig. 6, S from Fig. 7, at
	//650 / 550 / 450 nm, digitised from the 300 dpi page images.
	static const std::array< Material, 4 > table = {
		porcelain( "Porcelain", { 0.90, 1.01, 1.42 }, { 0.064, 0.064, 0.101 }, { 3.74, 4.73, 6.43 } ),
		porcelain( "Feldspar Porcelain", { 0.58, 0.59, 0.75 }, { 0.022, 0.011, 0.012 }, { 2.80, 3.40, 4.35 } ),
		porcelain( "Clay Porcelain", { 1.49, 2.05, 2.95 }, { 0.193, 0.300, 0.481 }, { 4.74, 6.00, 8.51 } ),
		makePla(),
	};
	return table;
}

/// Solve A x = b by Gaussian elimination with partial pivoting (n small).
bool solve( std::vector< double > A, std::vector< double > b, int n, std::vector< double >& x )
{
	for( int col = 0; col < n; ++col )
	{
		int pivot = col;
		for( int r = col + 1; r < n; ++r )
			if( std::fabs( A[ r * n + col ] ) > std::fabs( A[ pivot * n + col ] ) )
				pivot = r;
		if( A[ pivot * n + col ] == 0.0 )
			return false;
		if( pivot != col )
		{
			for( int k = 0; k < n; ++k )
				std::swap( A[ col * n + k ], A[ pivot * n + k ] );
			std::swap( b[ col ], b[ pivot ] );
		}
		for( int r = col + 1; r < n; ++r )
		{
			const double f = A[ r * n + col ] / A[ col * n + col ];
			for( int k = col; k < n; ++k )
				A[ r * n + k ] -= f * A[ col * n + k ];
			b[ r ] -= f * b[ col ];
		}
	}
	x.assign( n, 0.0 );
	for( int r = n - 1; r >= 0; --r )
	{
		double s = b[ r ];
		for( int k = r + 1; k < n; ++k )
			s -= A[ r * n + k ] * x[ k ];
		x[ r ] = s / A[ r * n + r ];
	}
	return true;
}

FitTable fit( const Material& m )
{
	//The radial grid: log-spaced, 1 um to 100 mm (PLA's tail, e^(-0.29 r),
	//still carries a percent of its variance past 40 mm), the weights
	//2 pi r dr with dr = r du.
	constexpr int N   = 500;
	const double u0   = std::log( 1e-3 ), u1 = std::log( 100.0 );
	const double du   = ( u1 - u0 ) / ( N - 1 );
	std::vector< double > r( N ), W( N );
	for( int i = 0; i < N; ++i )
	{
		r[ i ] = std::exp( u0 + i * du );
		W[ i ] = 2.0 * kPi * r[ i ] * r[ i ] * du * ( i == 0 || i == N - 1 ? 0.5 : 1.0 );
	}
	std::vector< double > B( static_cast< size_t >( N ) * kScales );
	for( int j = 0; j < kScales; ++j )
	{
		const double s = ScaleSigmaMm( j );
		for( int i = 0; i < N; ++i )
			B[ static_cast< size_t >( i ) * kScales + j ] = std::exp( -0.5 * r[ i ] * r[ i ] / ( s * s ) ) / ( 2.0 * kPi * s * s );
	}
	std::vector< double > G( kScales * kScales, 0.0 );
	for( int a = 0; a < kScales; ++a )
		for( int b = 0; b < kScales; ++b )
		{
			double s = 0.0;
			for( int i = 0; i < N; ++i )
				s += W[ i ] * B[ static_cast< size_t >( i ) * kScales + a ] * B[ static_cast< size_t >( i ) * kScales + b ];
			G[ a * kScales + b ] = s;
		}

	FitTable table;
	table.weights.assign( static_cast< size_t >( kRows ) * kScales * 3, 0.0 );
	std::vector< double > T( N ), h( kScales );
	for( int c = 0; c < 3; ++c )
	{
		const Diffusion p = DiffusionOf( m, c );
		for( int row = 0; row < kRows; ++row )
		{
			const double d = kRowOrigin + kRowStep * row;
			double total   = 0.0;
			for( int i = 0; i < N; ++i )
			{
				T[ i ] = Profile( p, d, r[ i ] );
				total += W[ i ] * T[ i ];
			}
			for( int i = 0; i < N; ++i )
				T[ i ] /= total;
			for( int j = 0; j < kScales; ++j )
			{
				double s = 0.0;
				for( int i = 0; i < N; ++i )
					s += W[ i ] * B[ static_cast< size_t >( i ) * kScales + j ] * T[ i ];
				h[ j ] = s;
			}
			//The total and the per-axis variance held to the profile's: two
			//rows added to the normal equations with weight kHold, which holds
			//the variance to 1e-4 (measured) and the blur radius with it.
			const double V = AxisVariance( p, d );
			std::vector< double > Gc = G, hc = h;
			for( int a = 0; a < kScales; ++a )
			{
				const double ca = ScaleSigmaMm( a ) * ScaleSigmaMm( a ) / V;
				for( int b = 0; b < kScales; ++b )
					Gc[ a * kScales + b ] += kHold * ( ca * ScaleSigmaMm( b ) * ScaleSigmaMm( b ) / V + 1.0 );
				hc[ a ] += kHold * ( ca + 1.0 );
			}
			std::vector< double > w = Nnls( Gc, hc, kScales );
			double sum = 0.0;
			for( double v : w )
				sum += v;
			if( !( sum > 0.0 ) )
			{
				w.assign( kScales, 0.0 );
				w[ 0 ] = sum = 1.0;
			}
			for( double& v : w )
				v /= sum;
			double l1 = 0.0;
			for( int i = 0; i < N; ++i )
			{
				double f = 0.0;
				for( int j = 0; j < kScales; ++j )
					f += w[ j ] * B[ static_cast< size_t >( i ) * kScales + j ];
				l1 += W[ i ] * std::fabs( f - T[ i ] );
			}
			if( l1 > table.worstL1 )
			{
				table.worstL1       = l1;
				table.worstL1At[ 0 ] = d;
				table.worstL1At[ 1 ] = c;
			}
			for( int j = 0; j < kScales; ++j )
				table.weights[ ( static_cast< size_t >( row ) * kScales + j ) * 3 + c ] = w[ j ];
		}
	}
	return table;
}
} // namespace

double ScaleSigmaMm( int j )
{
	return kScale0 * std::pow( 2.0, 0.5 * j );
}

const Material& MaterialAt( int index )
{
	return materials()[ static_cast< size_t >( std::clamp( index, 0, 3 ) ) ];
}

Diffusion DiffusionOf( const Material& m, int c )
{
	Diffusion p;
	p.musp        = m.musp[ c ];
	p.mua         = absorptionFromApparent( m.k[ c ], p.musp );
	const double t = p.mua + p.musp;
	const double D = 1.0 / ( 3.0 * t );
	const double A = ( 1.0 + m.Fdr ) / ( 1.0 - m.Fdr );
	p.zb           = 2.0 * A * D;
	p.ell          = 1.0 / t;
	p.alpha        = p.musp / t;
	p.sigtr        = std::sqrt( 3.0 * p.mua * t );
	return p;
}

double Profile( const Diffusion& p, double d, double r )
{
	double v = 0.0;
	for( int i = -kImagePairs; i <= kImagePairs; ++i )
	{
		const double zr = 2.0 * i * ( d + 2.0 * p.zb ) + p.ell;
		const double zv = 2.0 * i * ( d + 2.0 * p.zb ) - p.ell - 2.0 * p.zb;
		for( int s = 0; s < 2; ++s )
		{
			const double h   = d - ( s == 0 ? zr : zv );
			const double rho = std::sqrt( r * r + h * h );
			const double t   = h * ( 1.0 + p.sigtr * rho ) * std::exp( -p.sigtr * rho ) / ( 4.0 * kPi * rho * rho * rho );
			v += ( s == 0 ? 1.0 : -1.0 ) * p.alpha * t;
		}
	}
	return v;
}

double AxisVariance( const Diffusion& p, double d )
{
	//Per source at signed axial distance h: int f 2 pi r dr = alpha sgn(h)
	//e^(-s|h|) / 2 and int r^2 f 2 pi r dr = alpha h e^(-s|h|) / s
	//(d/drho of -e^(-s rho)/rho is e^(-s rho)(1 + s rho)/rho^2). The same
	//image pairs as Profile.
	double M0 = 0.0, M2 = 0.0;
	for( int i = -kImagePairs; i <= kImagePairs; ++i )
	{
		const double zr = 2.0 * i * ( d + 2.0 * p.zb ) + p.ell;
		const double zv = 2.0 * i * ( d + 2.0 * p.zb ) - p.ell - 2.0 * p.zb;
		for( int s = 0; s < 2; ++s )
		{
			const double h  = d - ( s == 0 ? zr : zv );
			const double sg = s == 0 ? 1.0 : -1.0;
			const double e  = std::exp( -p.sigtr * std::fabs( h ) );
			M0 += sg * p.alpha * 0.5 * ( h > 0.0 ? 1.0 : -1.0 ) * e;
			M2 += sg * p.alpha * h * e / p.sigtr;
		}
	}
	return M2 / ( 2.0 * M0 );
}

const FitTable& TableFor( int material )
{
	static std::once_flag once[ 4 ];
	static FitTable tables[ 4 ];
	const int m = std::clamp( material, 0, 3 );
	std::call_once( once[ m ], [ m ]() { tables[ m ] = fit( MaterialAt( m ) ); } );
	return tables[ m ];
}

std::vector< double > Nnls( const std::vector< double >& G, const std::vector< double >& h, int n )
{
	std::vector< double > w( n, 0.0 ), z;
	std::vector< bool > passive( n, false );
	double scale = 0.0;
	for( double v : h )
		scale = std::max( scale, std::fabs( v ) );
	const double tol = 1e-12 * std::max( scale, 1e-300 );
	for( int outer = 0; outer < 3 * n; ++outer )
	{
		int t       = -1;
		double best = tol;
		for( int j = 0; j < n; ++j )
		{
			if( passive[ j ] )
				continue;
			double g = h[ j ];
			for( int k = 0; k < n; ++k )
				g -= G[ j * n + k ] * w[ k ];
			if( g > best )
			{
				best = g;
				t    = j;
			}
		}
		if( t < 0 )
			break;
		passive[ t ] = true;
		for( int inner = 0; inner < 3 * n; ++inner )
		{
			std::vector< int > P;
			for( int j = 0; j < n; ++j )
				if( passive[ j ] )
					P.push_back( j );
			const int np = static_cast< int >( P.size() );
			std::vector< double > A( static_cast< size_t >( np ) * np ), b( np ), x;
			for( int a = 0; a < np; ++a )
			{
				b[ a ] = h[ P[ a ] ];
				for( int c = 0; c < np; ++c )
					A[ a * np + c ] = G[ P[ a ] * n + P[ c ] ];
			}
			if( !solve( A, b, np, x ) )
			{
				passive[ t ] = false;
				break;
			}
			z.assign( n, 0.0 );
			bool positive = true;
			for( int a = 0; a < np; ++a )
			{
				z[ P[ a ] ] = x[ a ];
				positive    = positive && x[ a ] > 0.0;
			}
			if( positive )
			{
				w = z;
				break;
			}
			double alpha = 1.0;
			for( int j : P )
				if( z[ j ] <= 0.0 )
					alpha = std::min( alpha, w[ j ] / ( w[ j ] - z[ j ] ) );
			for( int j = 0; j < n; ++j )
				w[ j ] += alpha * ( z[ j ] - w[ j ] );
			for( int j : P )
				if( w[ j ] <= 1e-15 )
				{
					passive[ j ] = false;
					w[ j ]       = 0.0;
				}
		}
	}
	return w;
}

std::vector< double > ExactKernel( double variance )
{
	if( !( variance > 0.0 ) )
		return { 1.0 };
	//The radius first, from the target (ceil( 4 sigma )): with it fixed the
	//truncated kernel's variance is continuous and increasing in s, so the
	//bisection reaches the target (letting the radius follow s makes the
	//variance jump where ceil( 4 s ) steps, and a target in a jump is missed).
	const int R = std::max( 1, static_cast< int >( std::ceil( 4.0 * std::sqrt( variance ) ) ) );
	auto varianceOf = [ R ]( double s, std::vector< double >* out ) {
		double sum = 0.0, second = 0.0;
		std::vector< double > g( 2 * R + 1 );
		for( int i = -R; i <= R; ++i )
		{
			g[ i + R ] = std::exp( -0.5 * ( i * i ) / ( s * s ) );
			sum += g[ i + R ];
			second += i * i * g[ i + R ];
		}
		if( out )
		{
			for( double& v : g )
				v /= sum;
			*out = g;
		}
		return second / sum;
	};
	double lo = 1e-3, hi = 4.0 * std::sqrt( variance ) + 2.0;
	for( int i = 0; i < 200 && hi - lo > 1e-15 * hi; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( varianceOf( mid, nullptr ) < variance ? lo : hi ) = mid;
	}
	std::vector< double > g;
	varianceOf( 0.5 * ( lo + hi ), &g );
	return g;
}

Plan PlanFor( int material, double dThin, double dThick, double plateWidthMm, int width, int height, int kernelModel )
{
	Plan plan;
	plan.material = std::clamp( material, 0, 3 );
	plan.dThin    = std::clamp( dThin, kRowOrigin, kRowOrigin + kRowStep * ( kRows - 1 ) );
	plan.dThick   = std::clamp( std::max( dThick, dThin ), plan.dThin, kRowOrigin + kRowStep * ( kRows - 1 ) );
	plan.pitch    = plateWidthMm / std::max( width, 1 );
	const FitTable& fitted = TableFor( plan.material );

	//The rows the frame can read (interpolation reads floor and floor + 1).
	const int r0 = std::clamp( static_cast< int >( std::floor( ( plan.dThin - kRowOrigin ) / kRowStep ) ), 0, kRows - 1 );
	const int r1 = std::clamp( static_cast< int >( std::floor( ( plan.dThick - kRowOrigin ) / kRowStep ) ) + 1, 0, kRows - 1 );

	//The weights this frame uses: the fit's, or a negative control's.
	auto weightAt = [ & ]( int row, int j, int c ) -> double {
		if( kernelModel == 1 )
			row = static_cast< int >( std::lround( ( 1.5 - kRowOrigin ) / kRowStep ) );
		if( kernelModel == 2 )
		{
			//One Gaussian of the profile's variance, as the two adjacent scales
			//whose mixture has that variance.
			double V = 0.0;
			for( int s = 0; s < kScales; ++s )
				V += fitted.weights[ ( static_cast< size_t >( row ) * kScales + s ) * 3 + c ] * ScaleSigmaMm( s ) * ScaleSigmaMm( s );
			for( int s = 0; s + 1 < kScales; ++s )
			{
				const double a = ScaleSigmaMm( s ) * ScaleSigmaMm( s ), b = ScaleSigmaMm( s + 1 ) * ScaleSigmaMm( s + 1 );
				if( V >= a && V <= b )
				{
					const double f = ( V - a ) / ( b - a );
					return j == s ? 1.0 - f : j == s + 1 ? f : 0.0;
				}
			}
			return j == 0 ? 1.0 : 0.0;
		}
		return fitted.weights[ ( static_cast< size_t >( row ) * kScales + j ) * 3 + c ];
	};

	for( int j = 0; j < kScales; ++j )
	{
		plan.sigmaPx[ j ] = ScaleSigmaMm( j ) / plan.pitch;
		double most       = 0.0;
		for( int row = r0; row <= r1; ++row )
			for( int c = 0; c < 3; ++c )
				most = std::max( most, weightAt( row, j, c ) );
		plan.active[ j ] = most >= kActiveWeight;
		plan.delta[ j ]  = plan.active[ j ] && plan.sigmaPx[ j ] < kDeltaBelowPx;
		int f            = 1;
		if( plan.sigmaPx[ j ] >= 4.0 )
			f = 1 << static_cast< int >( std::floor( std::log2( plan.sigmaPx[ j ] / 2.0 ) ) );
		plan.factor[ j ] = f;
	}

	//The table as uploaded.
	plan.table.assign( static_cast< size_t >( kTableWidth ) * kRows * 4, 0.0f );
	for( int row = 0; row < kRows; ++row )
		for( int c = 0; c < 3; ++c )
		{
			double kept = 0.0;
			for( int j = 0; j < kScales; ++j )
				if( plan.active[ j ] )
					kept += weightAt( row, j, c );
			for( int j = 0; j < kScales; ++j )
			{
				if( !plan.active[ j ] || !( kept > 0.0 ) )
					continue;
				const int column = plan.delta[ j ] ? kDeltaColumn : j;
				plan.table[ ( static_cast< size_t >( row ) * kTableWidth + column ) * 4 + c ] += static_cast< float >( weightAt( row, j, c ) / kept );
			}
			if( !( kept > 0.0 ) )
				plan.table[ ( static_cast< size_t >( row ) * kTableWidth + kDeltaColumn ) * 4 + c ] = 1.0f;
		}

	//The groups, by factor, four scales at most each, and their kernels.
	std::vector< std::vector< double > > kernels;
	for( int f = 1; f <= ( 1 << 20 ); f <<= 1 )
	{
		Group g;
		for( int j = 0; j < kScales; ++j )
		{
			if( !plan.active[ j ] || plan.delta[ j ] || plan.factor[ j ] != f )
				continue;
			if( g.count == kMaxGroupScales )
			{
				plan.groups.push_back( g );
				g = Group();
			}
			g.factor = f;
			const double variance = f == 1 ? plan.sigmaPx[ j ] * plan.sigmaPx[ j ]
			                               : plan.sigmaPx[ j ] * plan.sigmaPx[ j ] / ( double( f ) * f ) - 0.5;
			kernels.push_back( ExactKernel( variance ) );
			g.scale[ g.count ]     = j;
			g.radius[ g.count ]    = static_cast< int >( kernels.back().size() / 2 );
			g.kernelRow[ g.count ] = static_cast< int >( kernels.size() ) - 1;
			++g.count;
		}
		if( g.count > 0 )
			plan.groups.push_back( g );
		if( f > 4 * std::max( width, height ) )
			break;
	}
	for( Group& g : plan.groups )
	{
		g.lowWidth  = g.factor == 1 ? width : ( width + g.factor - 1 ) / g.factor + 4;
		g.lowHeight = g.factor == 1 ? height : ( height + g.factor - 1 ) / g.factor + 4;
	}

	plan.kernelRadius = 0;
	for( const auto& k : kernels )
		plan.kernelRadius = std::max( plan.kernelRadius, static_cast< int >( k.size() / 2 ) );
	plan.kernelRows  = std::max( 1, static_cast< int >( kernels.size() ) );
	const int stride = 2 * plan.kernelRadius + 1;
	plan.kernels.assign( static_cast< size_t >( stride ) * plan.kernelRows * 4, 0.0f );
	for( size_t k = 0; k < kernels.size(); ++k )
	{
		const int R = static_cast< int >( kernels[ k ].size() / 2 );
		for( int i = -R; i <= R; ++i )
			plan.kernels[ ( k * stride + static_cast< size_t >( i + plan.kernelRadius ) ) * 4 ] = static_cast< float >( kernels[ k ][ i + R ] );
	}
	return plan;
}

double FrontReflectance( double K, double S, double d )
{
	const double kappa = std::sqrt( K * ( K + 2.0 * S ) );
	const double R     = S / ( K + S + kappa / std::tanh( kappa * d ) );
	const double r1 = 0.06, r2 = 0.5;
	return r1 + ( 1.0 - r1 ) * ( 1.0 - r2 ) * R / ( 1.0 - r2 * R );
}

} // namespace lithophane::plate
