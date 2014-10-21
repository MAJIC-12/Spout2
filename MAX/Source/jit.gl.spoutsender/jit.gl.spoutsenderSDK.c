/*

    jit.gl.spoutsender.c
    
	Based on :
		jit.gl.simple by Cycling74
		and jjit.gl.syphonclient.m
	    Copyright 2010 bangnoise (Tom Butterworth) & vade (Anton Marini).

	=================== SPOUT 2 ===================
	01.08.14 - rebuilt with Spout SDK
			 - compiled /MT
			 - Fixed dest_changed error	
			 - enabled memoryshare for sender creation - tested OK
	04-08-14 - Compiled for DX9
	10-08-14 - Updated for testing - DX9 mode
	13-08-14 - corrected context change texture handle leak
	14-08-14 - interop class corrected texture delete without context
	24.08.14 - recompiled with MB sendernames class revision
	01.09.14 - changes to Interop class to ensure texture and fbo are deleted and set to zero
	03.09.14 - dest_changed cycle delay removed
			 - error due to not setting texture and fbo ids to zero after release
			 - subsequent release failed and caused Jitter errors.
	30.09.14 - Updated for DirectX 11 and revised SDK
	09.10-14 - Cleanup for release
	12.10.14 - Recompile for release - Vers 2.001
	21.10.14 - Recompile for update V 2.001 beta
			 - Vers 2.002

	- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		Copyright (c) 2014, Lynn Jarvis. All rights reserved.

		Redistribution and use in source and binary forms, with or without modification, 
		are permitted provided that the following conditions are met:

		1. Redistributions of source code must retain the above copyright notice, 
		   this list of conditions and the following disclaimer.

		2. Redistributions in binary form must reproduce the above copyright notice, 
		   this list of conditions and the following disclaimer in the documentation 
		   and/or other materials provided with the distribution.

		THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"	AND ANY 
		EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
		OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE	ARE DISCLAIMED. 
		IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
		INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
		PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
		INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
		LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
		OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
		- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

*/
// Compile for DX9 instead of DX11 (default)
// #define UseD3D9

#include "jit.common.h"
#include "jit.gl.h"
#include "jit.gl.ob3d.h"
#include "ext_obex.h"
#include "string"

#include "../../SpoutSDK/Spout.h"


t_jit_err jit_ob3d_dest_name_set(t_jit_object *x, void *attr, long argc, t_atom *argv);

typedef struct _jit_gl_spout_sender 
{
	// Max object
	t_object	ob;

	// 3d object extension.  This is what all objects in the GL group have in common.
	void		*ob3d;
		
	// internal jit.gl.texture object, which we use to handle matrix input.
	t_symbol	*texture;

	long		dim[2];			// texture size
	
	// the name of the texture we should draw from - our internal one (for matrix input) or an external one
	t_symbol	*textureSource;
	
	// attributes
	t_symbol	*sendername;	// Used for input of the sender name
	long		memoryshare;	// force memory share instead of interop directx texture share

	// Our Spout sender object
	SpoutSender * mySender;

	bool bInitialized;
	GLuint g_texId;			// jitter texture ID
	int g_Width;			// width
	int g_Height;			// height
	char g_SenderName[256]; // sender name - link with sendername


} t_jit_gl_spout_sender;


void *_jit_gl_spout_sender_class;

//
// Function Declarations
//

// init/constructor/free
t_jit_err jit_gl_spout_sender_init(void);
t_jit_gl_spout_sender *jit_gl_spout_sender_new(t_symbol * dest_name);
void jit_gl_spout_sender_free(t_jit_gl_spout_sender *x);

// handle context changes - need to rebuild spoutsender here.
t_jit_err jit_gl_spout_sender_dest_closing(t_jit_gl_spout_sender *x);
t_jit_err jit_gl_spout_sender_dest_changed(t_jit_gl_spout_sender *x);

// draw
t_jit_err jit_gl_spout_sender_draw(t_jit_gl_spout_sender *x);

// handle input texture
t_jit_err jit_gl_spout_sender_jit_gl_texture(t_jit_gl_spout_sender *x, t_symbol *s, int argc, t_atom *argv);

// handle input matrix
t_jit_err jit_gl_spout_sender_jit_matrix(t_jit_gl_spout_sender *x, t_symbol *s, int argc, t_atom *argv);

//attributes
// @sendername, for sender name
t_jit_err jit_gl_spout_sender_sendername(t_jit_gl_spout_sender *x, void *attr, long argc, t_atom *argv);

// @memoryshare 0 / 1 force memoryshare
t_jit_err jit_gl_spout_sender_memoryshare(t_jit_gl_spout_sender *x, void *attr, long argc, t_atom *argv); 

