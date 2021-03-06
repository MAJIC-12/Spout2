﻿/*
	
	SpoutReceiverSDK2.dll

	LJ - leadedge@adam.com.au

	FFGL plugin for receiving DirectX texture from an equivalent
	sending application	either using wglDxInterop or memory share
	Note fix to FFGL.cpp to allow setting string parameters
	http://resolume.com/forum/viewtopic.php?f=14&t=10324
	----------------------------------------------------------------------
	26.06.14 - major change to use Spout SDK
	08-07-14 - Version 3.000
	14.07-14 - changed to fixed SpoutReceiver object
	16.07.14 - restored host fbo binding after readtexture otherwise texture draw does not work
			 - used a local texture for both textureshare and memoryshare
	25.07.14 - Version 3.001 - corrected ReceiveTexture in SpoutSDK.cpp for reset if the sender was closed
	01.08.14 - Version 3.002 - external sender registration
	13.08.14 - Version 3.003 - restore viewport
	18.08.14 - recompiled for testing and copied to GitHub
	20.08.14 - activated event locks
			 - included DX9 mode compile flag (default true for Version 1 compatibility)
			 - included DX9 arg for SelectSenderPanel
			 - Version 3.004
			 - recompiled for testing and copied to GitHub
	=======================================================================================================
	24.08.14 - recompiled with MB sendernames class revision
			 - disabled mouse hook for SpoutPanel
			 - Version 3.005
	26.08.14 - removed mouse hook
			 - detect existing sender name on restart after Magic full screen.
			   Version 3.006
	29.08.14 - detect host name and dll start
			 - user messages for revised SpoutPanel instead of MessageBox
			 - Version 3.007
	31.08.14 - changed from quad to vertex array for draw
	01.09.14 - leak test and some changes made to SpoutGLDXinterop
	02.09.14 - added more information in plugin Description and About
			 - Version 3.008
			 - Uploaded to GitHub
	15-09-14 - added RestoreOpenGLstate before return for sender size change
			 - Version 3.009
	16-09-14 - change from saving state matrices to just the viewport
			 - Version 3.010
	17-09-14 - change from viewport to vertex for aspect control
			 - Version 3.011
	19-09-14 - Recompiled for DirectX 11
			   A receiver should be compatible with all apps, but a sender will not
			 - Version 3.012
	21-09-14 - recompiled for DirectX 11 mutex texture access locks
			 - Introduced bUseActive flag instead of empty name
			 - Corrected inverted draw
			 - Version 3.013
	23-09-14 - corrected User entered name reset for a saved entry
			 - Version 3.014
	30-09-14 - Host fbo argument for ReceiveTexture
			 - Version 3.015
	12-10-14 - recompiled for release
			 - SpoutSDK.cpp - updated SelectSenderPanel to set SpoutPanel.exe topmost if it exists
			 - Version 3.016
	21.10.14 - Recompile for update V 2.001 beta
			 - Version 3.017
	12.11.14 - fixed bug for ReceiveTexture passing host fbo	
			 - change to default startup for button detection to button up
			 - Version 3.018
	23.11.14 - fixed bug for FFPARAM_ASPECT which could be turned on but not off
	24.11.14 - FFPARAM_SharingName - test for null string as well as null parameter address
			 - Version 3.019
	16.12.14 - included NvOptimusEnablement export
			 - Version 3.020
	31.01.15 - Changed ID to LJ48/49 instead of OF48/49
			   Included define for DirectX 9 compile
			   Recompiled for DirectX 11, DirectX9 and Memoryshare for 2015 release
			   Version 3.021
	07.02.15 - corrected unregisterd sender logic in FFPARM_UPDATE
			   Version 3.022


*/
#include "SpoutReceiverSDK2.h"
#include <FFGL.h>
#include <FFGLLib.h>

