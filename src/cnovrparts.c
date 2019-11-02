// Copyright 2019 <>< Charles Lohr licensable under the MIT/X11 or NewBSD licenses.

#include <cnovrparts.h>
#include <cnovr.h>
#include <cnovrutil.h>
#include <chew.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stb_image.h>
#include <stb_include_custom.h>
#include <cnovrtccinterface.h>
#include <stretchy_buffer.h>

//XXX Overall TODO: Replace more FreeLater's with frees

static void parts_delete_callback( void * tag, void * opaquev )
{
	cnovr_base * b = (cnovr_base*)opaquev;
	b->header->Delete( b );
}

void CNOVRDeleteBase( cnovr_base * b )
{
	if( !b ) return;
	//XXX Tricky: Use -1 tag to prevent accidental task deletion.
	CNOVRJobTack( cnovrQPrerender, parts_delete_callback, (void*)-1, b, 0 );
}


static void CNOVRRenderFrameBufferDelete( cnovr_rf_buffer * ths )
{
	if( ths->nRenderFramebufferId ) glDeleteFramebuffers( 1, &ths->nRenderFramebufferId );
	if( ths->nResolveFramebufferId ) glDeleteFramebuffers( 1, &ths->nResolveFramebufferId );
	if( ths->nResolveTextureId ) glDeleteTextures( 1, &ths->nResolveTextureId );
	if( ths->nDepthBufferId ) glDeleteRenderbuffers( 1, &ths->nDepthBufferId );
	if( ths->nColorBufferId ) glDeleteRenderbuffers( 1, &ths->nColorBufferId );
	if( ths->nRenderTextureId ) glDeleteTextures( 1, &ths->nRenderTextureId );
	CNOVRListDeleteTag( ths );

	CNOVRFreeLater( ths );
}

cnovr_header cnovr_rf_buffer_header = {
	(cnovrfn)CNOVRRenderFrameBufferDelete,
	0, //Render
	TYPE_RFBUFFER,
};

cnovr_rf_buffer * CNOVRRFBufferCreate( int nWidth, int nHeight, int multisample )
{
	cnovr_rf_buffer * ret = malloc( sizeof( cnovr_rf_buffer ) );
	memset( ret, 0, sizeof( *ret ) );
	ret->base.header = &cnovr_rf_buffer_header;
	ret->base.tccctx = TCCGetTag();

	ret->multisample = multisample;

	//XXX TODO: Figure out why we can't use depth buffers with multisamples.
	int texmul = multisample?GL_TEXTURE_2D_MULTISAMPLE:GL_TEXTURE_2D;


	glGenFramebuffers(1, &ret->nRenderFramebufferId );
	glBindFramebuffer(GL_FRAMEBUFFER, ret->nRenderFramebufferId);


	glGenRenderbuffers(1, &ret->nDepthBufferId);
	glBindRenderbuffer(GL_RENDERBUFFER, ret->nDepthBufferId);
	if( multisample )
		glRenderbufferStorageMultisample(GL_RENDERBUFFER, multisample, GL_DEPTH_COMPONENT, nWidth, nHeight );
	else
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, nWidth, nHeight );
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,	ret->nDepthBufferId );



	glGenTextures(1, &ret->nRenderTextureId );
	glBindTexture(texmul, ret->nRenderTextureId );


	glTexParameterf(texmul, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf(texmul, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameterf(texmul, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(texmul, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	//glTexParameteri(texmul, GL_GENERATE_MIPMAP, GL_TRUE);

	if( multisample )
	{
		glTexImage2DMultisample(texmul, multisample, GL_RGBA8, nWidth, nHeight, GL_TRUE);
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nWidth, nHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	}


	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texmul, ret->nRenderTextureId, 0);


//	glGenRenderbuffers(1, &ret->nColorBufferId);
//	glBindRenderbuffer(GL_RENDERBUFFER, ret->nColorBufferId);
//	if( multisample )
//		glRenderbufferStorageMultisample(GL_RENDERBUFFER, multisample, GL_RGBA8, nWidth, nHeight );
//	else
//		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, nWidth, nHeight );
//	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, ret->nColorBufferId );

	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		printf( "Warning bad framebuffer setup (Render)\n" );
	}


	glGenFramebuffers(1, &ret->nResolveFramebufferId );
	glBindFramebuffer(GL_FRAMEBUFFER, ret->nResolveFramebufferId);
	glGenTextures(1, &ret->nResolveTextureId );
	glBindTexture(GL_TEXTURE_2D, ret->nResolveTextureId );
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, nWidth, nHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ret->nResolveTextureId, 0);

	// check FBO status
	status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE)
	{
		printf( "Warning: Bad framebuffersetup (Resolve)\n" );
		CNOVRRenderFrameBufferDelete( ret );
		return 0;
	}

	printf( "Framebuffers set up\n" );

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	//XXX TODO : Remove me.  I think?
	char stbuf[128];
	sprintf( stbuf, "#define MULTISAMPLES %d\n", multisample );
	ret->resolveshader = CNOVRShaderCreateWithPrefix( "resolve", stbuf );
	ret->resolvegeo = CNOVRModelCreate( 0, GL_TRIANGLES );
	CNOVRModelAppendMesh( ret->resolvegeo, 1, 1, 0, (cnovr_point3d){ 1, 1, 0 }, 0, 0 );
	return ret;
}


void CNOVRFBufferActivate( cnovr_rf_buffer * b )
{
	if( CNOVRCheck() ) ovrprintf( "PRE ACTIVAT\n" );
	b->origw = cnovrstate->iRTWidth;
	b->origh = cnovrstate->iRTHeight;
	int w = cnovrstate->iRTWidth = b->width;
	int h = cnovrstate->iRTHeight = b->height;
	if( b->multisample )  glEnable( GL_MULTISAMPLE );
	glBindFramebuffer( GL_FRAMEBUFFER, b->nRenderFramebufferId );
 	glViewport(0, 0, w, h );
	if( CNOVRCheck() ) ovrprintf( "POST ACTIVATE\n" );
}

void CNOVRFBufferDeactivate( cnovr_rf_buffer * b )
{
	if( CNOVRCheck() ) ovrprintf( "PRE DEACTIVATE\n" );
 	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	int w = cnovrstate->iRTWidth = b->origw;
	int h = cnovrstate->iRTHeight = b->origh;
 	glViewport(0, 0, w, h );
	if( CNOVRCheck() ) ovrprintf( "POST DEACTIVATE\n" );
}

void CNOVRFBufferBlitResolve( cnovr_rf_buffer * b )
{
	if( CNOVRCheck() ) ovrprintf( "PRE RESOLVE\n" );

#if 0
	//Broken???!?
	glBindFramebuffer( GL_FRAMEBUFFER, 0);
	if( b->multisample ) glDisable( GL_MULTISAMPLE );
	glBindFramebuffer(GL_READ_FRAMEBUFFER, b->nRenderFramebufferId );
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b->nResolveFramebufferId );
	glBlitFramebuffer(0, 0, b->width, b->height, 0, 0, b->width, b->height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
 	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0 );	
#else

	//glEnable( GL_MULTISAMPLE );
	glBindFramebuffer( GL_FRAMEBUFFER, b->nResolveFramebufferId);
	glActiveTextureCHEW( GL_TEXTURE0 );
	if( b->multisample )
		glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, b->nRenderTextureId );
	else
		glBindTexture(GL_TEXTURE_2D, b->nRenderTextureId );

	if( CNOVRCheck() ) ovrprintf( "MIDDLE RESOLVE\n" );
	CNOVRRender( b->resolveshader );
	if( CNOVRCheck() ) ovrprintf( "MIDDLE1 RESOLVE\n" );
	if( CNOVRCheck() ) ovrprintf( "MIDDLE2 RESOLVE\n" );
	CNOVRRender( b->resolvegeo );
#endif

	glBindFramebuffer( GL_FRAMEBUFFER, 0);

	int w = cnovrstate->iRTWidth = b->origw;
	int h = cnovrstate->iRTHeight = b->origh;
 	glViewport(0, 0, w, h );
	if( CNOVRCheck() ) ovrprintf( "POST RESOLVE\n" );
}



