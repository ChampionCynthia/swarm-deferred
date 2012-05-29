//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=====================================================================================//

#include "shaderlib/BaseShader.h"
#include "shaderlib/ShaderDLL.h"
#include "tier0/dbg.h"
#include "shaderDLL_Global.h"
#include "../IShaderSystem.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/itexture.h"
#include "materialsystem/ishaderapi.h"
#include "materialsystem/materialsystem_config.h"
#include "shaderlib/cshader.h"
#include "mathlib/vmatrix.h"
#include "tier1/strtools.h"
#include "convar.h"
#include "tier0/vprof.h"

#include "commandbuilderlib.h"
#include "shaderapi/commandbuffer.h"

// NOTE: This must be the last include file in a .cpp file!
#include "tier0/memdbgon.h"


//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
const char *CBaseShader::s_pTextureGroupName = NULL;
IMaterialVar **CBaseShader::s_ppParams;
IShaderShadow *CBaseShader::s_pShaderShadow;
IShaderDynamicAPI *CBaseShader::s_pShaderAPI;
IShaderInit *CBaseShader::s_pShaderInit;
int CBaseShader::s_nModulationFlags;
int CBaseShader::s_nPassCount = 0;

//CMeshBuilder *CBaseShader::s_pMeshBuilder;
static ConVar mat_fullbright( "mat_fullbright","0", FCVAR_CHEAT );


CFixedCommandStorageBuffer< 1024 > tempBuffer;

class CPerInstanceContextData : public CBasePerInstanceContextData
{
public:

	CPerInstanceContextData()
	{
		m_iAllocated = -1;
		m_piCommandArray = NULL;
	};

	~CPerInstanceContextData()
	{
		if ( m_piCommandArray != NULL )
		{
			for ( int i = 0; i <= m_iAllocated; i++ )
				delete [] m_piCommandArray[i];

			delete [] m_piCommandArray;
		}
	};

	void SetCommands( int p_iPass, uint8 *p_iCmd )
	{
		if ( p_iPass > m_iAllocated )
		{
			uint8 **pNewCmds = new uint8*[p_iPass + 1];
			Q_memset( pNewCmds, 0, sizeof(uint8*) * (p_iPass + 1) );

			if ( m_piCommandArray != NULL )
			{
				Assert( m_iAllocated >= 0 );

				Q_memcpy( pNewCmds, m_piCommandArray, sizeof(uint8*) * (m_iAllocated + 1) );
				delete [] m_piCommandArray;
			}

			m_piCommandArray = pNewCmds;
			m_iAllocated = p_iPass;
		}

		delete [] m_piCommandArray[ p_iPass ];
		m_piCommandArray[ p_iPass ] = p_iCmd;
	};

	uint8 *GetCommands( int p_iPass )
	{
		if ( m_iAllocated < p_iPass )
			return NULL;

		return m_piCommandArray[ p_iPass ];
	}

private:

	int m_iAllocated;

	uint8 **m_piCommandArray;
};

CPerInstanceContextData **CBaseShader::s_pInstanceDataPtr = NULL;

//-----------------------------------------------------------------------------
// constructor
//-----------------------------------------------------------------------------
CBaseShader::CBaseShader()
{
	GetShaderDLL()->InsertShader( this );
}