// symbols
t_symbol *ps_sendername;
t_symbol *ps_texture;
t_symbol *ps_width;
t_symbol *ps_height;
t_symbol *ps_glid;
t_symbol *ps_gltarget;
t_symbol *ps_drawto;
t_symbol *ps_jit_gl_texture; // our internal texture for matrix input

//
// Function implementations
//

//
// Init, New, Cleanup, Context changes
//

t_jit_err jit_gl_spout_sender_init(void) 
{

	/*
	// Debug console window so printf works
	FILE* pCout; // should really be freed on exit 
	AllocConsole();
	freopen_s(&pCout, "CONOUT$", "w", stdout); 
	printf("jit_gl_spout_sender_init\n");
	*/

	// create our class
	_jit_gl_spout_sender_class = jit_class_new("jit_gl_spout_sender", 
												(method)jit_gl_spout_sender_new, (method)jit_gl_spout_sender_free,
												sizeof(t_jit_gl_spout_sender),A_DEFSYM,0L);
	
	// setup our OB3D flags to indicate our capabilities.
	long ob3d_flags = JIT_OB3D_NO_MATRIXOUTPUT; // no matrix output
	ob3d_flags |= JIT_OB3D_NO_ROTATION_SCALE;
	ob3d_flags |= JIT_OB3D_NO_POLY_VARS;
	ob3d_flags |= JIT_OB3D_NO_FOG;
	ob3d_flags |= JIT_OB3D_NO_LIGHTING_MATERIAL;
	ob3d_flags |= JIT_OB3D_NO_DEPTH;
	ob3d_flags |= JIT_OB3D_NO_COLOR;
	
	// set up object extension for 3d object, customized with flags
	void *ob3d;
	ob3d = jit_ob3d_setup(_jit_gl_spout_sender_class, calcoffset(t_jit_gl_spout_sender, ob3d), ob3d_flags);
		
	// add attributes
	long attrflags = JIT_ATTR_GET_DEFER_LOW | JIT_ATTR_SET_USURP_LOW;
	t_jit_object *attr;
	
	// sender name
	attr = (t_jit_object *)jit_object_new(_jit_sym_jit_attr_offset,"sendername",_jit_sym_symbol,attrflags,
						  (method)0L, jit_gl_spout_sender_sendername, calcoffset(t_jit_gl_spout_sender, sendername));	
	jit_class_addattr(_jit_gl_spout_sender_class,attr);
	
	// force memory share
	attr = (t_jit_object *)jit_object_new(_jit_sym_jit_attr_offset,"memoryshare", _jit_sym_long, attrflags,
		(method)0L, (method)jit_gl_spout_sender_memoryshare, calcoffset(_jit_gl_spout_sender, memoryshare));	
	jit_class_addattr(_jit_gl_spout_sender_class,attr);	


	// define our OB3D draw method.  called in automatic mode by 
	jit_class_addmethod(_jit_gl_spout_sender_class, (method)jit_gl_spout_sender_draw, "ob3d_draw", A_CANT, 0L);

	// define our dest_closing and dest_changed methods. 
	jit_class_addmethod(_jit_gl_spout_sender_class, (method)jit_gl_spout_sender_dest_closing, "dest_closing", A_CANT, 0L);
	jit_class_addmethod(_jit_gl_spout_sender_class, (method)jit_gl_spout_sender_dest_changed, "dest_changed", A_CANT, 0L);
	
	// must register for ob3d use
	jit_class_addmethod(_jit_gl_spout_sender_class, (method)jit_object_register, "register", A_CANT, 0L);
	
	// handle texture input - we need to explictly handle jit_gl_texture messages
	// so we can set our internal texture reference
	jit_class_addmethod(_jit_gl_spout_sender_class, (method)jit_gl_spout_sender_jit_gl_texture, "jit_gl_texture", A_GIMME, 0L);

	// handle matrix inputs
	jit_class_addmethod(_jit_gl_spout_sender_class, (method)jit_gl_spout_sender_jit_matrix, "jit_matrix", A_USURP_LOW, 0);	
	
	//symbols
	ps_sendername = gensym("sendername");
	ps_texture = gensym("texture");
	ps_width = gensym("width");
	ps_height = gensym("height");
	ps_glid = gensym("glid");
	ps_gltarget = gensym("gltarget");
	ps_jit_gl_texture = gensym("jit_gl_texture");
	ps_drawto = gensym("drawto");

	jit_class_register(_jit_gl_spout_sender_class);

	return JIT_ERR_NONE;
}