// Just FYI to blit a buffer:
//	glBindFramebuffer(GL_READ_FRAMEBUFFER, leftEyeDesc.m_nRenderFramebufferId);
//	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, leftEyeDesc.m_nResolveFramebufferId );
//	glBlitFramebuffer( 0, 0, m_nRenderWidth, m_nRenderHeight, 0, 0, m_nRenderWidth, m_nRenderHeight, 
//		GL_COLOR_BUFFER_BIT,
// 		GL_LINEAR );
// 	glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
//	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0 );	



//////////////////////////////////////////////////////////////////////////////

#if 0
static void CNOVRShaderFileClearWatchlist( cnovr_shader * s )
{
	struct watchlist * w = s->tempwl;
	while( w )
	{
		struct watchlist * next = w->next;
		free( w );
		w = next;
	}
	s->tempwl = 0;
}
#endif

static void CNOVRShaderDelete( cnovr_shader * ths )
{
	CNOVRFileTimeRemoveTagged( ths, 1 );
	CNOVRListDeleteTag( ths );
	CNOVRJobCancelAllTag( ths, 1 );
	if( ths->nShaderID ) glDeleteProgram( ths->nShaderID );
	if( ths->prefix ) free( ths->prefix );
	//CNOVRShaderFileClearWatchlist( ths );
	CNOVRFreeLater( ths->shaderfilebase );
	CNOVRFreeLater( ths );
}

static GLuint CNOVRShaderCompilePart( cnovr_shader * ths, GLuint shader_type, const char * shadername, const char * compstr )
{
	GLuint nShader = glCreateShader( shader_type );
	glShaderSource( nShader, 1, &compstr, NULL );
	glCompileShader( nShader );
	GLint vShaderCompiled = GL_FALSE;

	glGetShaderiv( nShader, GL_COMPILE_STATUS, &vShaderCompiled );
	if ( vShaderCompiled != GL_TRUE )
	{
		CNOVRAlert( ths->base.tccctx, 1, "Unable to compile shader: %s\n", shadername );
		int retval;
		glGetShaderiv( nShader, GL_INFO_LOG_LENGTH, &retval );
		if ( retval > 1 ) {
			char * log = (char*)malloc( retval );
			glGetShaderInfoLog( nShader, retval, NULL, log );
			CNOVRAlert( ths->base.tccctx, 1, "%s\n", log );
			free( log );
		}

		glDeleteShader( nShader );
		return 0;
	}
	return nShader;
}

static void CNOVRShaderFileChange( void * tag, void * opaquev );

static void CNOVRShaderFileTackInclude( void * opaque, const char * filename )
{
	cnovr_shader * s = (cnovr_shader*)opaque;
	//int fnamelen = strlen( filename );
	CNOVRFileTimeAddWatch( filename, CNOVRShaderFileChange, s, 0 );
	//struct watchlist * new = malloc( sizeof( struct watchlist*) + fnamelen + 9 );
	//memcpy( new->watchfile, filename, fnamelen+1 );
	//new->next = s->tempwl;
	//s->tempwl = new;
}

static void CNOVRShaderFileChangePrerender( void * tag, void * opaquev )
{
	//Re-load shader
	cnovr_shader * ths = (cnovr_shader*)tag;
	//Careful: Need to re-try in case a program is still writing.

	GLuint nGeoShader  = 0;
	GLuint nFragShader = 0;
	GLuint nVertShader = 0;

	char stfbGeo[CNOVR_MAX_PATH];
	char stfbFrag[CNOVR_MAX_PATH];
	char stfbVert[CNOVR_MAX_PATH];
	char * filedataGeo = 0;
	char * filedataFrag = 0;
	char * filedataVert = 0;

	char includeerrors1[256];
	char includeerrors2[256];
	includeerrors1[0] = 0;
	includeerrors2[0] = 0;
	//printf( "THS: %p\n", ths );
	sprintf( stfbGeo, "%s.geo", ths->shaderfilebase );
	char * found = CNOVRFileSearch( stfbGeo ); if( found ) strcpy( stfbGeo, found );
	filedataGeo = stb_include_file( stfbGeo, ths->prefix, "assets", includeerrors1, CNOVRShaderFileTackInclude, tag );
	//XXX TODO: Do we care about odd errors on geo?
	sprintf( stfbFrag, "%s.frag", ths->shaderfilebase );
	found = CNOVRFileSearch( stfbFrag ); if( found ) strcpy( stfbFrag, found );
	filedataFrag = stb_include_file( stfbFrag, ths->prefix, "assets", includeerrors2, CNOVRShaderFileTackInclude, tag );
	sprintf( stfbVert, "%s.vert", ths->shaderfilebase );
	found = CNOVRFileSearch( stfbVert ); if( found ) strcpy( stfbVert, found );
	filedataVert = stb_include_file( stfbVert, ths->prefix, "assets", includeerrors2, CNOVRShaderFileTackInclude, tag );

	if( includeerrors2[0] )
	{
		CNOVRAlert( ths->base.tccctx, 1, "Shader preprocessor errors: %s\n", includeerrors2 );
		return;		
	}

	if( !filedataFrag || !filedataVert )
	{
		CNOVRAlert( ths->base.tccctx, 1, "Unable to open vert/frag in shader: %s\n", ths->shaderfilebase );
		return;
	}

	if( filedataGeo ) nGeoShader = CNOVRShaderCompilePart( ths, GL_GEOMETRY_SHADER, stfbGeo, filedataGeo );
	nFragShader = CNOVRShaderCompilePart( ths, GL_FRAGMENT_SHADER, stfbFrag, filedataFrag );
	nVertShader = CNOVRShaderCompilePart( ths, GL_VERTEX_SHADER, stfbVert, filedataVert );

	if( filedataVert ) free( filedataVert );
	if( filedataFrag ) free( filedataFrag );
	if( filedataGeo ) free( filedataGeo );

	bool compfail = false;
	if( filedataGeo )
	{
		if ( !nGeoShader )
			compfail = true;
	}
	if ( !nVertShader || !nFragShader )
	{
		compfail = true;
	}

	GLuint unProgramID = 0;

	if ( !compfail )
	{
		unProgramID = glCreateProgram();
		if( filedataGeo )
		{
			glAttachShader( unProgramID, nGeoShader );
		}
		glAttachShader( unProgramID, nFragShader );
		glAttachShader( unProgramID, nVertShader );
		glLinkProgram( unProgramID );

		GLint programSuccess = GL_TRUE;
		glGetProgramiv( unProgramID, GL_LINK_STATUS, &programSuccess );
		if ( programSuccess != GL_TRUE )
		{
			CNOVRAlert( ths->base.tccctx, 1, "Shader linking failed: %s\n", ths->shaderfilebase );
			int retval;
			glGetShaderiv( unProgramID, GL_INFO_LOG_LENGTH, &retval );
			if ( retval > 1 ) {
				char * log = (char*)malloc( retval );
				glGetProgramInfoLog( unProgramID, retval, NULL, log );
				CNOVRAlert( ths->base.tccctx, 1, "%s\n", log );
				free( log );
			}
			else
			{
				CNOVRAlert( ths->base.tccctx, 1, "No message\n" );
			}
			glDeleteProgram( unProgramID );
			unProgramID = 0;
			compfail = true;
		}
	}
	else
	{
		CNOVRAlert( ths->base.tccctx, 1, "Shader compilation failed: %s\n", ths->shaderfilebase );
	}

	if ( unProgramID )
	{
		if ( ths->nShaderID )
		{
			//CNOVRAlert( ths->base.tccctx, 3, "Compile OK: [%p] %s\n", ths, ths->shaderfilebase );
			//Note: If we got here, we were successful. 
			glDeleteProgram( ths->nShaderID );
		}
		ths->nShaderID = unProgramID;
	}

	if( nGeoShader )  glDeleteShader( nGeoShader );
	if( nFragShader ) glDeleteShader( nFragShader );
	if( nVertShader ) glDeleteShader( nVertShader );
}

static void CNOVRShaderFileChange( void * tag, void * opaquev )
{
	//printf( "CNOVRShaderFileChange (%p %p)\n", tag, opaquev );
	//0 = don't recompile if a recompile operation is already pending.
	CNOVRJobTack( cnovrQPrerender, CNOVRShaderFileChangePrerender, tag, opaquev, 0 );
}