//-----------------------------------------------------------------------------
// Shader parameter info
//-----------------------------------------------------------------------------
// Look in BaseShader.h for the enumeration for these.
// Update there if you update here.
static ShaderParamInfo_t s_StandardParams[NUM_SHADER_MATERIAL_VARS] =
{
	{ "$flags",				"flags",			SHADER_PARAM_TYPE_INTEGER,	"0", SHADER_PARAM_NOT_EDITABLE },
	{ "$flags_defined",		"flags_defined",	SHADER_PARAM_TYPE_INTEGER,	"0", SHADER_PARAM_NOT_EDITABLE },
	{ "$flags2",  			"flags2",			SHADER_PARAM_TYPE_INTEGER,	"0", SHADER_PARAM_NOT_EDITABLE },
	{ "$flags_defined2",	"flags2_defined",	SHADER_PARAM_TYPE_INTEGER,	"0", SHADER_PARAM_NOT_EDITABLE },
	{ "$color",		 		"color",			SHADER_PARAM_TYPE_COLOR,	"[1 1 1]", 0 },
	{ "$alpha",	   			"alpha",			SHADER_PARAM_TYPE_FLOAT,	"1.0", 0 },
	{ "$basetexture",  		"Base Texture with lighting built in", SHADER_PARAM_TYPE_TEXTURE, "shadertest/BaseTexture", 0 },
	{ "$frame",	  			"Animation Frame",	SHADER_PARAM_TYPE_INTEGER,	"0", 0 },
	{ "$basetexturetransform", "Base Texture Texcoord Transform",SHADER_PARAM_TYPE_MATRIX,	"center .5 .5 scale 1 1 rotate 0 translate 0 0", 0 },
	{ "$flashlighttexture",  		"flashlight spotlight shape texture", SHADER_PARAM_TYPE_TEXTURE, "effects/flashlight001", SHADER_PARAM_NOT_EDITABLE },
	{ "$flashlighttextureframe",	"Animation Frame for $flashlight",	SHADER_PARAM_TYPE_INTEGER, "0", SHADER_PARAM_NOT_EDITABLE },
	{ "$color2",		 		"color2",			SHADER_PARAM_TYPE_COLOR,	"[1 1 1]", 0 },
	{ "$srgbtint",		 		"tint value to be applied when running on new-style srgb parts",			SHADER_PARAM_TYPE_COLOR,	"[1 1 1]", 0 },
};


//-----------------------------------------------------------------------------
// Gets the standard shader parameter names
// FIXME: Turn this into one function?
//-----------------------------------------------------------------------------
int CBaseShader::GetParamCount( ) const
{ 
	return NUM_SHADER_MATERIAL_VARS; 
}
const ShaderParamInfo_t& CBaseShader::GetParamInfo( int paramIndex ) const
{
	return s_StandardParams[ paramIndex ];
}

//-----------------------------------------------------------------------------
// Necessary to snag ahold of some important data for the helper methods
//-----------------------------------------------------------------------------
void CBaseShader::InitShaderParams( IMaterialVar** ppParams, const char *pMaterialName )
{
	// Re-entrancy check
	Assert( !s_ppParams );

	s_ppParams = ppParams;

	OnInitShaderParams( ppParams, pMaterialName );

	s_ppParams = NULL;
}

void CBaseShader::InitShaderInstance( IMaterialVar** ppParams, IShaderInit *pShaderInit, const char *pMaterialName, const char *pTextureGroupName )
{
	// Re-entrancy check
	Assert( !s_ppParams );

	s_ppParams = ppParams;
	s_pShaderInit = pShaderInit;
	s_pTextureGroupName = pTextureGroupName;

	OnInitShaderInstance( ppParams, pShaderInit, pMaterialName );

	s_pTextureGroupName = NULL;
	s_ppParams = NULL;
	s_pShaderInit = NULL;
}

void CBaseShader::DrawElements( IMaterialVar **params, int nModulationFlags, IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
								VertexCompressionType_t vertexCompression, CBasePerMaterialContextData **pContext, CBasePerInstanceContextData** pInstanceDataPtr  )
{
	VPROF("CBaseShader::DrawElements");
	// Re-entrancy check
	Assert( !s_ppParams );

	s_ppParams = params;
	s_pShaderAPI = pShaderAPI;
	s_pShaderShadow = pShaderShadow;
	s_nModulationFlags = nModulationFlags;
	//s_pMeshBuilder = pShaderAPI ? pShaderAPI->GetVertexModifyBuilder() : NULL;

	s_nPassCount = 0;
	s_pInstanceDataPtr = (CPerInstanceContextData**)pInstanceDataPtr;

	if ( IsSnapshotting() )
	{
		// Set up the shadow state
		SetInitialShadowState( );
	}

	OnDrawElements( s_ppParams, pShaderShadow, pShaderAPI, vertexCompression, pContext );

	s_pInstanceDataPtr = NULL;
	s_nPassCount = 0;

	s_nModulationFlags = 0;
	s_ppParams = NULL;
	s_pShaderAPI = NULL;
	s_pShaderShadow = NULL;
	//s_pMeshBuilder = NULL;
}


