#include "Lithophane.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace lithophane
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

/// Texture units: the clip, the carve, the table, then the groups.
constexpr int kUnitInput = 0, kUnitCarve = 1, kUnitTable = 2, kUnitLevel = 3;
constexpr int kUnitsUsed = kUnitLevel + shaders::kMaxLevels;

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

GLuint makeDataTexture()
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

void uploadData( GLuint texture, int width, int height, const float* texels )
{
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glPixelStorei( GL_UNPACK_ROW_LENGTH, 0 );
	glPixelStorei( GL_UNPACK_SKIP_PIXELS, 0 );
	glPixelStorei( GL_UNPACK_SKIP_ROWS, 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, texels );
	glBindTexture( GL_TEXTURE_2D, 0 );
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );
static_assert( plate::kMaxGroups == shaders::kMaxLevels, "the composite reads one texture per group" );
static_assert( plate::kMaxGroupScales == MultiTarget::kMaxTargets, "a group's scales are one render target each" );

//---------------------------------------------------------------------------
LithophanePlugin::LithophanePlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//Nothing moves with time: every frame is the clip and the controls alone.
	SetTimeSupported( false );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint LithophanePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult LithophanePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage list[] = {
		{ &carveShader, shaders::Pass::Carve, "carve" },
		{ &spreadShader, shaders::Pass::Spread, "spread" },
		{ &passShader, shaders::Pass::Pass, "pass" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : list )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Lithophane: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//A core profile draws nothing without a vertex array bound; the
	//triangle needs no attributes.
	glGenVertexArrays( 1, &vao );
	tableTexture  = makeDataTexture();
	tableUploaded = false;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
bool LithophanePlugin::ensureBuffers( int width, int height )
{
	bool ok = carve.Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Nearest );

	//One shared target for the full-resolution groups, as many textures as
	//the largest of them needs; none if there are none.
	int fullCount = 0;
	for( const plate::Group& g : plan.groups )
		if( g.factor == 1 )
			fullCount = std::max( fullCount, g.count );
	if( fullCount > 0 )
		ok = ok && fullSpread.Ensure( width, height, fullCount );
	else
		fullSpread.Destroy();

	for( int i = 0; i < plate::kMaxGroups; ++i )
	{
		Stages& s = stages[ i ];
		if( i >= static_cast< int >( plan.groups.size() ) )
		{
			for( MultiTarget* t : { &s.spread, &s.decimated, &s.across, &s.result } )
				t->Destroy();
			continue;
		}
		const plate::Group& g = plan.groups[ static_cast< size_t >( i ) ];
		if( g.factor == 1 )
		{
			for( MultiTarget* t : { &s.spread, &s.decimated, &s.across } )
				t->Destroy();
			ok = ok && s.result.Ensure( width, height, 1 );
			continue;
		}
		ok = ok && s.spread.Ensure( g.lowWidth, height, g.count );
		ok = ok && s.decimated.Ensure( g.lowWidth, g.lowHeight, g.count );
		ok = ok && s.across.Ensure( g.lowWidth, g.lowHeight, g.count );
		ok = ok && s.result.Ensure( g.lowWidth, g.lowHeight, 1 );
	}
	return ok;
}

void LithophanePlugin::bindTarget( GLuint fbo, int width, int height )
{
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, static_cast< GLsizei >( width ), static_cast< GLsizei >( height ) );
}

void LithophanePlugin::setKernels( const FFGLShader& shader, const plate::Group& group )
{
	float k[ plate::kMaxGroupScales * shaders::kKernelTaps ] = {};
	int widest = 0;
	const int stride = 2 * plan.kernelRadius + 1;
	for( int s = 0; s < group.count; ++s )
	{
		const int R = std::min( group.radius[ s ], shaders::kKernelCentre );
		widest      = std::max( widest, R );
		for( int o = -R; o <= R; ++o )
			k[ s * shaders::kKernelTaps + o + shaders::kKernelCentre ]
			    = plan.kernels[ ( static_cast< size_t >( group.kernelRow[ s ] ) * stride + static_cast< size_t >( o + plan.kernelRadius ) ) * 4 ];
	}
	glUniform1fv( where( shader, "Kernel" ), plate::kMaxGroupScales * shaders::kKernelTaps, k );
	glUniform1i( where( shader, "Radius" ), widest );
}