static void CNOVRShaderRender( cnovr_shader * ths )
{
	int shdid = ths->nShaderID;
	if( !shdid ) return;
	glUseProgram( shdid );
	CNOVRShaderLoadedSetUniformsInternal();
}

cnovr_header cnovr_shader_header = {
	(cnovrfn)CNOVRShaderDelete, //Delete
	(cnovrfn)CNOVRShaderRender, //Render
	TYPE_SHADER,
};

cnovr_shader * CNOVRShaderCreate( const char * shaderfilebase )
{
	return CNOVRShaderCreateWithPrefix( shaderfilebase, 0 );
}

cnovr_shader * CNOVRShaderCreateWithPrefix( const char * shaderfilebase, const char * prefix )
{
	cnovr_shader * ret = malloc( sizeof( cnovr_shader ) );
	memset( ret, 0, sizeof( *ret ) );
	ret->base.header = &cnovr_shader_header;
	ret->base.tccctx = TCCGetTag();
	ret->shaderfilebase = strdup( shaderfilebase );
	ret->prefix = prefix?strdup(prefix):0;

	char stfb[CNOVR_MAX_PATH];
	sprintf( stfb, "%s.geo", shaderfilebase );
	char * found = CNOVRFileSearch( stfb );
	CNOVRFileTimeAddWatch( found, CNOVRShaderFileChange, ret, 0 );
	sprintf( stfb, "%s.frag", shaderfilebase );
	found = CNOVRFileSearch( stfb );
	CNOVRFileTimeAddWatch( stfb, CNOVRShaderFileChange, ret, 0 );
	sprintf( stfb, "%s.vert", shaderfilebase );
	found = CNOVRFileSearch( stfb );
	CNOVRFileTimeAddWatch( stfb, CNOVRShaderFileChange, ret, 0 );
	CNOVRShaderFileChange( ret, 0 );

	return ret;
}


//////////////////////////////////////////////////////////////////////////////

static void CNOVRTextureLoadFileTask( void * tag, void * opaquev )
{
	cnovr_texture * t = (cnovr_texture*)tag;
	OGLockMutex( t->mutProtect );
	char * localfn = strdup( t->texfile );
	OGUnlockMutex( t->mutProtect );

	int x, y, chan;
	const char * ffn = CNOVRFileSearch( localfn );
	chan = 4;
	x = 0;
	y = 0;
	stbi_uc * data = stbi_load( ffn, &x, &y, &chan, 4 );
	chan = 4;	//XXX UGH No idea why stbi_load does weird stuff if we don't specifically request 4.
	CNOVRFileTimeAddWatch( ffn, CNOVRTextureLoadFileTask, tag, 0 );
	free( localfn );
	if( data )
	{
		CNOVRTextureLoadDataAsync( t, x, y, chan, 0, data );
		t->bLoading = 0;
	}
	else
	{
		ovrprintf( "WARNING: stbi_load( %s, ... ) failed. Is it saving? Trying again.\n", ffn );
		CNOVRJobCancel( cnovrQAsync, CNOVRTextureLoadFileTask, t, 0, 0 );
		CNOVRJobTack( cnovrQAsync, CNOVRTextureLoadFileTask, t, 0, 1 ); //If one's already going, let it finish.

	}

}

static void CNOVRTextureUploadCallback( void * vths, void * dump )
{
	cnovr_texture * t = (cnovr_texture*)vths;
	OGLockMutex( t->mutProtect );
	t->bTaintData = 0;

	if( !t->nTextureId )
	{
		glGenTextures( 1, &t->nTextureId );
		glBindTexture( GL_TEXTURE_2D, t->nTextureId );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	}


	glBindTexture( GL_TEXTURE_2D, t->nTextureId );

	glTexImage2D( GL_TEXTURE_2D,
		0,
		t->nInternalFormat,
		t->width,
		t->height,
		0,
		t->nFormat,
		t->nType,
		t->data );

	if( t->bCalculateMipMaps )
	{
		glGenerateMipmapCHEW(GL_TEXTURE_2D);
	}
	else
	{
		//
	}
	glBindTexture( GL_TEXTURE_2D, 0 );

	OGUnlockMutex( t->mutProtect );
}

static void CNOVRTextureDelete( cnovr_texture * ths )
{
	OGLockMutex( ths->mutProtect );
	CNOVRListDeleteTag( ths );
	//In case any file changes are being watched.
	CNOVRFileTimeRemoveTagged( ths, 1 );
	CNOVRJobCancelAllTag( ths, 1 );
	if( ths->nTextureId )
	{
		glDeleteTextures( 1, &ths->nTextureId );
	}

	if( ths->data ) free( ths->data );
	if( ths->texfile ) free( ths->texfile );
	OGDeleteMutex( ths->mutProtect );
	CNOVRFreeLater( ths );
}

static void CNOVRTextureRender( cnovr_texture * ths )
{
	glBindTexture( GL_TEXTURE_2D, ths->nTextureId );
}


cnovr_header cnovr_texture_header = {
	(cnovrfn)CNOVRTextureDelete, //Delete
	(cnovrfn)CNOVRTextureRender, //Render
	TYPE_TEXTURE,
};

//Defaults to a 1x1 px texture.
cnovr_texture * CNOVRTextureCreate( int initw, int inith, int initchan )
{
	cnovr_texture * ret = malloc( sizeof( cnovr_texture ) );
	memset( ret, 0, sizeof( cnovr_texture ) );
	ret->base.header = &cnovr_texture_header;
	ret->base.tccctx = TCCGetTag();
	ret->texfile = 0;

	ret->mutProtect = OGCreateMutex();

	ret->nTextureId = 0;

	ret->width = 1;
	ret->height = 1;
	ret->channels = 4;
	ret->data = malloc( 4 );
	ret->bTaintData = 0;
	ret->bLoading = 0;
	ret->bFileChangeFlag = 0;
	memset( ret->data, 255, 4 );


	return ret;
}

int CNOVRTextureLoadFileAsync( cnovr_texture * tex, const char * texfile )
{
	OGLockMutex( tex->mutProtect );
	if( tex->texfile ) free( tex->texfile );
	tex->texfile = strdup( texfile );
	tex->bLoading = 1;
	CNOVRJobCancel( cnovrQAsync, CNOVRTextureLoadFileTask, tex, 0, 0 );
	CNOVRJobTack( cnovrQAsync, CNOVRTextureLoadFileTask, tex, 0, 1 ); //If one's already going, let it finish.
	OGUnlockMutex( tex->mutProtect );
	return 0;
}

static void InternalCNOVRTextureLoadSetup(  cnovr_texture * tex, int w, int h, int chan, int is_float )
{
	tex->width = w;
	tex->height = h;
	tex->channels = chan;
	static const int channelmapF[] = { 0, GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F };
	static const int channelmapI[] = { 0, GL_R8, GL_RG8, GL_RGB8, GL_RGBA8 };
	static const int channelmapB[] = { 0, GL_RED, GL_RG, GL_RGB, GL_RGBA };

	if( is_float )
		tex->nInternalFormat = channelmapF[chan];
	else
		tex->nInternalFormat = channelmapI[chan];
	tex->nFormat = channelmapB[chan];
	tex->nType = is_float?GL_FLOAT:GL_UNSIGNED_BYTE;
	tex->bTaintData = 1;
}

int CNOVRTextureLoadDataNow( cnovr_texture * tex, int w, int h, int chan, int is_float, void * data, int data_permanant )
{
	OGLockMutex( tex->mutProtect );
	InternalCNOVRTextureLoadSetup( tex, w, h, chan, is_float );

	if( tex->data ) free( tex->data );
	tex->data = data;

	CNOVRTextureUploadCallback( tex, 0 );

	//If data is permanant, we don't have to worry about deleting it.
	if( data_permanant )
	{
		tex->data = 0;
	}

	OGUnlockMutex( tex->mutProtect );
	return 0;
}