//-----------------------------------------------------------------------------
// Sets the default shadow state
//-----------------------------------------------------------------------------
void CBaseShader::SetInitialShadowState( )
{
	// Set the default state
	s_pShaderShadow->SetDefaultState();

	// Init the standard states...
	int flags = s_ppParams[FLAGS]->GetIntValue();
	if (flags & MATERIAL_VAR_IGNOREZ)
	{
		s_pShaderShadow->EnableDepthTest( false );
		s_pShaderShadow->EnableDepthWrites( false );
	}

	if (flags & MATERIAL_VAR_DECAL)
	{
		s_pShaderShadow->EnablePolyOffset( SHADER_POLYOFFSET_DECAL );
		s_pShaderShadow->EnableDepthWrites( false );
	}

	if (flags & MATERIAL_VAR_NOCULL)
	{
		s_pShaderShadow->EnableCulling( false );
	}

	if (flags & MATERIAL_VAR_ZNEARER)
	{
		s_pShaderShadow->DepthFunc( SHADER_DEPTHFUNC_NEARER );
	}

	if (flags & MATERIAL_VAR_WIREFRAME)
	{
		s_pShaderShadow->PolyMode( SHADER_POLYMODEFACE_FRONT_AND_BACK, SHADER_POLYMODE_LINE );
	}

	// Set alpha to coverage
	if (flags & MATERIAL_VAR_ALLOWALPHATOCOVERAGE)
	{
		// Force the bit on and then check against alpha blend and test states in CShaderShadowDX8::ComputeAggregateShadowState()
		s_pShaderShadow->EnableAlphaToCoverage( true );
	}
}


//-----------------------------------------------------------------------------
// Draws a snapshot
//-----------------------------------------------------------------------------
void CBaseShader::Draw( bool bMakeActualDrawCall )
{
	if ( IsSnapshotting() )
	{
		// Turn off transparency if we're asked to....
		if (g_pConfig->bNoTransparency && 
			((s_ppParams[FLAGS]->GetIntValue() & MATERIAL_VAR_NO_DEBUG_OVERRIDE) == 0))
		{
			s_pShaderShadow->EnableDepthWrites( true );
 			s_pShaderShadow->EnableBlending( false );
		}

		GetShaderSystem()->TakeSnapshot();

		// always add these if required since they are private??
		if ( (*s_pInstanceDataPtr) == NULL ||
			(*s_pInstanceDataPtr)->GetCommands( s_nPassCount ) == NULL ||
			( CShader_IsFlag2Set( s_ppParams, MATERIAL_VAR2_SUPPORTS_HW_SKINNING ) ||
			CShader_IsFlag2Set( s_ppParams, MATERIAL_VAR2_LIGHTING_VERTEX_LIT ) ) )
		{
			PI_BeginCommandBuffer();
			PI_EndCommandBuffer();
		}
	}
	else
	{
		uint8 *cmd = ( (*s_pInstanceDataPtr) != NULL ) ? (*s_pInstanceDataPtr)->GetCommands( s_nPassCount ) : NULL;
		GetShaderSystem()->DrawSnapshot( cmd, bMakeActualDrawCall );
	}

	s_nPassCount++;
}


//-----------------------------------------------------------------------------
// Finds a particular parameter	(works because the lowest parameters match the shader)
//-----------------------------------------------------------------------------
int CBaseShader::FindParamIndex( const char *pName ) const
{
	int numParams = GetParamCount();
	for( int i = 0; i < numParams; i++ )
	{
		if( Q_strnicmp( GetParamInfo( i ).m_pName, pName, 64 ) == 0 )
		{
			return i;
		}
	}
	return -1;
}