// This allows the Optimus global 3d setting to be "adapt" instead of "high performance"
extern "C" {
    _declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

// For DirectX 9 mode enable the define below, otherwise compiles for DirectX 11
// 14.02.15 - added auto detection in SpoutGLDXinterop so can leave as DX11 default
// #define DX9Mode

// For memoryshare, enable the define below
// #define MemoryShareMode

#ifndef MemoryShareMode
	#define FFPARAM_SharingName		(0)
	#define FFPARAM_Update			(1)
	#define FFPARAM_Select			(2)
	#define FFPARAM_Aspect			(3)

#else
	#define FFPARAM_Aspect			(0)
#endif
        
////////////////////////////////////////////////////////////////////////////////////////////////////
//  Plugin information
////////////////////////////////////////////////////////////////////////////////////////////////////
static CFFGLPluginInfo PluginInfo (
	SpoutReceiverSDK2::CreateInstance,				// Create method
	#ifndef MemoryShareMode
	"LJ48",										// Plugin unique ID
	"SpoutReceiver2",							// Plugin name (receive texture from DX)
	1,											// API major version number
	001,										// API minor version number
	3,											// Plugin major version number
	021,										// Plugin minor version number
	FF_SOURCE,									// Plugin type
		#ifdef DX9Mode
		"Spout Receiver DirectX 9 - Vers 3.022\nReceives textures from Spout Senders\n\nSender Name : enter a sender name\nUpdate : update the name entry\nSelect : select a sender using 'SpoutPanel'\nAspect : preserve aspect ratio of the received sender", // Plugin description
		#else
		"Spout Receiver DirectX 11 - Vers 3.022\nReceives textures from Spout Senders\n\nSender Name : enter a sender name\nUpdate : update the name entry\nSelect : select a sender using 'SpoutPanel'\nAspect : preserve aspect ratio of the received sender", // Plugin description
		#endif
	#else
	"LJ49",										// Plugin unique ID
	"SpoutReceiver2M",							// Plugin name (receive texture from DX)
	1,											// API major version number
	001,										// API minor version number
	3,											// Plugin major version number
	021,										// Plugin minor version number
	FF_SOURCE,									// Plugin type
	"Spout Receiver Memoryshare - Vers 3.022\nReceives textures from Spout Senders\n\nSender Name : enter a sender name\nUpdate : update the name entry\nSelect : select a sender using 'SpoutPanel'\nAspect : preserve aspect ratio of the received sender", // Plugin description
	#endif
	"S P O U T - Version 2\nspout.zeal.co"		// About
);

/////////////////////////////////
//  Constructor and destructor //
/////////////////////////////////
SpoutReceiverSDK2::SpoutReceiverSDK2()
:CFreeFrameGLPlugin(),
 m_initResources(1),
 m_maxCoordsLocation(-1)
{

	HMODULE module;
	char path[MAX_PATH];

	/*
	// Debug console window so printf works
	FILE* pCout;
	AllocConsole();
	freopen_s(&pCout, "CONOUT$", "w", stdout); 
	printf("SpoutReceiver2 Vers 3.022\n");
	*/

	// Input properties - this is a source and has no inputs
	SetMinInputs(0);
	SetMaxInputs(0);
	
	//
	// ======== initial values ========
	//

	#ifdef DX9Mode
	bDX9mode          = true;   // DirectX 9 mode rather than default DirectX 11
	#else
	bDX9mode          = false;
	#endif

	bInitialized      = false;  // not initialized yet by either means
	bAspect           = false;  // preserve aspect ratio of received texture in draw
	bUseActive        = true;   // connect to the active sender
	bStarted          = false;  // Do not allow a starting cycle

	UserSenderName[0] = 0;      // User entered sender name
	g_Width	          = 512;
	g_Height          = 512;    // arbitrary initial image size
	myTexture         = 0;      // only used for memoryshare mode

	//
	// Parameters
	//
	// Memoryshare define
	// if set to true (memory share only), it connects as memory share
	// and there is no user option to select a sender
	// default is false (automatic)
	#ifndef MemoryShareMode
	SetParamInfo(FFPARAM_SharingName, "Sender Name",   FF_TYPE_TEXT, "");
	SetParamInfo(FFPARAM_Update,      "Update",        FF_TYPE_EVENT, false );
	SetParamInfo(FFPARAM_Select,      "Select",        FF_TYPE_EVENT, false );
	bMemoryMode = false;
	#else
	bMemoryMode = true;
	#endif
	SetParamInfo(FFPARAM_Aspect,       "Aspect",       FF_TYPE_BOOLEAN, false );

	// For memory mode, tell Spout to use memoryshare
	if(bMemoryMode) {
		receiver.SetMemoryShareMode();
		// Give it an arbitrary user name for ProcessOpenGL
		strcpy_s(UserSenderName, 256, "0x8e14549a"); 
	}

	// Set DirectX mode depending on DX9 flag
	if(bDX9mode) 
		receiver.SetDX9(true);
	else 
	    receiver.SetDX9(false);

	// Find the host executable name
	module = GetModuleHandle(NULL);
	GetModuleFileNameA(module, path, MAX_PATH);
	_splitpath_s(path, NULL, 0, NULL, 0, HostName, MAX_PATH, NULL, 0);
	
	// Isadora and Resolume act on button down
	// Isadora activates all parameters on plugin load, so allow one cycle for starting.
	// Magic reacts on button-up, so when the dll loads the parameters are not activated. 
	// Magic and default Windows apps act on button up so all is OK.
	if(strstr(HostName, "Avenue") == 0 || strstr(HostName, "Arena") == 0 || strstr(HostName, "Isadora") == 0) {
		bStarted = false;
	}
	else {
		bStarted = true;
	}

}


SpoutReceiverSDK2::~SpoutReceiverSDK2()
{
	// OpenGL context required
	if(wglGetCurrentContext()) {
		// ReleaseReceiver does nothing if there is no receiver
		receiver.ReleaseReceiver();
		if(myTexture != 0) glDeleteTextures(1, &myTexture);
		myTexture = 0;
	}

}


////////////////////////////////////////////////////////////
//						Methods                           //
////////////////////////////////////////////////////////////
DWORD SpoutReceiverSDK2::InitGL(const FFGLViewportStruct *vp)
{
	// Viewport dimensions might not be supplied by the host here
	return FF_SUCCESS;
}


DWORD SpoutReceiverSDK2::DeInitGL()
{
	// OpenGL context required
	if(wglGetCurrentContext()) {
		// ReleaseReceiver does nothing if there is no receiver
		receiver.ReleaseReceiver();
		if(myTexture != 0) glDeleteTextures(1, &myTexture);
		myTexture = 0;
	}
	return FF_SUCCESS;
}


DWORD SpoutReceiverSDK2::ProcessOpenGL(ProcessOpenGLStruct *pGL)
{
	bool bRet;

	bStarted = true;
	
	//
	// Initialize a receiver
	//

	// If already initialized and the user has entered a different name, reset the receiver
	if(bInitialized && UserSenderName[0] && strcmp(UserSenderName, SenderName) != 0) {
		printf("Resetting receiver[%s][%s]\n", UserSenderName, SenderName);
		receiver.ReleaseReceiver();
		bInitialized = false;
	}

	if(!bInitialized) {

		// If UserSenderName is set, use it. Otherwise find the active sender
		if(UserSenderName[0]) {
			printf("Using [%s]\n", UserSenderName);
			strcpy_s(SenderName, UserSenderName); // Create a receiver with this name
			bUseActive = false;
		}
		else {
			bUseActive = true;
		}

		// CreateReceiver will return true only if it finds a sender running.
		// If a sender name is specified and does not exist it will return false.
		if(receiver.CreateReceiver(SenderName, g_Width, g_Height, bUseActive)) {
			printf("Created receiver [%s]\n", SenderName);
			// Did it initialized in Memory share mode ?
			bMemoryMode = receiver.GetMemoryShareMode();
			// Initialize a texture - Memorymode RGB or Texturemode RGBA
			InitTexture();
			bInitialized = true;
		}
		return FF_SUCCESS;
	}
	else {
		//
		// Receive a shared texture
		//
		//	Success : Returns the sender name, width and height
		//	Failure : No sender detected
		//
		// Important - pass the host FBO to restore the binding
		bRet = receiver.ReceiveTexture(SenderName, width, height, myTexture, GL_TEXTURE_2D, false, pGL->HostFBO);

		if(bRet) {
			// Received the texture OK, but the sender or texture dimensions could have changed
			// Reset the global width and height so that the viewport can be set for aspect ratio control
			if(width != g_Width || height != g_Height) {
				g_Width  = width;
				g_Height = height;
				// Reset the local texture
				InitTexture();
				return FF_SUCCESS;
			} // endif sender has changed

			// All matches so draw the texture
			DrawReceivedTexture(myTexture, GL_TEXTURE_2D,  g_Width, g_Height);
		}
	}

	return FF_SUCCESS;

}


DWORD SpoutReceiverSDK2::GetParameter(DWORD dwIndex)
{
	DWORD dwRet = FF_FAIL;

	#ifndef MemoryShareMode
	switch (dwIndex) {

		case FFPARAM_SharingName:
			dwRet = (DWORD)UserSenderName;
			return dwRet;
		default:
			return FF_FAIL;
	}
	#endif

	return FF_FAIL;
}


DWORD SpoutReceiverSDK2::SetParameter(const SetParameterStruct* pParam)
{
	unsigned int width, height;
	HANDLE dxShareHandle;
	DWORD dwFormat;
	char name[256];

	if (pParam != NULL) {

		switch (pParam->ParameterNumber) {

		// These parameters will not exist for memoryshare mode
		#ifndef MemoryShareMode

		case FFPARAM_SharingName:

			if(pParam->NewParameterValue && (char*)pParam->NewParameterValue) {
				// If there is anything already in this field at startup, it is set by a saved composition
				strcpy_s(name, 256, (char*)pParam->NewParameterValue);
				// If it is a different name, copy to the username
				if(strcmp(name, UserSenderName) != 0)
					strcpy_s(UserSenderName, 256, (char*)pParam->NewParameterValue);
				// Does the sender exist ?
				if(receiver.GetSenderInfo(UserSenderName, width, height, dxShareHandle, dwFormat)) {
					// Is it an external unregistered sender - e.g. VVVV ?
					if(!receiver.spout.interop.senders.FindSenderName(UserSenderName) ) {
						// register it
						receiver.spout.interop.senders.RegisterSenderName(UserSenderName);
					}
				}
			}
			else {
				// Reset to an empty string so that the active sender 
				// is used and SelectSenderPanel works
				UserSenderName[0] = 0;
			}
			break;

		case FFPARAM_Update :

			// Update the user entered name
			if(pParam->NewParameterValue) { // name entry toggle is on
				// Is there a  user entered name
				if(UserSenderName[0] != 0) {
					// Does the sender exist ?
					if(receiver.GetSenderInfo(UserSenderName, width, height, dxShareHandle, dwFormat)) {
						// Is it an external unregistered sender - e.g. VVVV ?
						if(!receiver.spout.interop.senders.FindSenderName(UserSenderName) ) {
							// register it
							receiver.spout.interop.senders.RegisterSenderName(UserSenderName);
						}
						// The user has typed it in, so make it the active sender
						receiver.spout.interop.senders.SetActiveSender(UserSenderName);
						// Start again
						if(bInitialized) receiver.ReleaseReceiver();
						bInitialized = false;
					}
				} // endif user name entered
			} // endif Update
			break;

		// SpoutPanel sender selection
		case FFPARAM_Select :
			// bStarted is set as soon as ProcessOpenGL is called
			if (pParam->NewParameterValue && bStarted) {
				if(UserSenderName[0]) {
					receiver.SelectSenderPanel("Using 'Sender Name' entry\nClear the name entry first");
				}
				else {
					if(bDX9mode) {
						receiver.SelectSenderPanel("/DX9");
					}
					else {
						receiver.SelectSenderPanel("/DX11"); // default DX11 compatible
					}
				}
			}
			break;
		#endif

		case FFPARAM_Aspect:
			// Bug here - could turn on but not off
			// if(pParam->NewParameterValue) {
				if(pParam->NewParameterValue > 0) {
					bAspect = true;
				}
				else  {
					bAspect = false;
				}
			// }
			break;

		default:
			break;

		}
		return FF_SUCCESS;
	}

	return FF_FAIL;

}


// Initialize a local texture
void SpoutReceiverSDK2::InitTexture()
{
	if(myTexture != 0) {
		glDeleteTextures(1, &myTexture);
		myTexture = 0;
	}

	glGenTextures(1, &myTexture);
	glBindTexture(GL_TEXTURE_2D, myTexture);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	if(bMemoryMode)
		glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGB, g_Width, g_Height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
	else
		glTexImage2D(GL_TEXTURE_2D, 0,  GL_RGBA, g_Width, g_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glBindTexture(GL_TEXTURE_2D, 0);

}


void SpoutReceiverSDK2::DrawReceivedTexture(GLuint TextureID, GLuint TextureTarget,  unsigned int width, unsigned int height)
{
	
	float image_aspect, vp_aspect;
	int vpdim[4];
	
	// Default offsets
	float vx = 1.0;
	float vy = 1.0;

	// find the current viewport dimensions to scale to the aspect ratio required
	glGetIntegerv(GL_VIEWPORT, vpdim);

	// Calculate aspect ratios
	vp_aspect = (float)vpdim[2]/(float)vpdim[3];
	image_aspect = (float)width/(float)height;

	// Preserve image aspect ratio for draw
	if(bAspect) {
		if(image_aspect > vp_aspect) {
			// Calculate the offset in Y
			vy = 1.0f/image_aspect;
			// Adjust to the viewport aspect ratio
			vy = vy*vp_aspect;
			vx = 1.0;
		}
		else { // Otherwise adjust the horizontal offset
			// Calculate the offset in X
			vx = image_aspect;
			// Adjust to the viewport aspect ratio
			vx = vx/vp_aspect;
			vy = 1.0;
		}
	}

	// Invert the texture coords from DirectX to OpenGL
	GLfloat tc[] = {
			 0.0, 1.0,
			 0.0, 0.0,
			 1.0, 0.0,
			 1.0, 1.0 };

	GLfloat verts[] =  {
			-vx,  -vy,   // bottom left
			-vx,   vy,   // top left
			 vx,   vy,   // top right
			 vx,  -vy }; // bottom right


	glPushMatrix();
	glColor4f(1.f, 1.f, 1.f, 1.f);
	glEnable(TextureTarget);
	glBindTexture(TextureTarget, TextureID);

	glEnableClientState( GL_TEXTURE_COORD_ARRAY );
	glTexCoordPointer(2, GL_FLOAT, 0, tc );
	glEnableClientState(GL_VERTEX_ARRAY);		
	glVertexPointer(2, GL_FLOAT, 0, verts );
	glDrawArrays( GL_TRIANGLE_FAN, 0, 4 );
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);

	glBindTexture(TextureTarget, 0);
	glDisable(TextureTarget);
	glPopMatrix();


}