//---------------------------------------------------------------------------
void LithophanePlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the passes' buffers" );
		return;
	}
	glBindVertexArray( vao );

	const int material         = MaterialFromParam( params[ PT_MATERIAL ] );
	const plate::Material& mat = plate::MaterialAt( material );
	const double span          = plan.dThick - plan.dThin;
	const int decimationKind   = hooks.decimationModel == 1 ? 2 : 1;

	auto setTable = [ & ]( const FFGLShader& s ) {
		glUniform1i( where( s, "Table" ), kUnitTable );
		glUniform1f( where( s, "RowOrigin" ), static_cast< float >( plate::kRowOrigin ) );
		glUniform1f( where( s, "RowStep" ), static_cast< float >( plate::kRowStep ) );
		glUniform1i( where( s, "RowCount" ), plate::kRows );
	};
	bindUnit( kUnitTable, tableTexture );

	//1. carve
	glUseProgram( carveShader.GetGLID() );
	bindUnit( kUnitInput, input->Handle );
	glUniform1i( where( carveShader, "InputTexture" ), kUnitInput );
	setTable( carveShader );
	glUniform1i( where( carveShader, "Mapping" ), MappingFromParam( params[ PT_MAPPING ] ) );
	glUniform1f( where( carveShader, "Thin" ), static_cast< float >( plan.dThin ) );
	glUniform1f( where( carveShader, "Span" ), static_cast< float >( span ) );
	glUniform3f( where( carveShader, "Coefficient" ), static_cast< float >( mat.k[ 0 ] ), static_cast< float >( mat.k[ 1 ] ),
	             static_cast< float >( mat.k[ 2 ] ) );
	glUniform1f( where( carveShader, "LightK" ), static_cast< float >( mat.k[ 1 ] ) );
	glUniform1f( where( carveShader, "MinT" ), static_cast< float >( std::exp( -mat.k[ 1 ] * span ) ) );
	glUniform1i( where( carveShader, "AttenuationModel" ), hooks.attenuationModel );
	glUniform1i( where( carveShader, "Premultiply" ), hooks.premultiply ? 1 : 0 );
	glUniform1i( where( carveShader, "ClampOn" ), hooks.clampOn ? 1 : 0 );
	bindTarget( carve.GetGLID(), width, height );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	//2. per group: spread along x from the carve, then the passes
	bindUnit( kUnitCarve, carve.TextureID() );
	for( size_t gi = 0; gi < plan.groups.size(); ++gi )
	{
		const plate::Group& g = plan.groups[ gi ];
		Stages& st            = stages[ gi ];
		const bool full       = g.factor == 1;

		glUseProgram( spreadShader.GetGLID() );
		glUniform1i( where( spreadShader, "Carve" ), kUnitCarve );
		setTable( spreadShader );
		glUniform1i( where( spreadShader, "Count" ), g.count );
		glUniform4i( where( spreadShader, "Column" ), g.scale[ 0 ], g.scale[ 1 ], g.scale[ 2 ], g.scale[ 3 ] );
		glUniform1i( where( spreadShader, "Kind" ), full ? 0 : decimationKind );
		glUniform1i( where( spreadShader, "Factor" ), g.factor );
		glUniform1i( where( spreadShader, "SourceWidth" ), width );
		glUniform1i( where( spreadShader, "EdgeModel" ), hooks.edgeModel );
		setKernels( spreadShader, g );
		if( full )
			bindTarget( fullSpread.FramebufferID(), width, height );
		else
			bindTarget( st.spread.FramebufferID(), g.lowWidth, height );
		glDrawArrays( GL_TRIANGLES, 0, 3 );

		glUseProgram( passShader.GetGLID() );
		glUniform1i( where( passShader, "Source0" ), 4 );
		glUniform1i( where( passShader, "Source1" ), 5 );
		glUniform1i( where( passShader, "Source2" ), 6 );
		glUniform1i( where( passShader, "Source3" ), 7 );
		glUniform1i( where( passShader, "Count" ), g.count );
		glUniform1i( where( passShader, "Factor" ), g.factor );
		glUniform1i( where( passShader, "EdgeModel" ), hooks.edgeModel );
		setKernels( passShader, g );
		struct Step
		{
			const MultiTarget* from;
			const MultiTarget* to;
			int kind, dx, dy, sum;
		};
		const Step full1[] = { { &fullSpread, &st.result, 0, 0, 1, 1 } };
		const Step low[]   = {
            { &st.spread, &st.decimated, decimationKind, 0, 1, 0 },
            { &st.decimated, &st.across, 0, 1, 0, 0 },
            { &st.across, &st.result, 0, 0, 1, 1 },
        };
		const Step* steps = full ? full1 : low;
		const int count   = full ? 1 : 3;
		for( int i = 0; i < count; ++i )
		{
			const Step& s = steps[ i ];
			//A sampler the draw never reads still needs a complete texture
			//bound, or Apple's driver logs it as unloadable: the first.
			for( int t = 0; t < MultiTarget::kMaxTargets; ++t )
				bindUnit( 4 + t, t < s.from->Count() ? s.from->TextureID( t ) : s.from->TextureID( 0 ) );
			glUniform1i( where( passShader, "Kind" ), s.kind );
			glUniform2i( where( passShader, "Direction" ), s.dx, s.dy );
			glUniform2i( where( passShader, "SourceSize" ), s.from->Width(), s.from->Height() );
			glUniform1i( where( passShader, "Sum" ), s.sum );
			bindTarget( s.to->FramebufferID(), s.to->Width(), s.to->Height() );
			glDrawArrays( GL_TRIANGLES, 0, 3 );
		}
	}

	//3. composite, into the host's framebuffer and viewport
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( kUnitInput, input->Handle );
	bindUnit( kUnitCarve, carve.TextureID() );
	bindUnit( kUnitTable, tableTexture );
	static const char* const levelNames[ shaders::kMaxLevels ] = { "Level0", "Level1", "Level2", "Level3",
		                                                           "Level4", "Level5", "Level6", "Level7" };
	GLint factors[ shaders::kMaxLevels ] = {};
	for( int i = 0; i < shaders::kMaxLevels; ++i )
	{
		const bool used = i < static_cast< int >( plan.groups.size() );
		bindUnit( kUnitLevel + i, used ? stages[ i ].result.TextureID( 0 ) : carve.TextureID() );
		glUniform1i( where( compositeShader, levelNames[ i ] ), kUnitLevel + i );
		factors[ i ] = used ? plan.groups[ static_cast< size_t >( i ) ].factor : 1;
	}
	glUniform1i( where( compositeShader, "InputTexture" ), kUnitInput );
	glUniform1i( where( compositeShader, "Carve" ), kUnitCarve );
	setTable( compositeShader );
	glUniform1i( where( compositeShader, "LevelCount" ), static_cast< GLint >( plan.groups.size() ) );
	glUniform1iv( where( compositeShader, "LevelFactor" ), shaders::kMaxLevels, factors );
	glUniform1i( where( compositeShader, "DeltaColumn" ), plate::kDeltaColumn );
	glUniform1i( where( compositeShader, "Reconstruction" ), hooks.decimationModel == 1 ? 1 : 0 );
	glUniform2i( where( compositeShader, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform2i( where( compositeShader, "Size" ), width, height );
	glUniform1f( where( compositeShader, "BackLight" ), static_cast< float >( BackLightFromParam( params[ PT_BACK_LIGHT ] ) ) );
	glUniform1f( where( compositeShader, "FrontLight" ), static_cast< float >( FrontLightFromParam( params[ PT_FRONT_LIGHT ] ) ) );
	double azimuth = LightAngleFromParam( params[ PT_LIGHT_ANGLE ] );
	if( hooks.timeLeak )
		azimuth += hostTime;
	const double theta = azimuth * kPi / 180.0;
	const double e     = LightElevationFromParam( params[ PT_LIGHT_ELEVATION ] ) * kPi / 180.0;
	glUniform3f( where( compositeShader, "LightDir" ), static_cast< float >( std::cos( e ) * std::cos( theta ) ),
	             static_cast< float >( std::cos( e ) * std::sin( theta ) ), static_cast< float >( std::sin( e ) ) );
	glUniform1f( where( compositeShader, "Pitch" ), static_cast< float >( plan.pitch ) );
	glUniform1i( where( compositeShader, "Relief" ), hooks.reliefIgnored ? kFacingViewer : ReliefSideFromParam( params[ PT_RELIEF_SIDE ] ) );
	glUniform3f( where( compositeShader, "KmK" ), static_cast< float >( mat.K[ 0 ] ), static_cast< float >( mat.K[ 1 ] ),
	             static_cast< float >( mat.K[ 2 ] ) );
	glUniform3f( where( compositeShader, "KmS" ), static_cast< float >( mat.S[ 0 ] ), static_cast< float >( mat.S[ 1 ] ),
	             static_cast< float >( mat.S[ 2 ] ) );
	glUniform1i( where( compositeShader, "FrontModel" ), hooks.frontModel );
	glUniform1f( where( compositeShader, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	glUniform1i( where( compositeShader, "AlphaModel" ), hooks.alphaModel );
	glUniform1i( where( compositeShader, "ClampOn" ), hooks.clampOn ? 1 : 0 );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	unbindTextureUnits( kUnitsUsed );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
FFResult LithophanePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	restore.restoreOnExit     = !hooks.leakGLState;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 || vao == 0 )
		return FF_FAIL;

	//The plan is a pure function of these; it is remade when one changes,
	//so keeping it is not state (the --resize check).
	const int material = MaterialFromParam( params[ PT_MATERIAL ] );
	const double thin  = ThinnestFromParam( params[ PT_THINNEST ] );
	const double thick = ThickestFromParam( params[ PT_THICKEST ] );
	double plateWidth  = PlateWidthFromParam( params[ PT_PLATE_WIDTH ] );
	if( firstWidth == 0 )
		firstWidth = width;
	if( hooks.planKeptOnResize )
		plateWidth *= static_cast< double >( width ) / firstWidth;//the first raster's pitch, kept
	const double key[ 7 ] = { static_cast< double >( material ), thin, thick, plateWidth, static_cast< double >( width ),
		                      static_cast< double >( height ), static_cast< double >( hooks.kernelModel ) };
	if( !planValid || std::memcmp( key, planKey, sizeof( key ) ) != 0 )
	{
		const bool firstForMaterial = !planValid || static_cast< int >( planKey[ 0 ] ) != material;
		plan = plate::PlanFor( material, thin, thick, plateWidth, width, height, hooks.kernelModel );
		if( plan.groups.size() > plate::kMaxGroups )
		{
			diag::error( "the plan has more groups than the composite reads - the widest scales are dropped" );
			plan.groups.resize( plate::kMaxGroups );
		}
		std::memcpy( planKey, key, sizeof( key ) );
		planValid     = true;
		tableUploaded = false;
		if( firstForMaterial )
		{
			const plate::FitTable& fit = plate::TableFor( material );
			diag::info( std::string( "material " ) + plate::MaterialAt( material ).name + ": the mixture's worst 2-D L1 distance "
			            + std::to_string( fit.worstL1 ) + " at " + std::to_string( fit.worstL1At[ 0 ] ) + " mm, channel "
			            + std::to_string( static_cast< int >( fit.worstL1At[ 1 ] ) ) );
		}
	}
	if( !tableUploaded )
	{
		uploadData( tableTexture, plate::kTableWidth, plate::kRows, plan.table.data() );
		tableUploaded = true;
	}

	render( input, hostViewport, pgl->HostFBO, width, height );
	++frameIndex;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult LithophanePlugin::DeInitGL()
{
	uniformCache.clear();
	for( FFGLShader* s : { &carveShader, &spreadShader, &passShader, &compositeShader } )
		s->FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
	if( tableTexture != 0 )
		glDeleteTextures( 1, &tableTexture );
	tableTexture  = 0;
	tableUploaded = false;
	carve.Destroy();
	fullSpread.Destroy();
	for( Stages& s : stages )
		for( MultiTarget* t : { &s.spread, &s.decimated, &s.across, &s.result } )
			t->Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult LithophanePlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* LithophanePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult LithophanePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult LithophanePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	params[ index ] = value;
	return FF_SUCCESS;
}

float LithophanePlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace lithophane