//-----------------------------------------------------------------------------
// Are we using graphics?
//-----------------------------------------------------------------------------
bool CBaseShader::IsUsingGraphics()
{
	return GetShaderSystem()->IsUsingGraphics();
}

//-----------------------------------------------------------------------------
// Loads a texture
//-----------------------------------------------------------------------------
void CBaseShader::LoadTexture( int nTextureVar )
{
	if ((!s_ppParams) || (nTextureVar == -1))
		return;

	IMaterialVar* pNameVar = s_ppParams[nTextureVar];
	if( pNameVar && pNameVar->IsDefined() )
	{
		s_pShaderInit->LoadTexture( pNameVar, s_pTextureGroupName );
	}
}


//-----------------------------------------------------------------------------
// Loads a bumpmap
//-----------------------------------------------------------------------------
void CBaseShader::LoadBumpMap( int nTextureVar )
{
	if ((!s_ppParams) || (nTextureVar == -1))
		return;

	IMaterialVar* pNameVar = s_ppParams[nTextureVar];
	if( pNameVar && pNameVar->IsDefined() )
	{
		s_pShaderInit->LoadBumpMap( pNameVar, s_pTextureGroupName );
	}
}


//-----------------------------------------------------------------------------
// Loads a cubemap
//-----------------------------------------------------------------------------
void CBaseShader::LoadCubeMap( int nTextureVar )
{
	if ((!s_ppParams) || (nTextureVar == -1))
		return;

	IMaterialVar* pNameVar = s_ppParams[nTextureVar];
	if( pNameVar && pNameVar->IsDefined() )
	{
		s_pShaderInit->LoadCubeMap( s_ppParams, pNameVar );
	}
}


ShaderAPITextureHandle_t CBaseShader::GetShaderAPITextureBindHandle( int nTextureVar, int nFrameVar, int nTextureChannel )
{
	Assert( !IsSnapshotting() );
	Assert( nTextureVar != -1 );
	Assert ( s_ppParams );

	IMaterialVar* pTextureVar = s_ppParams[nTextureVar];
	IMaterialVar* pFrameVar = (nFrameVar != -1) ? s_ppParams[nFrameVar] : NULL;
	int nFrame = pFrameVar ? pFrameVar->GetIntValue() : 0;
	return GetShaderSystem()->GetShaderAPITextureBindHandle( pTextureVar->GetTextureValue(), nFrame, nTextureChannel );
}


//-----------------------------------------------------------------------------
// Four different flavors of BindTexture(), handling the two-sampler
// case as well as ITexture* versus textureVar forms
//-----------------------------------------------------------------------------

void CBaseShader::BindTexture( Sampler_t sampler1, int nTextureVar, int nFrameVar /* = -1 */ )
{
	BindTexture( sampler1, (Sampler_t) -1, nTextureVar, nFrameVar );
}


void CBaseShader::BindTexture( Sampler_t sampler1, Sampler_t sampler2, int nTextureVar, int nFrameVar /* = -1 */ )
{
	Assert( !IsSnapshotting() );
	Assert( nTextureVar != -1 );
	Assert ( s_ppParams );

	IMaterialVar* pTextureVar = s_ppParams[nTextureVar];
	IMaterialVar* pFrameVar = (nFrameVar != -1) ? s_ppParams[nFrameVar] : NULL;
	if (pTextureVar)
	{
		int nFrame = pFrameVar ? pFrameVar->GetIntValue() : 0;

		if ( sampler2 == -1 )
		{
			GetShaderSystem()->BindTexture( sampler1, pTextureVar->GetTextureValue(), nFrame );
		}
		else
		{
			GetShaderSystem()->BindTexture( sampler1, sampler2, pTextureVar->GetTextureValue(), nFrame );
		}
	}
}


void CBaseShader::BindTexture( Sampler_t sampler1, ITexture *pTexture, int nFrame /* = 0 */ )
{
	BindTexture( sampler1, (Sampler_t) -1, pTexture, nFrame );
}

