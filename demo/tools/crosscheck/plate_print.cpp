// The plugin's CPU half, printed: the half of demo/tools/crosscheck/port_numbers.py
// that is the C++. Built by port_numbers.py on source/Plate.cpp and
// source/Controls.cpp as they stand, with the plugin's own flags (C++17, -O3,
// Apple clang's default contraction), and compared line for line with
// plate_print.mjs, which prints the same from demo/plate.js.
//
//     plate_print CASES     one case a line: material thinnest thickest width W H
//                           (the host's 0..1 as decimal floats)
//
// Each line is a key and a value: `d` a double (%.17g), `f` a float's bits
// (hex), `i` an integer.
#include "Controls.h"
#include "Plate.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace lithophane;

namespace
{
uint32_t bits( float v )
{
	uint32_t b;
	std::memcpy( &b, &v, sizeof b );
	return b;
}
void d( const std::string& key, double v )
{
	std::printf( "%s d %.17g\n", key.c_str(), v );
}
void f( const std::string& key, float v )
{
	std::printf( "%s f %08x\n", key.c_str(), bits( v ) );
}
void i( const std::string& key, long v )
{
	std::printf( "%s i %ld\n", key.c_str(), v );
}
} // namespace

int main( int argc, char** argv )
{
	if( argc != 2 )
	{
		std::fprintf( stderr, "usage: plate_print CASES\n" );
		return 2;
	}

	// The controls: every default, as the host's float.
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		std::string key = "control." + std::to_string( id );
		std::printf( "%s.name s %s\n", key.c_str(), info.name );
		std::printf( "%s.group s %s\n", key.c_str(), info.group );
		i( key + ".kind", info.kind == Kind::Option ? 1 : 0 );
		f( key + ".default", info.defaultValue );
		i( key + ".options", info.optionCount );
		for( int e = 0; e < info.optionCount; ++e )
			std::printf( "%s.option.%d s %s\n", key.c_str(), e, info.options[ e ] );
	}

	// Every conversion, on a grid of host values and past both ends.
	for( int n = -2; n <= 1002; ++n )
	{
		const float v = static_cast< float >( n ) / 1000.0f;
		const std::string key = "conv." + std::to_string( n );
		d( key + ".thinnest", ThinnestFromParam( v ) );
		d( key + ".thickest", ThickestFromParam( v ) );
		d( key + ".width", PlateWidthFromParam( v ) );
		d( key + ".back", BackLightFromParam( v ) );
		d( key + ".front", FrontLightFromParam( v ) );
		d( key + ".angle", LightAngleFromParam( v ) );
		d( key + ".elevation", LightElevationFromParam( v ) );
		i( key + ".material", MaterialFromParam( v * 4.0f ) );
		i( key + ".mapping", MappingFromParam( v * 2.0f ) );
		i( key + ".relief", ReliefSideFromParam( v * 2.0f ) );
	}

	// The materials and the multipole's parameters.
	for( int m = 0; m < 4; ++m )
	{
		const plate::Material& mat = plate::MaterialAt( m );
		const std::string key = "material." + std::to_string( m );
		std::printf( "%s.name s %s\n", key.c_str(), mat.name );
		d( key + ".Fdr", mat.Fdr );
		for( int c = 0; c < 3; ++c )
		{
			const std::string ck = key + "." + std::to_string( c );
			d( ck + ".k", mat.k[ c ] );
			d( ck + ".K", mat.K[ c ] );
			d( ck + ".S", mat.S[ c ] );
			d( ck + ".musp", mat.musp[ c ] );
			const plate::Diffusion p = plate::DiffusionOf( mat, c );
			d( ck + ".mua", p.mua );
			d( ck + ".zb", p.zb );
			d( ck + ".ell", p.ell );
			d( ck + ".alpha", p.alpha );
			d( ck + ".sigtr", p.sigtr );
			for( double th : { 0.5, 0.8, 1.125, 3.0, 6.0 } )
			{
				d( ck + ".variance." + std::to_string( th ), plate::AxisVariance( p, th ) );
				for( double r : { 0.01, 0.5, 2.0, 10.0 } )
					d( ck + ".profile." + std::to_string( th ) + "." + std::to_string( r ), plate::Profile( p, th, r ) );
			}
		}
		// The fit.
		const plate::FitTable& t = plate::TableFor( m );
		d( key + ".worstL1", t.worstL1 );
		d( key + ".worstL1At.d", t.worstL1At[ 0 ] );
		d( key + ".worstL1At.c", t.worstL1At[ 1 ] );
		for( size_t n = 0; n < t.weights.size(); ++n )
			d( key + ".w." + std::to_string( n ), t.weights[ n ] );
	}

	// The kernels on their own.
	for( int n = 1; n <= 400; ++n )
	{
		const double variance = n * 0.04;
		const std::vector< double > k = plate::ExactKernel( variance );
		const std::string key = "kernel." + std::to_string( n );
		i( key + ".size", static_cast< long >( k.size() ) );
		for( size_t t = 0; t < k.size(); ++t )
			f( key + "." + std::to_string( t ), static_cast< float >( k[ t ] ) );
	}

	// The plans.
	std::ifstream file( argv[ 1 ] );
	std::string line;
	int index = 0;
	while( std::getline( file, line ) )
	{
		std::istringstream words( line );
		std::string sm, sthin, sthick, swidth;
		int W = 0, H = 0;
		if( !( words >> sm >> sthin >> sthick >> swidth >> W >> H ) )
			continue;
		const float vm = std::strtof( sm.c_str(), nullptr ), vthin = std::strtof( sthin.c_str(), nullptr ),
		            vthick = std::strtof( sthick.c_str(), nullptr ), vwidth = std::strtof( swidth.c_str(), nullptr );
		const std::string key = "plan." + std::to_string( index++ );
		const plate::Plan plan = plate::PlanFor( MaterialFromParam( vm ), ThinnestFromParam( vthin ), ThickestFromParam( vthick ),
		                                         PlateWidthFromParam( vwidth ), W, H, 0 );
		d( key + ".pitch", plan.pitch );
		d( key + ".dThin", plan.dThin );
		d( key + ".dThick", plan.dThick );
		f( key + ".uThin", static_cast< float >( plan.dThin ) );
		f( key + ".uSpan", static_cast< float >( plan.dThick - plan.dThin ) );
		f( key + ".uPitch", static_cast< float >( plan.pitch ) );
		i( key + ".material", plan.material );
		for( int j = 0; j < plate::kScales; ++j )
		{
			const std::string sk = key + ".scale." + std::to_string( j );
			i( sk + ".active", plan.active[ j ] ? 1 : 0 );
			i( sk + ".delta", plan.delta[ j ] ? 1 : 0 );
			i( sk + ".factor", plan.factor[ j ] );
			d( sk + ".sigmaPx", plan.sigmaPx[ j ] );
		}
		i( key + ".groups", static_cast< long >( plan.groups.size() ) );
		for( size_t g = 0; g < plan.groups.size(); ++g )
		{
			const plate::Group& gr = plan.groups[ g ];
			const std::string gk = key + ".group." + std::to_string( g );
			i( gk + ".factor", gr.factor );
			i( gk + ".count", gr.count );
			for( int s = 0; s < gr.count; ++s )
			{
				i( gk + ".scale." + std::to_string( s ), gr.scale[ s ] );
				i( gk + ".radius." + std::to_string( s ), gr.radius[ s ] );
				i( gk + ".kernelRow." + std::to_string( s ), gr.kernelRow[ s ] );
			}
			i( gk + ".lowWidth", gr.lowWidth );
			i( gk + ".lowHeight", gr.lowHeight );
		}
		i( key + ".kernelRadius", plan.kernelRadius );
		i( key + ".kernelRows", plan.kernelRows );
		for( size_t n = 0; n < plan.table.size(); ++n )
			f( key + ".table." + std::to_string( n ), plan.table[ n ] );
		for( size_t n = 0; n < plan.kernels.size(); ++n )
			f( key + ".kernels." + std::to_string( n ), plan.kernels[ n ] );
	}
	return 0;
}