int CNOVRTextureLoadDataAsync( cnovr_texture * tex, int w, int h, int chan, int is_float, void * data )
{
	OGLockMutex( tex->mutProtect );

	//We don't want to confuse the second part of the loader.
	CNOVRJobCancel( cnovrQPrerender, CNOVRTextureUploadCallback, tex, 0, 1 );

	if( tex->data && data != tex->data ) free( tex->data );
	tex->data = data;

	InternalCNOVRTextureLoadSetup( tex, w, h, chan, is_float );

	CNOVRJobTack( cnovrQPrerender, CNOVRTextureUploadCallback, tex, 0, 1 );
	OGUnlockMutex( tex->mutProtect );
	return 0;
}



///////////////////////////////////////////////////////////////////////////////

cnovr_vbo * CNOVRCreateVBO( int iStride, int bDynamic, int iInitialSize, int iAttribNo )
{
	cnovr_vbo * ret = malloc( sizeof( cnovr_vbo ) );
	memset( ret, 0, sizeof( cnovr_vbo ) );
	ret->iVertexCount = iInitialSize;

	if( iInitialSize < 1 ) iInitialSize = 1;
	ret->pVertices = malloc( iInitialSize * sizeof(float) * iStride );
	ret->iStride = iStride;
	ret->bDynamic = bDynamic;
	ret->mutData = OGCreateMutex();
	ret->nVBO = 0;

	return ret;
}


void CNOVRVBOTackv( cnovr_vbo * g, int nverts, float * v )
{
	OGLockMutex( g->mutData );
	int stride = g->iStride;
	g->pVertices = realloc( g->pVertices, (g->iVertexCount+1)*stride*sizeof(float) );
	float * verts = g->pVertices + (g->iVertexCount*stride);
	int tocopy = nverts;
	if( nverts > stride ) tocopy = stride;
	int i;
	for( i = 0; i < tocopy; i++ )
	{
		verts[i] = v[i];
	}
	for( ; i < stride; i++ )
	{
		verts[i] = 0;
	}
	g->iVertexCount++;
	OGUnlockMutex( g->mutData );
}

void CNOVRVBOTack( cnovr_vbo * g,  int nverts, ... )
{
	OGLockMutex( g->mutData );
	int stride = g->iStride;
	g->pVertices = realloc( g->pVertices, (g->iVertexCount+1)*stride*sizeof(float) );
	float * verts = g->pVertices + (g->iVertexCount*stride);
	va_list argp;
	va_start( argp, nverts );
	int i;
	if( nverts > stride ) nverts = stride;
	for( i = 0; i < nverts; i++ )
	{
		verts[i] = va_arg(argp, double);
	}
	for( ; i < stride; i++ )
	{
		verts[i] = 0.0;
	}
	g->iVertexCount++;
	va_end( argp );
	OGUnlockMutex( g->mutData );
}

static void CNOVRVBOPerformUpload( void * gv, void * dump )
{
	cnovr_vbo * g = (cnovr_vbo *)gv;

	OGLockMutex( g->mutData );

	if( !g->nVBO ) glGenBuffers( 1, &g->nVBO );

	//This happens from within the render thread
	glBindBuffer( GL_ARRAY_BUFFER, g->nVBO );

	//XXX TODO: Consider streaming the data.
	glBufferData( GL_ARRAY_BUFFER, g->iStride*sizeof(float)*g->iVertexCount, g->pVertices, g->bDynamic?GL_DYNAMIC_DRAW:GL_STATIC_DRAW);
	glVertexPointer( g->iStride, GL_FLOAT, 0, 0);
	//glVertexAttribPointer( 0, g->iStride, GL_FLOAT, 0, g->iStride, g->pVertices );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	g->bIsUploaded = 1;

#if 0
	int i;
	for( i = 0; i < g->iVertexCount; i++ )
	{
		int j;
		for( j = 0; j < g->iStride; j++ )
		{
			printf( "%f ", g->pVertices[j+i*g->iStride] );
		}
		printf( "\n" );
	}
#endif
	if( CNOVRCheck() ) ovrprintf( "Problem in VBO Upload check\n" );

	OGUnlockMutex( g->mutData );
}

void CNOVRVBOTaint( cnovr_vbo * g )
{
	CNOVRJobCancel( cnovrQPrerender, CNOVRVBOPerformUpload, (void*)g, 0, 0 );
	CNOVRJobTack( cnovrQPrerender, CNOVRVBOPerformUpload, (void*)g, 0, 1 );
}

void CNOVRVBODelete( cnovr_vbo * g )
{
	//Not a normal object.
	CNOVRJobCancelAllTag( (void*)g, 1 );
	OGLockMutex( g->mutData );
	if( g->nVBO ) glDeleteBuffers( 1, &g->nVBO );
	CNOVRFreeLater( g->pVertices );
	OGDeleteMutex( g->mutData );
	CNOVRFreeLater( g );
}

void CNOVRVBOSetStride( cnovr_vbo * g, int stride )
{
	g->iStride = stride;
}

///////////////////////////////////////////////////////////////////////////////

static void CNOVRModelUpdateIBO( void * vm, void * dump )
{
//	printf( "START MODEL UPATE\n" );
	cnovr_model * m = (cnovr_model *)vm;
	OGLockMutex( m->model_mutex );
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->nIBO);
//	printf( "Updating IBO: %d\n", m->iIndexCount );
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(m->pIndices[0])*m->iIndexCount, m->pIndices, GL_STATIC_DRAW);	//XXX TODO Make this tunable.
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	OGUnlockMutex( m->model_mutex );
	m->bIsUploaded = 1;
//	printf( "STOP MODEL UPDATE\n" );
#if 0
	printf( "### IBO UPATE INDICES: %ld\n", sizeof(m->pIndices[0])*m->iIndexCount );
	int i;
//	for( i = 0; i < m->iIndexCount ; i++ )
//	{
//		printf( "%d\n", m->pIndices[i] );
//	}
#endif
}

void CNOVRModelTaintIndices( cnovr_model * vm )
{
	CNOVRJobTack( cnovrQPrerender, CNOVRModelUpdateIBO, (void*)vm, 0, 1 );	
}

static void CNOVRModelDelete( cnovr_model * m )
{
	OGLockMutex( m->model_mutex );
	CNOVRListDeleteTag( m );
	CNOVRJobCancelAllTag( (void*)m, 1 );
	int i;
	for( i = 0; i < m->iGeos; i++ )
	{
		CNOVRVBODelete( m->pGeos[i] );
	}
	CNOVRFreeLater( m->pGeos );
	if( m->sMeshMarks )
	{
		for( i = 0; i < m->nMeshes; i++ )
		{
			if( m->sMeshMarks[i] ) free( m->sMeshMarks[i] );
		}
		free( m->sMeshMarks );
	}
	if( m->iMeshMarks )
	{
		free( m->iMeshMarks );
	}
	for( i = 0; i < m->iTextures; i++ )
	{
		CNOVRDelete( m->pTextures[i] );
	}


	CNOVRFreeLater( m->pIndices );
	if( m->geofile ) CNOVRFreeLater( m->geofile );
	glDeleteBuffers( 1, &m->nIBO );
	OGDeleteMutex( m->model_mutex );
	CNOVRFreeLater( m );
}


static void CNOVRModelRender( cnovr_model * m )
{
	static cnovr_model * last_rendered_model = 0;

	cnovr_pose * pose;

	if( m->pose )
	{
		pose_to_matrix44( cnovrstate->mModel, m->pose );
		glUniformMatrix4fv( UNIFORMSLOT_MODEL, 1, 1, cnovrstate->mModel );
	}

	if( !m->bIsUploaded ) return;
	//XXX Tricky: Don't lock model, so if we're loading while rendering, we don't hitch.
//	if( m != last_rendered_model )
	{
		//Try binding any textures.
		int i;
		int count = m->iTextures;
		cnovr_texture ** ts = m->pTextures;
		if( ts )
		{
			for( i = 0; i < count; i++ )
			{
				glActiveTextureCHEW( GL_TEXTURE0 + i );
				cnovr_texture * t = ts[i];
				CNOVRRender( t );
			}
		}

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m->nIBO );

		count = m->iGeos;
		for( i = 0; i < count; i++ )
		{
			if( !m->pGeos[i]->bIsUploaded ) continue;
			glBindBuffer( GL_ARRAY_BUFFER, m->pGeos[i]->nVBO );
			//glVertexPointer( m->pGeos[i]->iStride, GL_FLOAT, m->pGeos[i]->iStride, 0);    // last param is offset, not ptr
			glVertexAttribPointer( i, m->pGeos[i]->iStride, GL_FLOAT, GL_FALSE, m->pGeos[i]->iStride*4, 0  );
		}
	}