void CBaseShader::BindTexture( Sampler_t sampler1, Sampler_t sampler2, ITexture *pTexture, int nFrame /* = 0 */ )
{
	Assert( !IsSnapshotting() );

	if ( sampler2 == -1 )
	{
		GetShaderSystem()->BindTexture( sampler1, pTexture, nFrame );
	}
	else
	{
		GetShaderSystem()->BindTexture( sampler1, sampler2, pTexture, nFrame );
	}
}

void CBaseShader::BindVertexTexture( VertexTextureSampler_t vtSampler, int nTextureVar, int nFrame )
{
	Assert( !IsSnapshotting() );
	Assert( nTextureVar != -1 );
	Assert ( s_ppParams );

	IMaterialVar* pTextureVar = s_ppParams[nTextureVar];
	IMaterialVar* pFrameVar = (nFrame != -1) ? s_ppParams[nFrame] : NULL;

	if (pTextureVar)
	{
		int iF = pFrameVar ? pFrameVar->GetIntValue() : 0;

		GetShaderSystem()->BindVertexTexture( vtSampler, pTextureVar->GetTextureValue(), iF );
	}
}

//-----------------------------------------------------------------------------
// Does the texture store translucency in its alpha channel?
//-----------------------------------------------------------------------------
bool CBaseShader::TextureIsTranslucent( int textureVar, bool isBaseTexture )
{
	if (textureVar < 0)
		return false;

	IMaterialVar** params = s_ppParams;
	if (params[textureVar]->GetType() == MATERIAL_VAR_TYPE_TEXTURE)
	{
		if (!isBaseTexture)
		{
			return params[textureVar]->GetTextureValue()->IsTranslucent();
		}
		else
		{
			// Override translucency settings if this flag is set.
			if (IS_FLAG_SET(MATERIAL_VAR_OPAQUETEXTURE))
				return false;

			if ( (CurrentMaterialVarFlags() & (MATERIAL_VAR_SELFILLUM | MATERIAL_VAR_BASEALPHAENVMAPMASK)) == 0)
			{
				if ((CurrentMaterialVarFlags() & MATERIAL_VAR_TRANSLUCENT) ||
					(CurrentMaterialVarFlags() & MATERIAL_VAR_ALPHATEST))
				{
					return params[textureVar]->GetTextureValue()->IsTranslucent();
				}
			}
		}
	}

	return false;
}


//-----------------------------------------------------------------------------
// Are we alpha or color modulating?
//-----------------------------------------------------------------------------
bool CBaseShader::IsAlphaModulating()
{
	return (s_nModulationFlags & SHADER_USING_ALPHA_MODULATION) != 0;
}



