
#include "deferred_includes.h"

#include "shadowpass_vs30.inc"
#include "shadowpass_ps30.inc"

#include "tier0/memdbgon.h"

static CCommandBufferBuilder< CFixedCommandStorageBuffer< 512 > > tmpBuf;

void InitParmsShadowPass( const defParms_shadow &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_NO_DEFAULT( info.iAlphatestRef ) ||
		PARM_VALID( info.iAlphatestRef ) && PARM_FLOAT( info.iAlphatestRef ) == 0.0f )
		params[ info.iAlphatestRef ]->SetFloatValue( DEFAULT_ALPHATESTREF );
}

void InitPassShadowPass( const defParms_shadow &info, CBaseVSShader *pShader, IMaterialVar **params )
{
	if ( PARM_DEFINED( info.iAlbedo ) )
		pShader->LoadTexture( info.iAlbedo );
}

void DrawPassShadowPass( const defParms_shadow &info, CBaseVSShader *pShader, IMaterialVar **params,
	IShaderShadow* pShaderShadow, IShaderDynamicAPI* pShaderAPI,
	VertexCompressionType_t vertexCompression, CDeferredPerMaterialContextData *pDeferredContext )
{
	const bool bModel = info.bModel;
	const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
	const bool bFastVTex = g_pHardwareConfig->HasFastVertexTextures();
	const bool bNoCull = IS_FLAG_SET( MATERIAL_VAR_NOCULL );

	const bool bAlbedo = PARM_TEX( info.iAlbedo );
	const bool bAlphatest = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) && bAlbedo;

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

		if ( bAlphatest )
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
		}

		pShaderShadow->VertexShaderVertexFormat( iVFmtFlags, iTexCoordNum, pTexCoordDim, iUserDataSize );

		DECLARE_STATIC_VERTEX_SHADER_OLD( shadowpass_vs30 );
		SET_STATIC_VERTEX_SHADER_COMBO_OLD( MODEL, bModel );
		SET_STATIC_VERTEX_SHADER_COMBO_OLD( MORPHING_VTEX, bModel && bFastVTex );
		SET_STATIC_VERTEX_SHADER_OLD( shadowpass_vs30 );

		DECLARE_STATIC_PIXEL_SHADER_OLD( shadowpass_ps30 );
		SET_STATIC_PIXEL_SHADER_COMBO_OLD( ALPHATEST, bAlphatest );
		SET_STATIC_PIXEL_SHADER_OLD( shadowpass_ps30 );
	}
	DYNAMIC_STATE
	{
		Assert( pDeferredContext != NULL );
		const int shadowMode = pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_MODE );

		if ( pDeferredContext->m_bMaterialVarsChanged || !pDeferredContext->HasCommands( CDeferredPerMaterialContextData::DEFSTAGE_SHADOW ) )
		{
			tmpBuf.Reset();

			if ( bAlphatest )
			{
				PARM_VALIDATE( info.iAlphatestRef );

				tmpBuf.BindTexture( pShader, SHADER_SAMPLER0, info.iAlbedo );
				tmpBuf.SetPixelShaderConstant1( 0, PARM_FLOAT( info.iAlphatestRef ) );
			}

			tmpBuf.End();

			pDeferredContext->SetCommands( CDeferredPerMaterialContextData::DEFSTAGE_SHADOW, tmpBuf.Copy() );
		}

		pShaderAPI->SetDefaultState();

		if ( bModel && bFastVTex )
			pShader->SetHWMorphVertexShaderState( VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, VERTEX_SHADER_SHADER_SPECIFIC_CONST_11, SHADER_VERTEXTEXTURE_SAMPLER0 );

		DECLARE_DYNAMIC_VERTEX_SHADER_OLD( shadowpass_vs30 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO_OLD( COMPRESSED_VERTS, (bModel && (int)vertexCompression) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO_OLD( SKINNING, (bModel && pShaderAPI->GetCurrentNumBones() > 0) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO_OLD( MORPHING, (bModel && pShaderAPI->IsHWMorphingEnabled()) ? 1 : 0 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO_OLD( SHADOW_MODE, shadowMode );
		SET_DYNAMIC_VERTEX_SHADER_OLD( shadowpass_vs30 );

		DECLARE_DYNAMIC_PIXEL_SHADER_OLD( shadowpass_ps30 );
		SET_DYNAMIC_PIXEL_SHADER_COMBO_OLD( SHADOW_MODE, shadowMode );
		SET_DYNAMIC_PIXEL_SHADER_OLD( shadowpass_ps30 );

		if ( bModel && bFastVTex )
		{
			bool bUnusedTexCoords[3] = { false, true, !pShaderAPI->IsHWMorphingEnabled() || !bIsDecal };
			pShaderAPI->MarkUnusedVertexFields( 0, 3, bUnusedTexCoords );
		}

		pShaderAPI->ExecuteCommandBuffer( pDeferredContext->GetCommands( CDeferredPerMaterialContextData::DEFSTAGE_SHADOW ) );

		switch ( shadowMode )
		{
		case DEFERRED_SHADOW_MODE_ORTHO:
			{
				CommitShadowcastingConstants_Ortho( pShaderAPI,
					pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX ),
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, VERTEX_SHADER_SHADER_SPECIFIC_CONST_1,
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_2
					);
			}
			break;
		case DEFERRED_SHADOW_MODE_PROJECTED:
			{
				CommitShadowcastingConstants_Proj( pShaderAPI,
					pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX ),
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, VERTEX_SHADER_SHADER_SPECIFIC_CONST_1,
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_2
					);
			}
			break;
		}

#ifdef SHADOWMAPPING_USE_COLOR
		CommitViewVertexShader( pShaderAPI, VERTEX_SHADER_SHADER_SPECIFIC_CONST_7 );
#endif
	}

	pShader->Draw();
}