//	glEnableClientState(GL_VERTEX_ARRAY);
	glDrawElements( m->nRenderType, m->iIndexCount, GL_UNSIGNED_INT, 0 );
//	glDisableClientState(GL_VERTEX_ARRAY);            // deactivate vertex position array
//	glBindBuffer( GL_ARRAY_BUFFER, 0 );
//	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,  0);
}


cnovr_header cnovr_model_header = {
	(cnovrfn)CNOVRModelDelete, //Delete
	(cnovrfn)CNOVRModelRender, //Render
	TYPE_MODEL,
};


cnovr_model * CNOVRModelCreate( int initial_indices, int rendertype )
{
	cnovr_model * ret = malloc( sizeof( cnovr_model ) );
	memset( ret, 0, sizeof( cnovr_model ) );
	ret->base.header = &cnovr_model_header;
	ret->base.tccctx = TCCGetTag();

	glGenBuffers( 1, &ret->nIBO );
	ret->iIndexCount = initial_indices;
	if( initial_indices < 1 ) initial_indices = 1;
	ret->pIndices = malloc( initial_indices * sizeof( GLuint ) );
	ret->nRenderType = rendertype;
	ret->iMeshMarks = malloc( sizeof( int ) );
	ret->iMeshMarks[0] = 0;
	ret->nMeshes = 1;
	ret->sMeshMarks = 0;

	ret->pGeos = malloc( sizeof( cnovr_vbo * ) * 1 );
	ret->iGeos = 0;
	ret->pTextures = 0;
	ret->iTextures = 0;
	ret->geofile = 0;

	ret->bIsLoading = 0;
	ret->iLastVertMark = 0;
	ret->model_mutex = OGCreateMutex(); 
	return ret;
}

void CNOVRModelSetNumVBOsWithStrides( cnovr_model * m, int vbos, ... )
{
	int i;
	va_list argp;
	va_start( argp, vbos );

	for( i = 0; i < m->iGeos; i++ )
	{
		CNOVRVBODelete( m->pGeos[i] );
	}
	m->pGeos = realloc( m->pGeos, sizeof( cnovr_vbo * ) * vbos );
	m->iGeos = vbos;

	for( i = 0; i < vbos; i++ )
	{
		int stride = va_arg(argp, int);
		m->pGeos[i] = CNOVRCreateVBO( stride, 0, 0, i );
	}
	va_end( argp );
}

void CNOVRModelSetNumVBOs( cnovr_model * m, int vbos )
{
	int i;
	for( i = 0; i < m->iGeos; i++ )
	{
		CNOVRVBODelete( m->pGeos[i] );
	}
	m->pGeos = realloc( m->pGeos, sizeof( cnovr_vbo * ) * vbos );
	m->iGeos = vbos;

	for( i = 0; i < vbos; i++ )
	{
		m->pGeos[i] = CNOVRCreateVBO( 4, 0, 0, i );
	}
}

void CNOVRModelSetNumIndices( cnovr_model * m, uint32_t indices )
{
	m->iIndexCount = indices;
	m->pIndices = realloc( m->pIndices, sizeof( GLuint ) * indices );
}

void CNOVRModelResetMarks( cnovr_model * m )
{
	int i;
	for( i = 0; i < m->nMeshes; i++ )
	{
		if( m->sMeshMarks && m->sMeshMarks[i] ) free( m->sMeshMarks[i] );
	}
	m->sMeshMarks = realloc( m->sMeshMarks, sizeof( char * ) );
	m->iMeshMarks = realloc( m->iMeshMarks, sizeof( uint32_t ) );
	m->nMeshes = 1;
	m->iMeshMarks[0] = 0;
	m->sMeshMarks[0] = 0;
	m->iLastVertMark = 0;
}

void CNOVRDelinateGeometry( cnovr_model * m, const char * sectionname )
{
	if( m->iIndexCount == 0 )
	{
		m->nMeshes = 1;
	}
	else
	{
		m->nMeshes++;
	}
	m->iMeshMarks = realloc( m->iMeshMarks, sizeof( uint32_t ) * ( m->nMeshes + 1 ) );
	m->sMeshMarks = realloc( m->sMeshMarks, sizeof( char * ) * ( m->nMeshes + 1 ) );
	m->iMeshMarks[m->nMeshes-1] = m->iIndexCount;
	m->sMeshMarks[m->nMeshes-1] = strdup( sectionname );
}


void CNOVRModelTackIndex( cnovr_model * m, int nindices, ...)
{
	int iIndexCount = m->iIndexCount;
	m->pIndices = realloc( m->pIndices, (iIndexCount + nindices) * sizeof( m->pIndices[0] ) );
	uint32_t * pIndices = m->pIndices + iIndexCount;
	int i;
	va_list argp;
	va_start( argp, nindices );
	for( i = 0; i < nindices; i++ )
	{
		pIndices[i] = va_arg(argp, int);
	}
	va_end( argp );
	m->iIndexCount = m->iIndexCount + nindices;
}

void CNOVRModelTackIndexv( cnovr_model * m, int nindices, uint32_t * indices )
{
	int iIndexCount = m->iIndexCount;
	m->pIndices = realloc( m->pIndices, ( iIndexCount + nindices ) * sizeof( m->pIndices[0] ) );
	uint32_t * pIndices = m->pIndices + iIndexCount;
	int i;
	for( i = 0; i < nindices; i++ )
	{
		pIndices[i] = indices[i];
	}
	m->iIndexCount = m->iIndexCount + nindices;
}

void CNOVRModelAppendCube( cnovr_model * m, cnovr_point3d size, cnovr_pose * poseofs_optional, cnovr_point4d * extradata )
{
	cnovr_pose * pose = poseofs_optional?poseofs_optional:&cnovr_pose_identity;

	//Bit pattern. 0 means -1, 1 means +1 on position.
	//n[face] n[+-](inverted)  v1[xyz] v2[xyz] v3[xyz];; TC can be computed from table based on N
	//XXX TODO: Check texture coord correctness.
	static const uint16_t trideets[] = {
		0b000000001011,
		0b000000011010,
		0b001111100110,
		0b001100111101,
		0b010101000100,
		0b010101001000,
		0b011111110010,
		0b011111010011,
		0b100110100000,
		0b100110000010,
		0b101011001101,
		0b101111011101,
	};
	OGLockMutex( m->model_mutex );
	if( m->iGeos != 4 )
	{
		CNOVRModelSetNumVBOsWithStrides( m, 4, 3, 3, 3, 4 );
		CNOVRModelSetNumIndices( m, 0 );
		CNOVRModelResetMarks( m );
	}
	CNOVRDelinateGeometry( m, "cube" );

	int i;
	{
		uint32_t * indices = m->pIndices;
		for( i = 0; i < 36; i++ ) 
		{
			CNOVRModelTackIndex( m, 1, i + m->iLastVertMark );
		}
	}

	{
		//float * points = m->pGeos[0]->pVertices;
		//float * texcoord = m->pGeos[1]->pVertices;
		//float * normals = m->pGeos[2]->pVertices;

		for( i = 0; i < 12; i++ )
		{
			uint16_t key = trideets[i];
			int normaxis = (key>>10)&3;
			float normplus = ((key>>9)&1)?1:-1;
			int tcaxis1 = (normaxis+1)%3;
			int tcaxis2 = (tcaxis1+1)%3;
			int vkeys[3] = { 
				(key >> 6)&7,
				(key >> 3)&7,
				(key >> 0)&7 };
			int j;
			for( j = 0; j < 3; j++ )
			{
				float stage[4];
				float staget[4];
				{
					float xyzin[3] = { (vkeys[j]&4)?1:-1, (vkeys[j]&2)?1:-1, (vkeys[j]&1)?1:-1 };
					mult3d( xyzin, xyzin, size );
					apply_pose_to_point( stage, pose, xyzin );
				}
				stage[3] = 1;
				CNOVRVBOTackv( m->pGeos[0], 3, stage );

				staget[0] = stage[tcaxis1] * 0.5 + 0.5;
				staget[1] = stage[tcaxis2] * 0.5 + 0.5;
				staget[2] = m->nMeshes;

				CNOVRVBOTackv( m->pGeos[1], 3, staget );

				stage[0] = (normaxis==0)?normplus:0;
				stage[1] = (normaxis==1)?normplus:0;
				stage[2] = (normaxis==2)?normplus:0;
				stage[3] = 0;
				CNOVRVBOTackv( m->pGeos[2], 3, stage );

				if( extradata )
					CNOVRVBOTackv( m->pGeos[3], 4, *extradata );
				else
					CNOVRVBOTackv( m->pGeos[3], 4, (float[4]){ 0, 0, 0, 0 } );
			}
		}
	}

	m->iLastVertMark += 36;

	CNOVRVBOTaint( m->pGeos[0] );
	CNOVRVBOTaint( m->pGeos[1] );
	CNOVRVBOTaint( m->pGeos[2] );
	CNOVRVBOTaint( m->pGeos[3] );
	CNOVRModelTaintIndices( m );

	OGUnlockMutex( m->model_mutex );
}

