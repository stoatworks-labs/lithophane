#include "MultiTarget.h"

namespace lithophane
{

bool MultiTarget::Ensure( int requestedWidth, int requestedHeight, int requestedCount, GLint requestedFormat )
{
	if( requestedWidth <= 0 || requestedHeight <= 0 || requestedCount < 1 || requestedCount > kMaxTargets )
		return false;
	if( fbo != 0 && width == requestedWidth && height == requestedHeight && count == requestedCount && format == requestedFormat )
		return true;

	Destroy();
	GLint previousFBO = 0;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFBO );

	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	GLenum buffers[ kMaxTargets ];
	for( int i = 0; i < requestedCount; ++i )
	{
		glGenTextures( 1, &textures[ i ] );
		glBindTexture( GL_TEXTURE_2D, textures[ i ] );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexImage2D( GL_TEXTURE_2D, 0, requestedFormat, requestedWidth, requestedHeight, 0, GL_RGBA, GL_FLOAT, nullptr );
		glFramebufferTexture2D( GL_FRAMEBUFFER, static_cast< GLenum >( GL_COLOR_ATTACHMENT0 + i ), GL_TEXTURE_2D, textures[ i ], 0 );
		buffers[ i ] = static_cast< GLenum >( GL_COLOR_ATTACHMENT0 + i );
	}
	glBindTexture( GL_TEXTURE_2D, 0 );
	glDrawBuffers( requestedCount, buffers );
	const bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFBO ) );
	if( !complete )
	{
		Destroy();
		return false;
	}
	width  = requestedWidth;
	height = requestedHeight;
	count  = requestedCount;
	format = requestedFormat;
	Clear();
	return true;
}

void MultiTarget::Clear()
{
	if( fbo == 0 )
		return;
	GLint previousFBO = 0, previousViewport[ 4 ] = {};
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &previousFBO );
	glGetIntegerv( GL_VIEWPORT, previousViewport );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, width, height );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	glBindFramebuffer( GL_FRAMEBUFFER, static_cast< GLuint >( previousFBO ) );
	glViewport( previousViewport[ 0 ], previousViewport[ 1 ], previousViewport[ 2 ], previousViewport[ 3 ] );
}

void MultiTarget::Destroy()
{
	for( GLuint& t : textures )
	{
		if( t != 0 )
			glDeleteTextures( 1, &t );
		t = 0;
	}
	if( fbo != 0 )
		glDeleteFramebuffers( 1, &fbo );
	fbo   = 0;
	width = height = count = 0;
	format = 0;
}

} // namespace lithophane
