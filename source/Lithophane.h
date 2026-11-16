#pragma once

#include "Controls.h"
#include "MultiTarget.h"
#include "PassBuffer.h"
#include "Plate.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace lithophane
{

/// The harness's negative controls and test seams. All off in the plugin.
struct PluginHooks
{
	int attenuationModel = 0;  ///< 1 red and blue coefficients swapped; 2 a linear 1 - k d law
	int kernelModel      = 0;  ///< 1 every thickness given the 1.5 mm row; 2 a variance-matched single Gaussian
	int decimationModel  = 0;  ///< 1 a box decimation, nearest reconstruction
	int frontModel       = 0;  ///< 1 the clip's colour let into the front term; 2 the normal's sign flipped
	bool reliefIgnored   = false;///< Facing Lamp shaded as if the relief faced the viewer
	int alphaModel       = 0;  ///< 1 the clip's alpha passed through
	bool premultiply     = true;///< false: the thickness from the clip's straight colour
	bool clampOn         = true;///< false: the clip's NaN and range not tamed
	int edgeModel        = 0;  ///< 1 black past the frame's edge
	bool leakGLState     = false;
	bool timeLeak        = false;///< the light's angle turned by the host's clock (a plugin with state in time)
	bool planKeptOnResize = false;///< the frame's plan kept across a new raster (a plugin with state)
};

/**
    The plugin, `SW Lithophane`: the clip carved into a backlit porcelain
    plate. Every frame: the plan (Plate.h: which scales, at which factor, the
    weight table and kernels; rebuilt when an input to it changes), then the
    passes of Shaders.h -- carve, per group spread and pass, composite.

    Host indices are the ParamIds, About last.
*/
class LithophanePlugin : public CFFGLPlugin
{
public:
	LithophanePlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	/// Stored, never read by the plugin (only by the --clock negative control).
	FFResult SetTime( double time ) override;

	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	PluginHooks& HooksForTest()
	{
		return hooks;
	}
	const plate::Plan& LastPlan() const
	{
		return plan;
	}
	/// The carve texture: (a_R, a_G, a_B, d).
	GLuint CarveTextureID() const
	{
		return carve.TextureID();
	}
	uint32_t FrameIndex() const
	{
		return frameIndex;
	}

private:
	bool ensureBuffers( int width, int height );
	void bindTarget( GLuint fbo, int width, int height );
	void setKernels( const ffglex::FFGLShader& shader, const plate::Group& group );
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};
	PluginHooks hooks;
	uint32_t frameIndex = 0;
	double hostTime     = 0.0;
	int firstWidth      = 0;///< read only by the planKeptOnResize negative control

	// The plan and what it was made for.
	plate::Plan plan;
	bool planValid      = false;
	double planKey[ 7 ] = {};
	bool tableUploaded  = false;

	ffglex::FFGLShader carveShader, spreadShader, passShader, compositeShader;
	GLuint vao          = 0;
	GLuint tableTexture = 0;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	PassBuffer carve;
	/// The full-resolution groups' first pass, shared by them (one at a time).
	MultiTarget fullSpread;
	/// Per group: the spread (low-res width, full height), the decimation
	/// along y, the Gaussian along x, and the result (one texture).
	struct Stages
	{
		MultiTarget spread, decimated, across, result;
	};
	Stages stages[ plate::kMaxGroups ];
};

} // namespace lithophane
