#include "Lithophane.h"

/**
    The registration. `lithophane_core` is an OBJECT library so this
    file-scope constructor, which nothing references by name, is linked into
    the bundle (an archive would let the linker drop it, and the bundle would
    load and report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< lithophane::LithophanePlugin >,// Create method
	"LP01",                                       // Plugin unique ID of maximum length 4
	"SW Lithophane",                              // Plugin name
	2,                                            // API major version number
	1,                                            // API minor version number
	0,                                            // Plugin major version number
	1,                                            // Plugin minor version number
	FF_EFFECT,                                    // Plugin type
	"The clip carved into a backlit porcelain plate. Dark is thick and light is thin; the lamp behind shines through "
	"each column of porcelain by the material's measured attenuation, red travelling further than blue, and the light "
	"spreads inside the plate by its measured scattering, more through the thick parts: a warm, soft picture only when "
	"backlit, its shadows' detail lost first. Front-lit, the plate is a white relief with no picture.",
	"Lithophane FFGL effect"// About
);

extern "C" const char* LithophaneBuildStamp()
{
	return "lithophane " LITHOPHANE_VERSION ", built " __DATE__ " " __TIME__;
}