//-----------------------------------------------------------------------------
// FIXME: Figure out a better way to do this?
//-----------------------------------------------------------------------------
int CBaseShader::ComputeModulationFlags( IMaterialVar** params, IShaderDynamicAPI* pShaderAPI )
{
 	s_pShaderAPI = pShaderAPI;

	int mod = 0;
	//if ( GetAlpha(params) < 1.0f )
	//{
	//	mod |= SHADER_USING_ALPHA_MODULATION;
	//}

	//float color[3];
	//GetColorParameter( params, color );

	//if ((color[0] != 1.0) || (color[1] != 1.0) || (color[2] != 1.0))
	//{
	//	mod |= SHADER_USING_COLOR_MODULATION;
	//}

	if( UsingFlashlight(params) )
	{
		mod |= SHADER_USING_FLASHLIGHT;
	}
	
	if ( UsingEditor(params) )
	{
		mod |= SHADER_USING_EDITOR;
	}

	if( IS_FLAG2_SET( MATERIAL_VAR2_USE_FIXED_FUNCTION_BAKED_LIGHTING ) )
	{
		AssertOnce( IS_FLAG2_SET( MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS ) );
		if( IS_FLAG2_SET( MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS ) )
		{
			mod |= SHADER_USING_FIXED_FUNCTION_BAKED_LIGHTING;
		}
	}

	s_pShaderAPI = NULL;

	return mod;
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
bool CBaseShader::NeedsPowerOfTwoFrameBufferTexture( IMaterialVar **params, bool bCheckSpecificToThisFrame ) const 
{ 
	return CShader_IsFlag2Set( params, MATERIAL_VAR2_NEEDS_POWER_OF_TWO_FRAME_BUFFER_TEXTURE ); 
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
bool CBaseShader::NeedsFullFrameBufferTexture( IMaterialVar **params, bool bCheckSpecificToThisFrame ) const 
{ 
	return CShader_IsFlag2Set( params, MATERIAL_VAR2_NEEDS_FULL_FRAME_BUFFER_TEXTURE ); 
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
bool CBaseShader::IsTranslucent( IMaterialVar **params ) const
{
	return IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );
}

void CBaseShader::ApplyColor2Factor( float *pColorOut ) const // (*pColorOut) *= COLOR2
{
	IMaterialVar* pColor2Var = s_ppParams[COLOR2];
	if (pColor2Var->GetType() == MATERIAL_VAR_TYPE_VECTOR)
	{
		float flColor2[3];
		pColor2Var->GetVecValue( flColor2, 3 );
		
		pColorOut[0] *= flColor2[0];
		pColorOut[1] *= flColor2[1];
		pColorOut[2] *= flColor2[2];
	}
	if ( g_pHardwareConfig->UsesSRGBCorrectBlending() )
	{
		IMaterialVar* pSRGBVar = s_ppParams[SRGBTINT];
		if (pSRGBVar->GetType() == MATERIAL_VAR_TYPE_VECTOR)
		{
			float flSRGB[3];
			pSRGBVar->GetVecValue( flSRGB, 3 );
			
			pColorOut[0] *= flSRGB[0];
			pColorOut[1] *= flSRGB[1];
			pColorOut[2] *= flSRGB[2];
		}
	}
}

//-----------------------------------------------------------------------------
//
// Helper methods for alpha blending....
//
//-----------------------------------------------------------------------------
void CBaseShader::EnableAlphaBlending( ShaderBlendFactor_t src, ShaderBlendFactor_t dst )
{
	Assert( IsSnapshotting() );
	s_pShaderShadow->EnableBlending( true );
	s_pShaderShadow->BlendFunc( src, dst );
	s_pShaderShadow->EnableDepthWrites(false);
}

void CBaseShader::DisableAlphaBlending()
{
	Assert( IsSnapshotting() );
	s_pShaderShadow->EnableBlending( false );
}

void CBaseShader::SetNormalBlendingShadowState( int textureVar, bool isBaseTexture )
{
	Assert( IsSnapshotting() );

	// Either we've got a constant modulation
	bool isTranslucent = IsAlphaModulating();

	// Or we've got a vertex alpha
	isTranslucent = isTranslucent || (CurrentMaterialVarFlags() & MATERIAL_VAR_VERTEXALPHA);

	// Or we've got a texture alpha
	isTranslucent = isTranslucent || ( TextureIsTranslucent( textureVar, isBaseTexture ) &&
		                               !(CurrentMaterialVarFlags() & MATERIAL_VAR_ALPHATEST ) );

	if (isTranslucent)
	{
		EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
	}
	else
	{
		DisableAlphaBlending();
	}
}

//ConVar mat_debug_flashlight_only( "mat_debug_flashlight_only", "0" );
void CBaseShader::SetAdditiveBlendingShadowState( int textureVar, bool isBaseTexture )
{
	Assert( IsSnapshotting() );

	// Either we've got a constant modulation
	bool isTranslucent = IsAlphaModulating();

	// Or we've got a vertex alpha
	isTranslucent = isTranslucent || (CurrentMaterialVarFlags() & MATERIAL_VAR_VERTEXALPHA);

	// Or we've got a texture alpha
	isTranslucent = isTranslucent || ( TextureIsTranslucent( textureVar, isBaseTexture ) &&
		                               !(CurrentMaterialVarFlags() & MATERIAL_VAR_ALPHATEST ) );

	/*
	if ( mat_debug_flashlight_only.GetBool() )
	{
		if (isTranslucent)
		{
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA);
			//s_pShaderShadow->EnableAlphaTest( true );
			//s_pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GREATER, 0.99f );
		}
		else
		{
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ZERO);
		}
	}
	else
	*/
	{
		if (isTranslucent)
		{
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE );
		}
		else
		{
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
		}
	}
}

void CBaseShader::SetDefaultBlendingShadowState( int textureVar, bool isBaseTexture ) 
{
	if ( CurrentMaterialVarFlags() & MATERIAL_VAR_ADDITIVE )
	{
		SetAdditiveBlendingShadowState( textureVar, isBaseTexture );
	}
	else
	{
		SetNormalBlendingShadowState( textureVar, isBaseTexture );
	}
}

void CBaseShader::SetBlendingShadowState( BlendType_t nMode )
{
	switch ( nMode )
	{
		case BT_NONE:
			DisableAlphaBlending();
			break;

		case BT_BLEND:
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			break;

		case BT_ADD:
			EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			break;

		case BT_BLENDADD:
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE );
			break;
	}
}