t_jit_gl_spout_sender *jit_gl_spout_sender_new(t_symbol * dest_name)
{
	t_jit_gl_spout_sender *x = NULL;

	// make jit object
	if ((x = (t_jit_gl_spout_sender *)jit_object_alloc(_jit_gl_spout_sender_class)))	{
		
		// create and attach ob3d
		jit_ob3d_new(x, dest_name);

		// Initialize variables
		x->bInitialized  = false;
		x->g_Width       = 0;
		x->g_Height      = 0;
		x->g_texId       = 0;
		x->mySender      = NULL;

		// Create a new Spout sender
		x->mySender = new SpoutSender;
		
		// Set to DX9 for compatibility with Version 1 apps
		#ifdef UseD3D9
		x->mySender->SetDX9(true);
		#else
		x->mySender->SetDX9(false);
		#endif

		// set up attributes
		x->memoryshare = 0; // default is texture share

		// Syphon comment - TODO : is this right ? LJ - not sure
		jit_attr_setsym(x->sendername, _jit_sym_name, gensym("sendername"));

		// instantiate a single internal jit.gl.texture for matrix input
		x->texture = (t_symbol *)jit_object_new(ps_jit_gl_texture, jit_attr_getsym(x,ps_drawto) );
		if (x->texture)	{
			// set texture attributes.
			t_symbol *name =  jit_symbol_unique();
			jit_attr_setsym(x->texture, _jit_sym_name, name);
			jit_attr_setsym(x->texture,gensym("defaultimage"),gensym("white"));
			jit_attr_setlong(x->texture,gensym("rectangle"), 1);
			jit_attr_setsym(x->texture, gensym("mode"),gensym("dynamic"));	
			jit_attr_setsym(x, ps_texture, name);
			x->textureSource = name; // init of texture
		} 
		else {
			jit_object_error((t_object *)x,"jit.gl.spoutsender: could not create texture");
			x->textureSource = _jit_sym_nothing;		
		}
	} 
	else {
		x = NULL;
	}

	return x;
}

void jit_gl_spout_sender_free(t_jit_gl_spout_sender *x)
{

	// free our internal texture
	if(x->texture) jit_object_free(x->texture);

	// free our ob3d data
	if(x) jit_ob3d_free(x);

	// Release the sender
	x->mySender->ReleaseSender();

	// Delete the sender object last.
	if(x->mySender) delete x->mySender;
	x->mySender = NULL;

}

t_jit_err jit_gl_spout_sender_dest_closing(t_jit_gl_spout_sender *x)
{

	// In case we have dest-closing without dest_changed
	// or dest_changed without dest_closing - allow for both.
	// Release sender if initialized
	if(x->bInitialized)	x->mySender->ReleaseSender();
	x->bInitialized = false; // Initialize again in draw

	return JIT_ERR_NONE;
}


t_jit_err jit_gl_spout_sender_dest_changed(t_jit_gl_spout_sender *x)
{	
	//
	// Release sender if initialized
	//
	// IMPORTANT : do not re-initialize here but do so next round in draw
	//
	if(x->bInitialized)	x->mySender->ReleaseSender();
	x->bInitialized = false; // Initialize again in draw

	return JIT_ERR_NONE;
}

//
// Input Imagery, Texture/ Matrix
//


// handle matrix input
t_jit_err jit_gl_spout_sender_jit_matrix(t_jit_gl_spout_sender *x, t_symbol *s, int argc, t_atom *argv)
{
	t_symbol *name;
	void *m;

	if ((name=jit_atom_getsym(argv)) != _jit_sym_nothing) {
		m = jit_object_findregistered(name);
		if (!m)	{
			jit_object_error((t_object *)x,"jit.gl.spoutsender: couldn't get matrix object!");
			return JIT_ERR_GENERIC;
		}
	}

	if (x->texture)	{				
		jit_object_method(x->texture,s,s,argc,argv);
		// add texture to ob3d texture list
		t_symbol *texName = jit_attr_getsym(x->texture, _jit_sym_name);
		jit_attr_setsym(x,ps_texture,texName);
		x->textureSource = texName; // out texture is matrix input
	}

	return JIT_ERR_NONE;
}

// handle texture input 
// This happens all the time. Changes are handled in draw
t_jit_err jit_gl_spout_sender_jit_gl_texture(t_jit_gl_spout_sender *x, t_symbol *s, int argc, t_atom *argv)
{

	t_symbol *name = jit_atom_getsym(argv);
	
	if (name)   {
		// add texture to ob3d texture list
		jit_attr_setsym(x, ps_texture, name);
		x->textureSource = name;
	}

	return JIT_ERR_NONE;
}


//
// Draw
//

