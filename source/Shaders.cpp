#include "Shaders.h"

namespace lithophane::shaders
{

const char* const kVersion = "#version 410 core\n";

/// The full-raster triangle, from gl_VertexID alone.
const char* const kQuadVertex = R"(
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
)";

/// IEC 61966-2-1 both ways; the quadratic B-spline; the weight table, read
/// by thickness with the rows interpolated by hand; a NaN read as 0 by its
/// bits (a comparison can be folded away by a compiler that assumes no NaN).
const char* const kCommon = R"(
const int kKernelCentre = 20;
const int kKernelTaps   = 41;

uniform sampler2D Table;// columns: the scales, then the delta column; rows: thickness
uniform float RowOrigin;// mm
uniform float RowStep;  // mm
uniform int RowCount;

vec3 toLinear( vec3 c )
{
	c       = max( c, vec3( 0.0 ) );
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
vec4 tame( vec4 c )
{
	uvec4 bits  = floatBitsToUint( c );
	bvec4 isNaN = bvec4( uvec4( equal( bits & 0x7f800000u, uvec4( 0x7f800000u ) ) ) * uvec4( notEqual( bits & 0x007fffffu, uvec4( 0u ) ) ) );
	return mix( clamp( c, 0.0, 1.0 ), vec4( 0.0 ), isNaN );
}

float bspline( float u )
{
	u = abs( u );
	if( u < 0.5 )
		return 0.75 - u * u;
	if( u < 1.5 )
		return 0.5 * ( u - 1.5 ) * ( u - 1.5 );
	return 0.0;
}

// The row below a thickness and the fraction of the way to the next.
int rowOf( float d, out float t )
{
	float x = ( d - RowOrigin ) / RowStep;
	float f = clamp( floor( x ), 0.0, float( RowCount - 2 ) );
	t       = clamp( x - f, 0.0, 1.0 );
	return int( f );
}
vec3 weightAt( int column, int row, float t )
{
	vec3 lo = texelFetch( Table, ivec2( column, row ), 0 ).rgb;
	vec3 hi = texelFetch( Table, ivec2( column, row + 1 ), 0 ).rgb;
	return lo + t * ( hi - lo );
}
)";

/// Output: (a_R, a_G, a_B, d). The clip decoded, weighted by its alpha
/// (straight alpha over black: a transparent pixel is black, the thickest
/// plate), re-encoded to codes for the mapping. Linear Depth: d linear in the
/// codes' Rec. 601 luma. Linear Light: Cura's, so the green transmission is
/// linear in the codes' 2.2-gamma luminance.
const char* const kCarveFragment = R"(
uniform sampler2D InputTexture;
uniform int Mapping;          // 0 Linear Depth; 1 Linear Light
uniform float Thin;           // mm
uniform float Span;           // Thickest - Thinnest, mm, >= 0
uniform vec3 Coefficient;     // k per channel, 1/mm
uniform float LightK;         // k_G, 1/mm
uniform float MinT;           // exp( -k_G Span )
uniform int AttenuationModel; // 0 the plugin; 1 red and blue swapped; 2 1 - k d
uniform int Premultiply;      // 1 the plugin; 0 the negative control
uniform int ClampOn;          // 1 the plugin; 0 the negative control
out vec4 FragColor;
void main()
{
	vec4 src = texelFetch( InputTexture, ivec2( gl_FragCoord.xy ), 0 );
	if( ClampOn == 1 )
		src = tame( src );
	vec3 lin = toLinear( src.rgb );
	if( ClampOn == 1 )
		lin = clamp( lin, 0.0, 1.0 );
	if( Premultiply == 1 )
		lin *= src.a;
	vec3 code = toCode( lin );
	float d;
	if( Mapping == 1 )
	{
		float lum = dot( pow( code, vec3( 2.2 ) ), vec3( 0.299, 0.587, 0.114 ) );
		d         = Thin - log( MinT + ( 1.0 - MinT ) * lum ) / LightK;
	}
	else
		d = Thin + ( 1.0 - dot( code, vec3( 0.299, 0.587, 0.114 ) ) ) * Span;
	d = clamp( d, Thin, Thin + Span );
	vec3 k = AttenuationModel == 1 ? Coefficient.bgr : Coefficient;
	vec3 a = AttenuationModel == 2 ? max( vec3( 1.0 ) - k * d, vec3( 0.0 ) ) : exp( -k * d );
	FragColor = vec4( a, d );
}
)";

