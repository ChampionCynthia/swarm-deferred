
#include "deferred_includes.h"

#include "gbuffer_vs30.inc"
#include "gbuffer_ps30.inc"

#include "tier0/memdbgon.h"

static CCommandBufferBuilder< CFixedCommandStorageBuffer< 512 > > tmpBuf;

void InitParmsGBuffer( const defParms_gBuffer &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_NO_DEFAULT( info.iAlphatestRef ) ||
		PARM_VALID( info.iAlphatestRef ) && PARM_FLOAT( info.iAlphatestRef ) == 0.0f )
		params[ info.iAlphatestRef ]->SetFloatValue( DEFAULT_ALPHATESTREF );

	PARM_INIT_FLOAT( info.iPhongExp, DEFAULT_PHONG_EXP );
	PARM_INIT_FLOAT( info.iPhongExp2, DEFAULT_PHONG_EXP );
}

void InitPassGBuffer( const defParms_gBuffer &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_DEFINED( info.iBumpmap ) )
		pShader->LoadBumpMap( info.iBumpmap );

	if ( PARM_DEFINED( info.iBumpmap2 ) )
		pShader->LoadBumpMap( info.iBumpmap2 );

	if ( PARM_DEFINED( info.iBlendmodulate ) )
		pShader->LoadTexture( info.iBlendmodulate );

	if ( PARM_DEFINED( info.iAlbedo ) )
		pShader->LoadTexture( info.iAlbedo );

	if ( PARM_DEFINED( info.iPhongmap ) )
		pShader->LoadTexture( info.iPhongmap );
}