t_jit_err jit_gl_spout_sender_draw(t_jit_gl_spout_sender *x)
{
	if (!x)	return JIT_ERR_INVALID_PTR;

	float vpdim[4]; // for saving the viewport dimensions
	GLint previousFBO;
    GLint previousMatrixMode;
	GLint previousActiveTexture;


	if(x->textureSource) {

		// get our latest texture info.
		t_jit_object *texture = (t_jit_object*)jit_object_findregistered(x->textureSource);
		GLuint texId     = jit_attr_getlong(texture, ps_glid);
		GLuint texWidth  = jit_attr_getlong(texture, ps_width);
		GLuint texHeight = jit_attr_getlong(texture, ps_height);
		GLuint texTarget = jit_attr_getlong(texture, ps_gltarget);

		// all of these must be > 0
		if(texId > 0 && texWidth > 0 && texHeight > 0)	{

			// =========================================
			// 
			//		Save OpenGL state
			//
			glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &previousFBO);
			glGetIntegerv(GL_MATRIX_MODE, &previousMatrixMode);
			glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);

			glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);

			// Syphon note :
			// Jitter uses multiple texture coordinate arrays on different units
			// http://s-musiclab.jp/mmj_docs/max5/develop/MaxSDK-5.1.1_J/html/jit_8gl_8texture_8c_source.html
			// We need to ensure we set this before changing our texture matrix
			// glActiveTexture selects which texture unit subsequent texture state calls will affect.
			glClientActiveTexture(GL_TEXTURE0);

			glMatrixMode(GL_TEXTURE);
			glPushMatrix();
			glLoadIdentity();

			glPushAttrib(GL_TRANSFORM_BIT);
			glGetFloatv(GL_VIEWPORT, vpdim);
			glViewport(0, 0, x->g_Width, x->g_Height);
					
			glMatrixMode(GL_PROJECTION);
			glPushMatrix();
			glLoadIdentity();
			glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0f, 1.0f);

			glMatrixMode(GL_MODELVIEW);
			glPushMatrix();
			glLoadIdentity();
			// =========================================

			// --- Create a sender if it is not initialized ---
			if(!x->bInitialized) {
				x->g_texId	= texId;
				x->g_Width	= texWidth;
				x->g_Height	= texHeight;
				
				// Set memoryshare mode if the user requested it
				if(x->memoryshare == 1) x->mySender->SetMemoryShareMode(true);

				// Create a sender
				x->mySender->CreateSender(x->g_SenderName, x->g_Width, x->g_Height);

				x->bInitialized = true;
			}
			// --- Check for change of name, width and height or texture ID ---
			else if(texWidth != x->g_Width 
				 || texHeight != x->g_Height
				 || texId != x->g_texId
				 || strncmp(x->sendername->s_name, x->g_SenderName, 256) != 0) {
				// Reset and initialize again the next time round
				x->mySender->ReleaseSender();
				x->bInitialized = false; 
			} // endif texture has changed
			// --- otherwise it is initialized OK so send the frame out ---
			else {
				// No actual rendering is done here, this is just sending out a texture
				if(x->bInitialized)	x->mySender->SendTexture(texId, texTarget, texWidth, texHeight);
			} // end if size was OK and normal draw

			// =========================================
			// Restore everything
			glMatrixMode(GL_MODELVIEW);
			glPopMatrix();

			glMatrixMode(GL_PROJECTION);
			glPopMatrix();

			glPopAttrib();
			glViewport(vpdim[0], vpdim[1], vpdim[2], vpdim[3]);
		
			glMatrixMode(GL_TEXTURE);
			glPopMatrix();
			glMatrixMode(previousMatrixMode);

			glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, previousFBO);

			glActiveTexture(previousActiveTexture);

			// ensure we act on the proper client texture as well
			glPopClientAttrib();
			glClientActiveTexture(previousActiveTexture);
			// =========================================

		} // endif texId && width > 0 && height > 0
	} // endif x->textureSource
	return JIT_ERR_NONE;
}

//
// attributes
//

// @sendername
t_jit_err jit_gl_spout_sender_sendername(t_jit_gl_spout_sender *x, void *attr, long argc, t_atom *argv)
{
	t_symbol *srvname;

	// Use this for user input of sharing name
	if(x) {	
		if (argc && argv) {
			srvname = jit_atom_getsym(argv);
			x->sendername = srvname;
		} 
		else {
			// no args, set to zero
			x->sendername = gensym("jit.gl.spoutSender");
			strcpy_s(x->g_SenderName, "jit.gl.spoutSender");
		}
		// set the name for this sender
		strcpy_s(x->g_SenderName, 256, x->sendername->s_name);
	}

	return JIT_ERR_NONE;
}


// @memoryshare
// force memory share flag (default is 0 off - then is automatic dependent on hardware)
t_jit_err jit_gl_spout_sender_memoryshare(t_jit_gl_spout_sender *x, void *attr, long argc, t_atom *argv)
{
	long c = jit_atom_getlong(argv);

	// Bypass compiler warning
	bool bC = true;
	if(c == 0) bC = false;

	x->memoryshare = c; // 0 off or 1 on
	x->mySender->SetMemoryShareMode(bC); // Memoryshare on or off

	return JIT_ERR_NONE;
}