/// Along x, from the carve texture: per tap, a times the thickness's weight
/// for each scale of the group, then either the scale's sampled Gaussian
/// (Kind 0, output the same size) or the decimation by Factor (Kind 1 the
/// quadratic B-spline, support 3 Factor; Kind 2 a box, the negative
/// control). Output texel B of a decimation is low-res texel b = B - 2,
/// centred on pixel b f + (f - 1) / 2; its taps are pixels (b - 1) f + i,
/// i < 3 f, weighted B2( (i + 1/2) / f - 3/2 ) / f.
const char* const kSpreadFragment = R"(
uniform sampler2D Carve;
uniform int Count;           // scales in the group, 1..4
uniform ivec4 Column;        // each scale's table column
uniform int Kind;            // 0 Gaussian; 1 B-spline decimation; 2 box decimation
uniform int Factor;
uniform int Radius;          // the group's widest kernel
uniform float Kernel[ 164 ]; // kKernelTaps per scale
uniform int SourceWidth;
uniform int EdgeModel;       // 0 the plugin (clamp to edge); 1 black past the edge
layout( location = 0 ) out vec4 Out[ 4 ];
void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy );
	vec3 s0 = vec3( 0.0 ), s1 = vec3( 0.0 ), s2 = vec3( 0.0 ), s3 = vec3( 0.0 );
	int first = Kind == 0 ? at.x - Radius : ( at.x - 3 ) * Factor;
	int taps  = Kind == 0 ? 2 * Radius + 1 : 3 * Factor;
	float f   = float( Factor );
	for( int i = 0; i < taps; ++i )
	{
		int x = first + i;
		if( EdgeModel == 1 && ( x < 0 || x >= SourceWidth ) )
			continue;
		vec4 c = texelFetch( Carve, ivec2( clamp( x, 0, SourceWidth - 1 ), at.y ), 0 );
		float t;
		int row = rowOf( c.a, t );
		vec4 k;
		if( Kind == 0 )
		{
			int o = i - Radius + kKernelCentre;
			k     = vec4( Kernel[ o ], Kernel[ kKernelTaps + o ], Kernel[ 2 * kKernelTaps + o ], Kernel[ 3 * kKernelTaps + o ] );
		}
		else if( Kind == 1 )
			k = vec4( bspline( ( float( i ) + 0.5 ) / f - 1.5 ) / f );
		else
			k = vec4( i >= Factor && i < 2 * Factor ? 1.0 / f : 0.0 );
		s0 += k.x * c.rgb * weightAt( Column.x, row, t );
		if( Count > 1 )
			s1 += k.y * c.rgb * weightAt( Column.y, row, t );
		if( Count > 2 )
			s2 += k.z * c.rgb * weightAt( Column.z, row, t );
		if( Count > 3 )
			s3 += k.w * c.rgb * weightAt( Column.w, row, t );
	}
	Out[ 0 ] = vec4( s0, 0.0 );
	Out[ 1 ] = vec4( s1, 0.0 );
	Out[ 2 ] = vec4( s2, 0.0 );
	Out[ 3 ] = vec4( s3, 0.0 );
}
)";