void CNOVRModelAppendMesh( cnovr_model * m, int rows, int cols, int flipv, cnovr_point3d size, cnovr_pose * poseofs_optional, cnovr_point4d * extradata )
{
	//Copied from Spreadgine.
	int i;
	int x, y;
//	int c = w * h;
//	int v = (w+1)*(h+1);

	OGLockMutex( m->model_mutex );

	if( m->iGeos != 4 )
	{
		CNOVRModelSetNumVBOsWithStrides( m, 4, 3, 3, 3, 4  );
		CNOVRModelSetNumIndices( m, 0 );
		CNOVRModelResetMarks( m );
	}

	CNOVRDelinateGeometry( m, "mesh" );

	{
		uint32_t * indices = m->pIndices;
		for( y = 0; y < rows; y++ )
		for( x = 0; x < cols; x++ )
		{
			int k = m->iLastVertMark;
			//printf( "MARK: %d\n", k );
			CNOVRModelTackIndex( m, 6, 
				k + x + y * (cols+1),
				k + (x+1) + y * (cols+1),
				k + (x+1) + (y+1) * (cols+1),
				k + (x) + (y) * (cols+1),
				k + (x+1) + (y+1) * (cols+1),
				k + (x) + (y+1) * (cols+1) );
		}
	}

	{
		float stage[4];
		float stagen[4];

		stagen[0] = 0;
		stagen[1] = 0;
		stagen[2] = -1;
		stagen[3] = m->nMeshes;

		for( y = 0; y <= rows; y++ )
		for( x = 0; x <= cols; x++ )
		{
			stage[0] = x/(float)rows;
			stage[1] = y/(float)cols;
			if( flipv ) stage[1] = 1.0 - stage[1];
			stage[2] = m->nMeshes;
			CNOVRVBOTackv( m->pGeos[1], 3, stage );	//TC
			stage[1] = y/(float)cols;
			stage[0] = (stage[0] - 0.5)*2.0 * size[0];
			stage[1] = (stage[1] - 0.5)*2.0 * size[1];
			stage[2] = size[2];
			if( poseofs_optional )
				apply_pose_to_point( stage, poseofs_optional, stage );
			CNOVRVBOTackv( m->pGeos[0], 3, stage );	//Pos
			CNOVRVBOTackv( m->pGeos[2], 3, stagen );

			if( extradata )
				CNOVRVBOTackv( m->pGeos[3], 4, *extradata );
			else
				CNOVRVBOTackv( m->pGeos[3], 4, (float[4]){ 0, 0, 0, 0 } );
		}
	}

	m->iLastVertMark += (rows+1)*(cols+1);

	CNOVRVBOTaint( m->pGeos[0] );
	CNOVRVBOTaint( m->pGeos[1] );
	CNOVRVBOTaint( m->pGeos[2] );
	CNOVRVBOTaint( m->pGeos[3] );
	CNOVRModelTaintIndices( m );

	OGUnlockMutex( m->model_mutex );
}

void CNOVRModelApplyTextureFromFileAsync( cnovr_model * m, const char * sTextureFile )
{
	if( m->iTextures == 0 )
	{
		if( !m->pTextures ) m->pTextures = malloc( sizeof( cnovr_texture * ) );
		m->pTextures[0] = CNOVRTextureCreate( 1, 1, 4 );
		m->iTextures = 1;
	}
	CNOVRTextureLoadFileAsync( m->pTextures[0], sTextureFile );
}

void CNOVRModelSetNumTextures( cnovr_model * m, int textures )
{
	int i;
	for( i = textures; i < m->iTextures; i++ )
		CNOVRDelete( m->pTextures[i] );

	m->pTextures = realloc( m->pTextures, textures * sizeof( cnovr_texture * ) );

	for( i = m->iTextures; i < textures; i++ )
		m->pTextures[i] = CNOVRTextureCreate( 1, 1, 4 );

	m->iTextures = 1;
}

void CNOVRModelRenderWithPose( cnovr_model * m, cnovr_pose * pose )
{
	pose_to_matrix44( cnovrstate->mModel, pose );
	glUniformMatrix4fv( UNIFORMSLOT_MODEL, 1, 1, cnovrstate->mModel );
	m->base.header->Render( (cnovr_base*)m );
}

int  CNOVRModelCollide( cnovr_model * m, const cnovr_point3d start, const cnovr_vec3d direction, cnovr_collide_results * r )
{
	int ret = -1;
	if( m->iGeos == 0 ) return -1;
	int iMeshNo = 0;
	//Iterate through all this.
	if( !m->pGeos[0] ) return -1;
	float * vpos = m->pGeos[0]->pVertices;
	int stride = m->pGeos[0]->iStride;
	int i;
	GLuint * indices = m->pIndices;
	for( i = 0; i < m->nMeshes; i++ )
	{
		int meshStart = m->iMeshMarks[i];
		int meshEnd = (i == m->nMeshes-1 ) ? m->iIndexCount : m->iMeshMarks[i+1];
		int j;
		for( j = meshStart; j < meshEnd; j+=3 )
		{
			int i0 = indices[j+0];
			int i1 = indices[j+1];
			int i2 = indices[j+2];
			//i0..2 are the indices we will be verting.
			float * v0 = &vpos[i0*stride];
			float * v1 = &vpos[i1*stride];
			float * v2 = &vpos[i2*stride];

			float v10[3];
			float v21[3];
			float v02[3];
			float N[3];
			sub3d( v10, v1, v0 );
			sub3d( v21, v2, v1 );
			sub3d( v02, v0, v2 );
			{
				float v20[3];
				sub3d( v20, v2, v0 ); //!?!?!
				cross3d( N, v20, v10 );
			}
			float Ax2 = magnitude3d( N );
			scale3d( N, N, 1/Ax2 );
			//normalize3d( N, N );
			float D = dot3d(N, v0);
			float t = -( dot3d( N, start ) + D) / dot3d( N, direction ); 
			float Phit[3];
			scale3d( Phit, direction, t );
			add3d( Phit, Phit, start );
			float C0[3];
			float C1[3];
			float C2[3];

			{
				float C0tmp[3];
				float C1tmp[3];
				float C2tmp[3];
				sub3d( C0tmp, Phit, v0 );
				sub3d( C1tmp, Phit, v1 );
				sub3d( C2tmp, Phit, v2 );
				cross3d( C0, C0tmp, v10 );
				cross3d( C1, C1tmp, v21 );
				cross3d( C2, C2tmp, v02 );
			}

			//C1-2 are busted (?) here. Their magnitude is weird.  Almost like it's squared or something?
			float t0 = dot3d( N, C0 );
			float t1 = dot3d( N, C1 );
			float t2 = dot3d( N, C2 );

//			printf( "C0: %f %f %f  C1: %f %f %f   C2: %f %f %f   N: %f %f %f  %f\n", PFTHREE( C0 ), PFTHREE( C1 ), PFTHREE( C2 ), PFTHREE( N ), t );
			if( t0 < 0 ||
				t1 < 0 ||
				t2 < 0 ) continue;


			//Else: We have a hit.  This doesn't happen for all that many polys, so time isn't as critical here.
			if( t < r->t )
			{
				r->t = t;
				r->whichmesh = i;
				r->whichvert = j;
				copy3d( r->collidepos, Phit );

				if( m->iGeos > 1 )
				{
					t0/=Ax2;
					t1/=Ax2;
					t2/=Ax2;

					//{ t0, t1, t2 } are the barycentric coordinates. 
					int stride1 = m->pGeos[1]->iStride;
					float * vd1 = m->pGeos[1]->pVertices;
					float * tx0 = vd1 + i0 * stride1;
					float * tx1 = vd1 + i1 * stride1;
					float * tx2 = vd1 + i2 * stride1;
					float * txo = r->collidevs;
					int j;
					for( j = 0; j < stride1; j++ )
					{
						txo[j] = tx0[j] * t1 + tx1[j] * t2 + tx2[j] * t0;
					}
					for( ; j < sizeof(r->collidevs) / sizeof(r->collidevs[0]); j++ )
					{
						txo[j] = 0;
					}
					//printf( "%f * (%f %f %f)  %f * (%f %f %f)  %f * (%f %f %f)\n", t1, PFTHREE( tx0 ), t2, PFTHREE( tx1 ), t0, PFTHREE( tx2 ) );
				}
				ret = i;
			}
		}
	}
	return ret;
}