//-----------------------------------------------------------------------------
// Sets lightmap blending mode for single texturing
//-----------------------------------------------------------------------------
void CBaseShader::SingleTextureLightmapBlendMode( )
{
	Assert( IsSnapshotting() );

	s_pShaderShadow->EnableBlending( true );
	s_pShaderShadow->BlendFunc( SHADER_BLEND_DST_COLOR, SHADER_BLEND_SRC_COLOR );
}


//-----------------------------------------------------------------------------
//
// Helper methods for fog
//
//-----------------------------------------------------------------------------
void CBaseShader::FogToOOOverbright( void )
{
	Assert( IsSnapshotting() );
	if (( CurrentMaterialVarFlags() & MATERIAL_VAR_NOFOG ) == 0)
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_OO_OVERBRIGHT, false );
	}
	else
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED, false );
	}
}

void CBaseShader::FogToWhite( void )
{
	Assert( IsSnapshotting() );
	if (( CurrentMaterialVarFlags() & MATERIAL_VAR_NOFOG ) == 0)
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_WHITE, false );
	}
	else
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED, false );
	}
}
void CBaseShader::FogToBlack( void )
{
	Assert( IsSnapshotting() );
	if (( CurrentMaterialVarFlags() & MATERIAL_VAR_NOFOG ) == 0)
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_BLACK, false );
	}
	else
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED, false );
	}
}

void CBaseShader::FogToGrey( void )
{
	Assert( IsSnapshotting() );
	if (( CurrentMaterialVarFlags() & MATERIAL_VAR_NOFOG ) == 0)
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_GREY, false );
	}
	else
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED, false );
	}
}

void CBaseShader::FogToFogColor( void )
{
	Assert( IsSnapshotting() );
	if (( CurrentMaterialVarFlags() & MATERIAL_VAR_NOFOG ) == 0)
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_FOGCOLOR, false );
	}
	else
	{
		s_pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED, false );
	}
}

void CBaseShader::DisableFog( void )
{
	Assert( IsSnapshotting() );
	s_pShaderShadow->FogMode( SHADER_FOGMODE_DISABLED, false );
}

void CBaseShader::DefaultFog( void )
{
	if ( CurrentMaterialVarFlags() & MATERIAL_VAR_ADDITIVE )
	{
		FogToBlack();
	}
	else
	{
		FogToFogColor();
	}
}

bool CBaseShader::UsingFlashlight( IMaterialVar **params ) const
{
	if( IsSnapshotting() )
	{
		return CShader_IsFlag2Set( params, MATERIAL_VAR2_USE_FLASHLIGHT );
	}
	else
	{
		return s_pShaderAPI->InFlashlightMode();
	}
}

