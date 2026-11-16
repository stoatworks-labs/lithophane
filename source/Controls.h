#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged STANDARD parameter the host sees is 0..1, because
    `SetParamInfo` clamps an `FF_TYPE_STANDARD` default into 0..1 before
    `SetParamRange` could widen it. The conversions live in Controls.cpp, one
    function per control, each with its inverse where a default or the harness
    needs it. Option parameters hold the element index (an option reads back
    0..1 whatever its count: map by index).

    Units: millimetres for the plate (Thinnest, Thickest, Plate Width -- the
    frame's width), degrees for the front lamp's direction, and two light
    levels in linear light: Back Light is the lamp's radiance as a plate of
    zero thickness would show it, Front Light the irradiance at which a white
    Lambertian sheet facing the lamp reads Front Light. Every mapping is linear
    so a harness can choose an exact value.
*/
namespace lithophane
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Plate -------------------------------------------------------------------
	PT_THINNEST = 0,
	PT_THICKEST,
	PT_MATERIAL,
	PT_MAPPING,
	PT_RELIEF_SIDE,
	PT_PLATE_WIDTH,

	// -- Light -------------------------------------------------------------------
	PT_BACK_LIGHT,
	PT_FRONT_LIGHT,
	PT_LIGHT_ANGLE,
	PT_LIGHT_ELEVATION,

	// -- Look --------------------------------------------------------------------
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

/// The options, in element order.
enum Mapping
{
	kLinearDepth = 0,///< thickness linear in the codes' Rec. 601 luma (3dp.rocks)
	kLinearLight = 1 ///< transmission linear in luminance (Cura's transparency model)
};
enum ReliefSide
{
	kFacingViewer = 0,
	kFacingLamp   = 1
};
constexpr int kMaterialCount = 4;
extern const char* const kMaterialNames[ kMaterialCount ];

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back. All linear.
//---------------------------------------------------------------------------
constexpr double kThinnestLow = 0.5, kThinnestHigh = 3.0;     ///< mm
constexpr double kThickestLow = 0.5, kThickestHigh = 6.0;     ///< mm
constexpr double kPlateWidthLow = 20.0, kPlateWidthHigh = 400.0;///< mm
constexpr double kBackLightHigh  = 8.0;
constexpr double kFrontLightHigh = 2.0;
constexpr double kAngleHigh      = 360.0;///< degrees
constexpr double kElevationLow = 5.0, kElevationHigh = 90.0;  ///< degrees

double ThinnestFromParam( float v );
float ParamFromThinnest( double mm );
double ThickestFromParam( float v );
float ParamFromThickest( double mm );
int MaterialFromParam( float v );
int MappingFromParam( float v );
int ReliefSideFromParam( float v );
double PlateWidthFromParam( float v );
float ParamFromPlateWidth( double mm );
double BackLightFromParam( float v );
float ParamFromBackLight( double level );
double FrontLightFromParam( float v );
float ParamFromFrontLight( double level );
double LightAngleFromParam( float v );///< degrees, 0 from the right, 90 from the top
float ParamFromLightAngle( double degrees );
double LightElevationFromParam( float v );///< degrees above the plate
float ParamFromLightElevation( double degrees );

/// Back Light's default, from Resolume's demo clips (tools/clips.py defaults).
extern const double kDefaultBackLight;

} // namespace lithophane