/// Along Direction, from the group's targets: each scale's sampled Gaussian
/// (Kind 0) or the decimation along y (Kind 1 B-spline, 2 box, as spread's).
/// Sum 1: the scales added into the one target.
const char* const kPassFragment = R"(
uniform sampler2D Source0;
uniform sampler2D Source1;
uniform sampler2D Source2;
uniform sampler2D Source3;
uniform int Count;
uniform int Kind;
uniform int Factor;
uniform int Radius;
uniform float Kernel[ 164 ];
uniform ivec2 Direction;
uniform ivec2 SourceSize;
uniform int Sum;
uniform int EdgeModel;
layout( location = 0 ) out vec4 Out[ 4 ];
void main()
{
	ivec2 at   = ivec2( gl_FragCoord.xy );
	int along  = Direction.x == 1 ? at.x : at.y;
	int extent = Direction.x == 1 ? SourceSize.x : SourceSize.y;
	vec3 s0 = vec3( 0.0 ), s1 = vec3( 0.0 ), s2 = vec3( 0.0 ), s3 = vec3( 0.0 );
	int first = Kind == 0 ? along - Radius : ( along - 3 ) * Factor;
	int taps  = Kind == 0 ? 2 * Radius + 1 : 3 * Factor;
	float f   = float( Factor );
	for( int i = 0; i < taps; ++i )
	{
		int p = first + i;
		if( EdgeModel == 1 && ( p < 0 || p >= extent ) )
			continue;
		p        = clamp( p, 0, extent - 1 );
		ivec2 q  = Direction.x == 1 ? ivec2( p, at.y ) : ivec2( at.x, p );
		vec4 k;
		if( Kind == 0 )
		{
			int o = i - Radius + kKernelCentre;
			k     = vec4( Kernel[ o ], Kernel[ kKernelTaps + o ], Kernel[ 2 * kKernelTaps + o ], Kernel[ 3 * kKernelTaps + o ] );
		}
		else if( Kind == 1 )
			k = vec4( bspline( ( float( i ) + 0.5 ) / f - 1.5 ) / f );
		else
			k = vec4( i >= Factor && i < 2 * Factor ? 1.0 / f : 0.0 );
		s0 += k.x * texelFetch( Source0, q, 0 ).rgb;
		if( Count > 1 )
			s1 += k.y * texelFetch( Source1, q, 0 ).rgb;
		if( Count > 2 )
			s2 += k.z * texelFetch( Source2, q, 0 ).rgb;
		if( Count > 3 )
			s3 += k.w * texelFetch( Source3, q, 0 ).rgb;
	}
	if( Sum == 1 )
	{
		Out[ 0 ] = vec4( ( s0 + s1 ) + ( s2 + s3 ), 0.0 );
		return;
	}
	Out[ 0 ] = vec4( s0, 0.0 );
	Out[ 1 ] = vec4( s1, 0.0 );
	Out[ 2 ] = vec4( s2, 0.0 );
	Out[ 3 ] = vec4( s3, 0.0 );
}
)";

/// To the host. The back-lit picture: the delta column's share of a at the
/// pixel, plus every group's texture reconstructed at its factor (the
/// pixel's low-res coordinate t = (x - (f - 1)/2) / f, the three texels
/// nearest, weights B2(t - b)), times Back Light. The front light: Lambert
/// off the carved face (n from the thickness's central difference in mm over
/// the pitch, one-sided on the edge rows; flat when the relief faces the
/// lamp) times the Kubelka-Munk reflectance at the pixel's thickness. Output
/// alpha mix( a, 1, Mix ), mixed premultiplied; Mix 0 hands the clip through.
const char* const kCompositeFragment = R"(
uniform sampler2D InputTexture;
uniform sampler2D Carve;
uniform sampler2D Level0;
uniform sampler2D Level1;
uniform sampler2D Level2;
uniform sampler2D Level3;
uniform sampler2D Level4;
uniform sampler2D Level5;
uniform sampler2D Level6;
uniform sampler2D Level7;
uniform int LevelCount;
uniform int LevelFactor[ 8 ];
uniform int DeltaColumn;
uniform int Reconstruction;// 0 the plugin (B-spline); 1 the box's nearest texel
uniform ivec2 ViewOrigin;
uniform ivec2 Size;
uniform float BackLight;
uniform float FrontLight;
uniform vec3 LightDir;      // toward the lamp: x right, y up, z out of the plate
uniform float Pitch;        // mm per pixel
uniform int Relief;         // 0 facing the viewer; 1 facing the lamp
uniform vec3 KmK;           // Kubelka-Munk K per channel, 1/mm
uniform vec3 KmS;           // Kubelka-Munk S per channel, 1/mm
uniform int FrontModel;     // 0 the plugin; 1 the clip's colour let in; 2 the normal flipped
uniform float MixAmount;
uniform int AlphaModel;     // 0 the plugin; 1 the clip's alpha passed through
uniform int ClampOn;        // 1 the plugin; 0 the negative control
out vec4 FragColor;

vec3 level( sampler2D t, int f, ivec2 at )
{
	if( f == 1 )
		return texelFetch( t, at, 0 ).rgb;
	if( Reconstruction == 1 )
		return texelFetch( t, at / f + 2, 0 ).rgb;
	float ff = float( f );
	vec2 tc  = ( vec2( at ) - 0.5 * ( ff - 1.0 ) ) / ff;
	vec2 bn  = floor( tc + 0.5 );
	ivec2 b  = ivec2( bn ) + 2;
	vec3 wx  = vec3( bspline( tc.x - bn.x + 1.0 ), bspline( tc.x - bn.x ), bspline( tc.x - bn.x - 1.0 ) );
	vec3 wy  = vec3( bspline( tc.y - bn.y + 1.0 ), bspline( tc.y - bn.y ), bspline( tc.y - bn.y - 1.0 ) );
	vec3 sum = vec3( 0.0 );
	for( int j = 0; j < 3; ++j )
	{
		vec3 row = wx.x * texelFetch( t, b + ivec2( -1, j - 1 ), 0 ).rgb + wx.y * texelFetch( t, b + ivec2( 0, j - 1 ), 0 ).rgb
		           + wx.z * texelFetch( t, b + ivec2( 1, j - 1 ), 0 ).rgb;
		sum += wy[ j ] * row;
	}
	return sum;
}