void DrawPassGBuffer( const defParms_gBuffer &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext )
{
	const bool bModel = info.bModel;
	const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
	const bool bFastVTex = g_pHardwareConfig->HasFastVertexTextures();
	const bool bNoCull = IS_FLAG_SET( MATERIAL_VAR_NOCULL );

	const bool bAlbedo = PARM_TEX( info.iAlbedo );
	const bool bBumpmap = PARM_TEX( info.iBumpmap );
	const bool bBumpmap2 = bBumpmap && PARM_TEX( info.iBumpmap2 );
	const bool bPhongmap = PARM_TEX( info.iPhongmap );
	const bool bBlendmodulate = bBumpmap2 && PARM_TEX( info.iBlendmodulate );

	const bool bAlphatest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) && bAlbedo;
	const bool bSSBump = bBumpmap && PARM_SET( info.iSSBump );

	Assert( !bIsDecal );

	SHADOW_STATE
	{
		pShaderShadow->SetDefaultState();

		pShaderShadow->EnableSRGBWrite( false );

		if ( bNoCull )
		{
			pShaderShadow->EnableCulling( false );
		}

		int iVFmtFlags = VERTEX_POSITION | VERTEX_NORMAL;
		int iUserDataSize = 0;
		int pTexCoordDim[3] = { 2, 0, 3 };
		int iTexCoordNum = ( bModel && bIsDecal && bFastVTex ) ? 3 : 1;

		if ( bModel )
		{
			iVFmtFlags |= VERTEX_FORMAT_COMPRESSED;
		}
		else
		{
			if ( bBumpmap2 )
				iVFmtFlags |= VERTEX_COLOR;
		}

		if ( bAlphatest )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
		}

		if ( bBumpmap )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );

			if ( bModel )
				iUserDataSize = 4;
			else
			{
				iVFmtFlags |= VERTEX_TANGENT_SPACE;
			}
		}

		if ( bPhongmap )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );
		}

		if ( bBumpmap2 )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );

			if ( bBlendmodulate )
				pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
		}

		pShaderShadow->EnableAlphaWrites( true );

		pShaderShadow->VertexShaderVertexFormat( iVFmtFlags, iTexCoordNum, pTexCoordDim, iUserDataSize );

		DECLARE_STATIC_VERTEX_SHADER( gbuffer_vs30 );
		SET_STATIC_VERTEX_SHADER_COMBO( MODEL, bModel );
		SET_STATIC_VERTEX_SHADER_COMBO( MORPHING_VTEX, bModel && bFastVTex );
		SET_STATIC_VERTEX_SHADER_COMBO( TANGENTSPACE, bBumpmap );
		SET_STATIC_VERTEX_SHADER_COMBO( BUMPMAP2, bBumpmap2 );
		SET_STATIC_VERTEX_SHADER_COMBO( BLENDMODULATE, bBlendmodulate );
		SET_STATIC_VERTEX_SHADER( gbuffer_vs30 );

		DECLARE_STATIC_PIXEL_SHADER( gbuffer_ps30 );
		SET_STATIC_PIXEL_SHADER_COMBO( ALPHATEST, bAlphatest );
		SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP, bBumpmap ? bSSBump ? 2 : 1 : 0 );
		SET_STATIC_PIXEL_SHADER_COMBO( NOCULL, bNoCull );
		SET_STATIC_PIXEL_SHADER_COMBO( PHONGMAP, bPhongmap );
		SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP2, bBumpmap2 );
		SET_STATIC_PIXEL_SHADER_COMBO( BLENDMODULATE, bBlendmodulate );
		SET_STATIC_PIXEL_SHADER( gbuffer_ps30 );
	}
	DYNAMIC_STATE
	{
		Assert( pDeferredContext != NULL );

		if ( pDeferredContext->m_bMaterialVarsChanged || !pDeferredContext->HasCommands( CDeferredPerMaterialContextData::DEFSTAGE_GBUFFER ) )
		{
			tmpBuf.Reset();

			if ( bAlphatest )
			{
				PARM_VALIDATE( info.iAlphatestRef );

				tmpBuf.BindTexture( pShader, SHADER_SAMPLER0, info.iAlbedo );

				tmpBuf.SetPixelShaderConstant1( 0, PARM_FLOAT( info.iAlphatestRef ) );
			}

			if ( bBumpmap )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER1, info.iBumpmap );

			if ( bPhongmap )
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER2, info.iPhongmap );
			else
			{
				float flPhongExp[2] = { 0 };
				flPhongExp[0] = clamp( PARM_FLOAT( info.iPhongExp ), 0, 1 ) * 63.0f;

				if ( bBumpmap2 )
				{
					PARM_VALIDATE( info.iPhongExp2 );

					flPhongExp[1] = clamp( PARM_FLOAT( info.iPhongExp2 ), 0, 1 ) * 63.0f;
					tmpBuf.SetPixelShaderConstant2( 2, flPhongExp[0], flPhongExp[1] );
				}
				else
					tmpBuf.SetPixelShaderConstant1( 2, flPhongExp[0] );
			}

			if ( bBumpmap2 )
			{
				tmpBuf.BindTexture( pShader, SHADER_SAMPLER3, info.iBumpmap2 );

				if ( bBlendmodulate )
				{
					tmpBuf.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, info.iBlendmodulateTransform );
					tmpBuf.BindTexture( pShader, SHADER_SAMPLER4, info.iBlendmodulate );
				}
			}

			tmpBuf.SetPixelShaderConstant2( 1,
				IS_FLAG_SET( MATERIAL_VAR_HALFLAMBERT ) ? 1.0f : 0.0f,
				PARM_SET( info.iLitface ) ? 1.0f : 0.0f );

			tmpBuf.End();

			pDeferredContext->SetCommands( CDeferredPerMaterialContextData::DEFSTAGE_GBUFFER, tmpBuf.Copy() );
		}

		pShaderAPI->SetDefaultState();

		if ( bModel && bFastVTex )
			pShader->SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );
		
		DECLARE_DYNAMIC_VERTEX_SHADER( gbuffer_vs30 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (bModel && (int)vertexCompression) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, (bModel && pShaderAPI->GetCurrentNumBones() > 0) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( MORPHING, (bModel && pShaderAPI->IsHWMorphingEnabled()) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER( gbuffer_vs30 );

		DECLARE_DYNAMIC_PIXEL_SHADER( gbuffer_ps30 );
		SET_DYNAMIC_PIXEL_SHADER( gbuffer_ps30 );

		if ( bModel && bFastVTex )
		{
			bool bUnusedTexCoords[3] = { false, true, !pShaderAPI->IsHWMorphingEnabled() || !bIsDecal };
			pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
		}

		float vPos[4] = {0,0,0,0};
		pShaderAPI->GetWorldSpaceCameraPosition( vPos );
		float zScale[4] = {GetDeferredExt()->GetZScale(),0,0,0};
		pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, vPos );
		pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, GetDeferredExt()->GetForwardBase() );
		pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, zScale );

		pShaderAPI->ExecuteCommandBuffer( pDeferredContext->GetCommands( CDeferredPerMaterialContextData::DEFSTAGE_GBUFFER ) );
	}

	pShader->Draw();
}



// testing my crappy math
float PackLightingControls( int phong_exp, int half_lambert, int litface )
{
	return ( litface +
		half_lambert * 2 +
		phong_exp * 4 ) / 255.0f;
}

void UnpackLightingControls( float mixed,
	float &phong_exp, float &half_lambert, float &litface )
{
	mixed *= 255.0f;

	litface = fmod( mixed, 2.0f );
	half_lambert = fmod( mixed -= litface, 4.0f );
	phong_exp = fmod( mixed -= half_lambert, 256.0f );

	half_lambert /= 2.0f;
	phong_exp /= 252.0f;
}

static uint8 packed;

CON_COMMAND( test_packing, "" )
{
	if ( args.ArgC() < 4 )
		return;

	float res = PackLightingControls( atoi( args[1] ),
		atoi( args[2] ),
		atoi( args[3] ) );

	res *= 255.0f;

	packed = res;

	Msg( "packed to: %u\n", packed );
}

CON_COMMAND( test_unpacking, "" )
{
	float o0,o1,o2;

	UnpackLightingControls( packed / 255.0f, o0, o1, o2 );

	Msg( "unpacked to: exp %f, halfl %f, litface %f\n", o0, o1, o2 );
}