///////////////////////////////////////////////////////////////////////////////
//Model Loaders

#define OBJBUFFERSIZE 256

///
/// OBJ File Loader (From Spreadgine)
///

#define TBUFFERSIZE 256
#define VBUFFERSIZE 524288

struct TempObject
{
	int     CVertCount;
	float * CVerts;
	int     CNormalCount;
	float * CNormals;
	int     CTexCount;
	float * CTexs;
};


static void CNOVRModelLoadOBJ( cnovr_model * m, const char * filename )
{
	int filelen;
	char * file = CNOVRFileToString( filename, &filelen );
	char ** splits = CNOVRSplitStrings( file, "\n", "\r", 1, 0 );
	free( file );

	int flipv = 1;

	struct TempObject t;
	memset( &t, 0, sizeof( t ) );
	if( m->iGeos != 3 )
	{
		CNOVRModelSetNumVBOsWithStrides( m, 3, 3, 4, 3 );
		CNOVRModelSetNumIndices( m, 0 );
		CNOVRModelResetMarks( m );
	}
	CNOVRModelSetNumIndices( m, 0 );

	int indices = 0;
	char * line;
	int lineno;
	int nObjNo = 0;
	for( lineno = 0; (line = splits[lineno]) ; lineno++ )
	{
		int linelen = strlen( line );
		if( linelen < 2 ) continue;
		if( tolower( line[0] ) == 'v' )
		{
			if( tolower( line[1] ) == 'n' )
			{
				t.CNormals = realloc( t.CNormals, ( t.CNormalCount + 1 ) * 3 * sizeof( float ) );
				int r = sscanf( line + 3, "%f %f %f", 
					&t.CNormals[0 + t.CNormalCount * 3], 
					&t.CNormals[1 + t.CNormalCount * 3], 
					&t.CNormals[2 + t.CNormalCount * 3] );
				t.CNormals[3 + t.CNormalCount * 3] = 0;
				if( r == 3 )
					t.CNormalCount++;
			}
			else if( tolower( line[1] ) == 't' )
			{
				t.CTexs = realloc( t.CTexs, ( t.CTexCount + 1 ) * 4 * sizeof( float ) );
				t.CTexs[1 + t.CTexCount * 4] = 0;
				t.CTexs[2 + t.CTexCount * 4] = 0;
				t.CTexs[3 + t.CTexCount * 4] = nObjNo;
				int r = sscanf( line + 3, "%f %f %f", 
					&t.CTexs[0 + t.CTexCount * 4], 
					&t.CTexs[1 + t.CTexCount * 4], 
					&t.CTexs[2 + t.CTexCount * 4] );

				if( flipv )
					t.CTexs[1 + t.CTexCount * 4] = 1. - t.CTexs[1 + t.CTexCount * 4];
				if( r == 3 || r == 2 )
					t.CTexCount++;
				else
				{
					CNOVRAlert( m->base.tccctx, 1, "Error: Invalid Tex Coords (%d) (%s)\n", r, line + 3 );
				}
			}
			else
			{
				t.CVerts = realloc( t.CVerts, ( t.CVertCount + 1 ) * 3 * sizeof( float ) );
				t.CVerts[2 + t.CVertCount * 3] = 0;
				int r = sscanf( line + 2, "%f %f %f", 
					&t.CVerts[0 + t.CVertCount * 3], 
					&t.CVerts[1 + t.CVertCount * 3], 
					&t.CVerts[2 + t.CVertCount * 3] );
				if( r == 3 || r == 2 )
					t.CVertCount++;
				else
				{
					fprintf( stderr, "Error: Invalid Vertex\n" );
				}
			}
		}
		else if( tolower( line[0] ) == 'f' )
		{
			char buffer[3][TBUFFERSIZE];
			int p = 0;
			int r = sscanf( line + 1, "%30s %30s %30s", 
				buffer[0], buffer[1], buffer[2] );

			if( r != 3 )
			{
				//Invalid line - continue.
				continue;
			}

			//Whatever... they're all populated with something now.

			for( p = 0; p < 3; p++ )
			{
				char buffer2[3][TBUFFERSIZE];
				int mark = 0, markb = 0;
				int i;
				int sl = strlen( buffer[p] );
				for( i = 0; i < sl; i++ )
				{
					if( buffer[p][i] == '/' )
					{
						buffer2[mark][markb] = 0;
						mark++;
						if( mark >= 3 ) break;
						markb = 0;
					}
					else
						buffer2[mark][markb++] = buffer[p][i];
				}
				buffer2[mark][markb] = 0;
				for( i = mark+1; i < 3; i++ )
				{
					buffer2[i][0] = '0';
					buffer2[i][1] = 0;
				}

				int VNumber = atoi( buffer2[0] ) - 1;
				int TNumber = atoi( buffer2[1] ) - 1;
				int NNumber = atoi( buffer2[2] ) - 1;

				CNOVRModelTackIndex( m, 1, indices++ );
				CNOVRModelTackIndex( m, 1, indices++ );
				CNOVRModelTackIndex( m, 1, indices++ );

				if( VNumber < t.CVertCount )
					CNOVRVBOTackv( m->pGeos[0], 3, &t.CVerts[VNumber*3] );
				else
					CNOVRVBOTack( m->pGeos[0], 3, 0, 0, 0 );

				if( TNumber < t.CTexCount )

					CNOVRVBOTackv( m->pGeos[1], 4, &t.CTexs[TNumber*4] );
				else
					CNOVRVBOTack( m->pGeos[1], 4, 0, 0, 0, nObjNo );

				if( NNumber < t.CNormalCount )
					CNOVRVBOTackv( m->pGeos[2], 3, &t.CNormals[NNumber*3] );
				else
					CNOVRVBOTack( m->pGeos[2], 3, 0, 0, 0 );
			}
		}
		else if( tolower( line[0] ) == 'o' )
		{
			const char * marker = ( strlen( line ) > 2 )?( line + 2 ):0;
			nObjNo++;
			CNOVRDelinateGeometry( m, marker );
		}
		else if( strncmp( line, "usemtl", 6 ) == 0 )
		{
			//Not implemented.
		}
		else if( strncmp( line, "mtllib", 6 ) == 0 )
		{
			//Not implemented.
		}
		else if( tolower( line[0] ) == 's' )
		{
			//Not implemented.
		}
	}

	if( t.CVerts ) free( t.CVerts ); 
	if( t.CTexs ) free( t.CTexs ); 
	if( t.CNormals ) free( t.CNormals ); 
	free( splits );

	CNOVRVBOTaint( m->pGeos[0] );
	CNOVRVBOTaint( m->pGeos[1] );
	CNOVRVBOTaint( m->pGeos[2] );
	CNOVRModelTaintIndices( m );
	return;
}