float thicknessAt( ivec2 p )
{
	return texelFetch( Carve, p, 0 ).a;
}

vec3 reflectance( float d )
{
	vec3 kappa = sqrt( KmK * ( KmK + 2.0 * KmS ) );
	vec3 R     = KmS / ( KmK + KmS + kappa / tanh( kappa * d ) );
	return 0.06 + 0.94 * 0.5 * R / ( 1.0 - 0.5 * R );
}

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	if( ClampOn == 1 )
		src = tame( src );
	vec4 c = texelFetch( Carve, at, 0 );
	float t;
	int row   = rowOf( c.a, t );
	vec3 back = c.rgb * weightAt( DeltaColumn, row, t );
	if( LevelCount > 0 )
		back += level( Level0, LevelFactor[ 0 ], at );
	if( LevelCount > 1 )
		back += level( Level1, LevelFactor[ 1 ], at );
	if( LevelCount > 2 )
		back += level( Level2, LevelFactor[ 2 ], at );
	if( LevelCount > 3 )
		back += level( Level3, LevelFactor[ 3 ], at );
	if( LevelCount > 4 )
		back += level( Level4, LevelFactor[ 4 ], at );
	if( LevelCount > 5 )
		back += level( Level5, LevelFactor[ 5 ], at );
	if( LevelCount > 6 )
		back += level( Level6, LevelFactor[ 6 ], at );
	if( LevelCount > 7 )
		back += level( Level7, LevelFactor[ 7 ], at );
	vec3 lin = BackLight * back;

	if( FrontLight > 0.0 )
	{
		vec3 n = vec3( 0.0, 0.0, 1.0 );
		if( Relief == 0 )
		{
			// A central difference, one-sided on the frame's edge columns and
			// rows (repousse's: a clamped central difference there would halve
			// the slope).
			ivec2 lo  = max( at - ivec2( 1 ), ivec2( 0 ) );
			ivec2 hi  = min( at + ivec2( 1 ), Size - ivec2( 1 ) );
			vec2 span = vec2( max( hi - lo, ivec2( 1 ) ) ) * Pitch;
			vec2 g    = vec2( thicknessAt( ivec2( hi.x, at.y ) ) - thicknessAt( ivec2( lo.x, at.y ) ),
			                  thicknessAt( ivec2( at.x, hi.y ) ) - thicknessAt( ivec2( at.x, lo.y ) ) ) / span;
			n         = normalize( vec3( FrontModel == 2 ? g : -g, 1.0 ) );
		}
		vec3 front = FrontLight * max( dot( n, LightDir ), 0.0 ) * reflectance( c.a );
		if( FrontModel == 1 )
			front *= toLinear( src.rgb );
		lin += front;
	}

	vec3 rgb   = toCode( lin );
	float a    = clamp( src.a, 0.0, 1.0 );
	float outA = AlphaModel == 1 ? a : 1.0;
	float m    = min( MixAmount, 1.0 );
	float mixA = a * ( 1.0 - m ) + outA * m;
	vec3 mixC  = ( src.rgb * a * ( 1.0 - m ) + rgb * outA * m ) / max( mixA, 1e-30 );
	FragColor  = vec4( mixC, mixA );
}
)";

std::string AssembleVertex( Pass )
{
	return std::string( kVersion ) + kQuadVertex;
}

std::string Assemble( Pass pass )
{
	switch( pass )
	{
	case Pass::Carve: return std::string( kVersion ) + kCommon + kCarveFragment;
	case Pass::Spread: return std::string( kVersion ) + kCommon + kSpreadFragment;
	case Pass::Pass: return std::string( kVersion ) + kCommon + kPassFragment;
	case Pass::Composite: return std::string( kVersion ) + kCommon + kCompositeFragment;
	}
	return std::string();
}

} // namespace lithophane::shaders