bool CBaseShader::UsingEditor( IMaterialVar **params ) const
{
	if( IsSnapshotting() )
	{
		return CShader_IsFlag2Set( params, MATERIAL_VAR2_USE_EDITOR );
	}
	else
	{
		return s_pShaderAPI->InEditorMode();
	}
}


bool CBaseShader::IsHDREnabled( void )
{
	// HDRFIXME!  Need to fix this for vgui materials
	HDRType_t hdr_mode=g_pHardwareConfig->GetHDRType();
	switch(hdr_mode)
	{
		case HDR_TYPE_NONE:
			return false;

		case HDR_TYPE_INTEGER:
			return true;

		case HDR_TYPE_FLOAT:
			return true;
	}
	return false;
}

void CBaseShader::PI_BeginCommandBuffer()
{
	tempBuffer.Reset();
}
void CBaseShader::PI_EndCommandBuffer()
{
	if ( CShader_IsFlag2Set( s_ppParams, MATERIAL_VAR2_SUPPORTS_HW_SKINNING ) )
		PI_SetSkinningMatrices();

	if ( CShader_IsFlag2Set( s_ppParams, MATERIAL_VAR2_LIGHTING_VERTEX_LIT ) )
		PI_SetVertexShaderLocalLighting();

	tempBuffer.PutInt( CBICMD_END );

	if ( !*s_pInstanceDataPtr )
	{
		CPerInstanceContextData *pContext = new CPerInstanceContextData();
		*s_pInstanceDataPtr = pContext;
	}

	const int size = tempBuffer.Size();
	uint8 *buf = new uint8[ size ];
	Q_memcpy( buf, tempBuffer.Base(), size );

	(*s_pInstanceDataPtr)->SetCommands( s_nPassCount, buf );
}
void CBaseShader::PI_SetPixelShaderAmbientLightCube( int nFirstRegister )
{
	tempBuffer.PutInt( CBICMD_SETPIXELSHADERAMBIENTLIGHTCUBE );
	tempBuffer.PutInt( nFirstRegister );
}
void CBaseShader::PI_SetPixelShaderLocalLighting( int nFirstRegister )
{
	tempBuffer.PutInt( CBICMD_SETPIXELSHADERLOCALLIGHTING );
	tempBuffer.PutInt( nFirstRegister );
}
void CBaseShader::PI_SetPixelShaderAmbientLightCubeLuminance( int nFirstRegister )
{
	tempBuffer.PutInt( CBICMD_SETPIXELSHADERAMBIENTLIGHTCUBELUMINANCE );
	tempBuffer.PutInt( nFirstRegister );
}
void CBaseShader::PI_SetPixelShaderGlintDamping( int nFirstRegister )
{
	tempBuffer.PutInt( CBICMD_SETPIXELSHADERGLINTDAMPING );
	tempBuffer.PutInt( nFirstRegister );
}
void CBaseShader::PI_SetVertexShaderAmbientLightCube( /*int nFirstRegister*/ )
{
	tempBuffer.PutInt( CBICMD_SETVERTEXSHADERAMBIENTLIGHTCUBE );
}
void CBaseShader::PI_SetSkinningMatrices()
{
	tempBuffer.PutInt( CBICMD_SETSKINNINGMATRICES );
}
void CBaseShader::PI_SetVertexShaderLocalLighting( )
{
	tempBuffer.PutInt( CBICMD_SETVERTEXSHADERLOCALLIGHTING );
}
void CBaseShader::PI_SetModulationPixelShaderDynamicState( int nRegister )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationPixelShaderDynamicState_LinearColorSpace_LinearScale( int nRegister, float scale )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationPixelShaderDynamicState_LinearScale( int nRegister, float scale )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationPixelShaderDynamicState_LinearScale_ScaleInW( int nRegister, float scale )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationPixelShaderDynamicState_LinearColorSpace( int nRegister )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationPixelShaderDynamicState_Identity( int nRegister )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationVertexShaderDynamicState( void )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}
void CBaseShader::PI_SetModulationVertexShaderDynamicState_LinearScale( float flScale )
{
	AssertMsgOnce( 0, "I'm stubbed!" );
}