static void CNOVRModelLoadRenderModel( cnovr_model * m, char * pchRenderModelNameIn )
{
	RenderModel_t * pModel = NULL;
	int rnnamelen = pchRenderModelNameIn?strlen( pchRenderModelNameIn ):0;
	if( !rnnamelen )
	{
		CNOVRAlert( m->base.tccctx, 1, "Unable to load render model %s (zero-length)\n", pchRenderModelNameIn );
	}
	if( strcasecmp( pchRenderModelNameIn + rnnamelen - (sizeof( ".rendermodel" )-1), ".rendermodel" ) == 0 )
	{
		rnnamelen -=( sizeof( ".rendermodel" )-1 );
	}
	char * pchRenderModelName = alloca( rnnamelen + 1 );
	memcpy( pchRenderModelName, pchRenderModelNameIn, rnnamelen + 1 );
	pchRenderModelName[rnnamelen] = 0;
	
	while( cnovrstate->oRenderModels->LoadRenderModel_Async( pchRenderModelName, &pModel ) == EVRRenderModelError_VRRenderModelError_Loading)
	{
		OGUSleep( 100 );
	}

	if( cnovrstate->oRenderModels->LoadRenderModel_Async( pchRenderModelName, &pModel ) || pModel == NULL )
	{
		CNOVRAlert( m->base.tccctx, 1, "Unable to load render model %s (ASync begin)\n", pchRenderModelName );
		return;
	}	
	RenderModel_TextureMap_t *pTexture = NULL;

	while( cnovrstate->oRenderModels->LoadTexture_Async( pModel->diffuseTextureId, &pTexture ) == EVRRenderModelError_VRRenderModelError_Loading )
	{
		OGUSleep( 100 );
	}

	if( cnovrstate->oRenderModels->LoadTexture_Async(pModel->diffuseTextureId, &pTexture) || pTexture == NULL )
	{
		CNOVRAlert( m->base.tccctx, 1, "Unable to load render model %s (Texture fail)\n", pchRenderModelName );
		cnovrstate->oRenderModels->FreeRenderModel( pModel );
		return;
	}

	CNOVRAlert( m->base.tccctx, 5, "Loading rendermodel %s\n", pchRenderModelName );

	if( m->iGeos != 3 )
	{
		printf( "Setting with strides\n" );
		CNOVRModelSetNumVBOsWithStrides( m, 3, 3, 2, 3 );
		CNOVRModelResetMarks( m );
	}
	CNOVRModelSetNumIndices( m, 0 );
	CNOVRDelinateGeometry( m, pchRenderModelName );
	int i;
	for( i = 0; i < pModel->unVertexCount; i++ )
	{
		RenderModel_Vertex_t * v = pModel->rVertexData + i;
		CNOVRVBOTackv( m->pGeos[0], 3, v->vPosition.v );
		CNOVRVBOTackv( m->pGeos[1], 2,  v->rfTextureCoord );
		CNOVRVBOTackv( m->pGeos[2], 3, v->vNormal.v );
	}
	for( i = 0; i < pModel->unTriangleCount; i++ )
	{
		CNOVRModelTackIndex( m, 3, pModel->rIndexData[i*3+0], pModel->rIndexData[i*3+1], pModel->rIndexData[i*3+2] );
	}

	if( m->iTextures == 0 )
	{
		m->pTextures = realloc( m->pTextures, sizeof( cnovr_texture * ) * 1 );
		m->pTextures[0] = CNOVRTextureCreate( 1, 1, 4 );
		m->iTextures = 1;
	}
	uint8_t * copyTexture = malloc( pTexture->unWidth * pTexture->unHeight * 4 );
	memcpy( copyTexture, pTexture->rubTextureMapData, pTexture->unWidth * pTexture->unHeight * 4 );
	CNOVRTextureLoadDataAsync( m->pTextures[0], pTexture->unWidth, pTexture->unHeight, 4, 0, copyTexture );

	CNOVRVBOTaint( m->pGeos[0] );
	CNOVRVBOTaint( m->pGeos[1] );
	CNOVRVBOTaint( m->pGeos[2] );
	CNOVRModelTaintIndices( m );
	cnovrstate->oRenderModels->FreeRenderModel( pModel );
	cnovrstate->oRenderModels->FreeTexture( pTexture );
}


void CNOVRModelLoadFromFileAsyncCallback( void * vm, void * dump )
{
	cnovr_model * m = (cnovr_model*) vm;
	OGLockMutex( m->model_mutex );
	char * filename = m->geofile;
	int slen = strlen( filename );
	if( CNOVRStringCompareEndingCase( filename, ".obj" ) == 0 )
	{
		CNOVRModelLoadOBJ( m, filename );
	}
	else if( CNOVRStringCompareEndingCase( filename, ".rendermodel" ) == 0 )
	{
		CNOVRModelLoadRenderModel( m, filename );
	}
	else
	{
		CNOVRAlert( m->base.tccctx, 1, "Error: Could not open model file: \"%s\".\n", filename );
	}
	OGUnlockMutex( m->model_mutex );

}

void CNOVRModelLoadFromFileAsync( cnovr_model * m, const char * filename )
{
	OGLockMutex( m->model_mutex );
	if( m->geofile ) free( m->geofile );
	m->geofile = strdup( filename );
	CNOVRJobTack( cnovrQAsync, CNOVRModelLoadFromFileAsyncCallback, m, 0, 1 );
	CNOVRJobTack( cnovrQAsync, CNOVRModelLoadFromFileAsyncCallback, m, 0, 1 );
	OGUnlockMutex( m->model_mutex );
}

/////////////////////////////////////////////////////////////////////////////////////

void CNOVRNodeDelete( void * ths, void * opaque )
{
	int i;
	CNOVRListDeleteTag( ths );

	cnovr_simple_node * node = (cnovr_simple_node *)ths;
	sb_free( node->objects );

#if 0
	//I think the behavior we desire is to not auto delete.
	cnovr_simple_node * n = (cnovr_simple_node*)ths;
	cnovr_base ** objects = (n->objects);
	int count = sb_count( objects );
	for( i = 0; i < count; i++ )
	{
		cnovr_header * o = objects[i]->header;
		if( o->Delete ) o->Delete( objects[i] );
	}
#endif
	CNOVRFreeLater( ths );
}

void CNOVRNodeRender( void * ths )
{
	int i;
	cnovr_simple_node * n = (cnovr_simple_node*)ths;
	cnovr_base ** objects = (n->objects);
	for( i = 0; i < sb_count( objects ); i++ )
	{
		cnovr_base * b = objects[i];
		cnovr_header * o = b->header;
		if( o->Type == TYPE_MODEL )
		{
			CNOVRModelRenderWithPose( (cnovr_model*)b, &n->pose );
		}
		else
		{
			if( o->Render ) o->Render( objects[i] );
		}
	}
}

cnovr_header cnovr_node_header = {
	(cnovrfn)CNOVRNodeDelete, //Delete
	(cnovrfn)CNOVRNodeRender, //Render
	TYPE_NODE,
};



cnovr_simple_node * CNOVRNodeCreateSimple( int reserved_size )
{
	cnovr_simple_node * ret = malloc( sizeof( cnovr_simple_node ) );
	memset( ret, 0, sizeof( cnovr_simple_node ) );
	ret->base.header = &cnovr_node_header;
	ret->base.tccctx = TCCGetTag();
	ret->objects = 0;
	pose_make_identity( &ret->pose );
	return ret;
}

void CNOVRNodeAddObject( cnovr_simple_node * node, void * o )
{
	sb_push( node->objects, (cnovr_base *)o );
}

void CNOVRNodeRemoveObject( cnovr_simple_node * node, void * o )
{
	cnovr_base * obj = (cnovr_base *)o;
	int i;
	int count = sb_count( node->objects );
	for( i = 0; i < count; i++ )
	{
		if( obj == node->objects[i] )
		{
			node->objects[i] = 0;
			break;
		}
	}

	stb_sb_remove( node->objects, i );
}


