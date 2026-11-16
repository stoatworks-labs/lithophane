#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace lithophane
{
namespace
{
double clamp01( float v )
{
	//NaN reads as 0, never as a NaN thickness.
	return v == v ? std::clamp( static_cast< double >( v ), 0.0, 1.0 ) : 0.0;
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

const char* const kMappingNames[] = { "Linear Depth", "Linear Light" };
const char* const kReliefNames[]  = { "Facing Viewer", "Facing Lamp" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}

//---------------------------------------------------------------------------
// The defaults (SPEC-lithophane.md, Controls). 0.8 and 3 mm and a 100 mm
// plate: 3dp.rocks' "Thinnest Layer", "Thickness" and "Maximum Size"; Sugiyama
// body (a), "a typical body"; thickness linear in luma (3dp.rocks; Meissen's
// "dark areas need to be applied thickly"); the relief toward the viewer (the
// printing guides). The lamp behind, the room dark. Front lamp from the upper
// left at 45 degrees (chosen). Back Light from the demo clips.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	standard( PT_THINNEST, "Thinnest", "Plate", ParamFromThinnest( 0.8 ) ),
	standard( PT_THICKEST, "Thickest", "Plate", ParamFromThickest( 3.0 ) ),
	option( PT_MATERIAL, "Material", "Plate", 0.0f, kMaterialNames, kMaterialCount ),
	option( PT_MAPPING, "Mapping", "Plate", 0.0f, kMappingNames, 2 ),
	option( PT_RELIEF_SIDE, "Relief Side", "Plate", 0.0f, kReliefNames, 2 ),
	standard( PT_PLATE_WIDTH, "Plate Width", "Plate", ParamFromPlateWidth( 100.0 ) ),
	standard( PT_BACK_LIGHT, "Back Light", "Light", ParamFromBackLight( kDefaultBackLight ) ),
	standard( PT_FRONT_LIGHT, "Front Light", "Light", 0.0f ),
	standard( PT_LIGHT_ANGLE, "Light Angle", "Light", ParamFromLightAngle( 135.0 ) ),
	standard( PT_LIGHT_ELEVATION, "Light Elevation", "Light", ParamFromLightElevation( 45.0 ) ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const char* const kMaterialNames[ kMaterialCount ] = { "Porcelain", "Feldspar Porcelain", "Clay Porcelain", "White PLA" };

//Set from the demo clips: tools/clips.py defaults.
const double kDefaultBackLight = 2.0;

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const double v = value == value ? static_cast< double >( value ) : 0.0;
	const long i   = std::lround( std::clamp( v, -1.0, static_cast< double >( count ) ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double ThinnestFromParam( float v )
{
	return linear( v, kThinnestLow, kThinnestHigh );
}
float ParamFromThinnest( double mm )
{
	return inverseLinear( mm, kThinnestLow, kThinnestHigh );
}
double ThickestFromParam( float v )
{
	return linear( v, kThickestLow, kThickestHigh );
}
float ParamFromThickest( double mm )
{
	return inverseLinear( mm, kThickestLow, kThickestHigh );
}
int MaterialFromParam( float v )
{
	return OptionIndex( v, kMaterialCount );
}
int MappingFromParam( float v )
{
	return OptionIndex( v, 2 );
}
int ReliefSideFromParam( float v )
{
	return OptionIndex( v, 2 );
}
double PlateWidthFromParam( float v )
{
	return linear( v, kPlateWidthLow, kPlateWidthHigh );
}
float ParamFromPlateWidth( double mm )
{
	return inverseLinear( mm, kPlateWidthLow, kPlateWidthHigh );
}
double BackLightFromParam( float v )
{
	return linear( v, 0.0, kBackLightHigh );
}
float ParamFromBackLight( double level )
{
	return inverseLinear( level, 0.0, kBackLightHigh );
}
double FrontLightFromParam( float v )
{
	return linear( v, 0.0, kFrontLightHigh );
}
float ParamFromFrontLight( double level )
{
	return inverseLinear( level, 0.0, kFrontLightHigh );
}
double LightAngleFromParam( float v )
{
	return linear( v, 0.0, kAngleHigh );
}
float ParamFromLightAngle( double degrees )
{
	return inverseLinear( degrees, 0.0, kAngleHigh );
}
double LightElevationFromParam( float v )
{
	return linear( v, kElevationLow, kElevationHigh );
}
float ParamFromLightElevation( double degrees )
{
	return inverseLinear( degrees, kElevationLow, kElevationHigh );
}

} // namespace lithophane
