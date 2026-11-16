#pragma once

#include <FFGLSDK.h>

namespace lithophane
{
/**
    A framebuffer with one to four colour textures, drawn into at once
    (multiple render targets): one per Gaussian scale of a group.

    Every texture is RGBA16F (SPEC-lithophane.md, The chain) unless asked,
    nearest, clamp to edge, and read texel for texel. It reallocates only
    when the size, the count or the format changes, and clears what it
    allocates, as PassBuffer does.
*/
class MultiTarget
{
public:
	static constexpr int kMaxTargets = 4;

	bool Ensure( int width, int height, int count, GLint format = GL_RGBA16F );
	void Destroy();
	void Clear();

	GLuint FramebufferID() const
	{
		return fbo;
	}
	GLuint TextureID( int i ) const
	{
		return i >= 0 && i < kMaxTargets ? textures[ i ] : 0;
	}
	int Width() const
	{
		return width;
	}
	int Height() const
	{
		return height;
	}
	int Count() const
	{
		return count;
	}

private:
	GLuint fbo                    = 0;
	GLuint textures[ kMaxTargets ] = {};
	int width = 0, height = 0, count = 0;
	GLint format = 0;
};

} // namespace lithophane
