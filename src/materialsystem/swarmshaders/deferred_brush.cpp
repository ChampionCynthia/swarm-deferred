
#include "deferred_includes.h"

#include "tier0/memdbgon.h"


BEGIN_VS_SHADER( DEFERRED_BRUSH, "" )
	BEGIN_SHADER_PARAMS

		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( SSBUMP, SHADER_PARAM_TYPE_BOOL, "", "" )

		SHADER_PARAM( LITFACE, SHADER_PARAM_TYPE_BOOL, "0", "" )

		SHADER_PARAM( PHONG_SCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONG_EXP, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( PHONG_MAP, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( PHONG_FRESNEL, SHADER_PARAM_TYPE_BOOL, "", "" )

		SHADER_PARAM( FRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "", "" )
		SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "", "" )

		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_env", "envmap" )
		SHADER_PARAM( ENVMAPTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "envmap tint" )
		SHADER_PARAM( ENVMAPCONTRAST, SHADER_PARAM_TYPE_FLOAT, "0.0", "contrast 0 == normal 1 == color*color" )
		SHADER_PARAM( ENVMAPSATURATION, SHADER_PARAM_TYPE_FLOAT, "1.0", "saturation 0 == greyscale 1 == normal" )
		SHADER_PARAM( ENVMAPMASK, SHADER_PARAM_TYPE_TEXTURE, "shadertest/shadertest_envmask", "envmap mask" )

	END_SHADER_PARAMS

	void SetupParmsGBuffer( defParms_gBuffer &p )
	{
		p.bModel = false;

		p.iAlbedo = BASETEXTURE;
		p.iBumpmap = BUMPMAP;
		p.iSSBump = SSBUMP;
		p.iPhongmap = PHONG_MAP;

		p.iAlphatestRef = ALPHATESTREFERENCE;
		p.iLitface = LITFACE;
		p.iPhongExp = PHONG_EXP;
	}

	void SetupParmsShadow( defParms_shadow &p )
	{
		p.bModel = false;
		p.iAlbedo = BASETEXTURE;
		p.iAlphatestRef = ALPHATESTREFERENCE;
	}

	void SetupParmsComposite( defParms_composite &p )
	{
		p.bModel = false;
		p.iAlbedo = BASETEXTURE;

		p.iEnvmap = ENVMAP;
		p.iEnvmapMask = ENVMAPMASK;
		p.iEnvmapTint = ENVMAPTINT;
		p.iEnvmapContrast = ENVMAPCONTRAST;
		p.iEnvmapSaturation = ENVMAPSATURATION;

		p.iAlphatestRef = ALPHATESTREFERENCE;

		p.iPhongScale = PHONG_SCALE;
		p.iPhongFresnel = PHONG_FRESNEL;

		p.iFresnelRanges = FRESNELRANGES;
	}

	bool DrawToGBuffer( IMaterialVar **params )
	{
		const bool bIsDecal = IS_FLAG_SET( MATERIAL_VAR_DECAL );
		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );

		return !bTranslucent && !bIsDecal;
	}

	SHADER_INIT_PARAMS()
	{
		// for fallback shaders
		// SWARMS VBSP BETTER FUCKING CALL MODINIT NOW
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		if ( PARM_DEFINED( BUMPMAP ) )
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );

		const bool bDrawToGBuffer = DrawToGBuffer( params );

		if ( bDrawToGBuffer )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			InitParmsGBuffer( parms_gbuffer, this, params );
		
			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			InitParmsShadowPass( parms_shadow, this, params );
		}

		defParms_composite parms_composite;
		SetupParmsComposite( parms_composite );
		InitParmsComposite( parms_composite, this, params );
	}

	SHADER_INIT
	{
		const bool bDrawToGBuffer = DrawToGBuffer( params );

		if ( bDrawToGBuffer )
		{
			defParms_gBuffer parms_gbuffer;
			SetupParmsGBuffer( parms_gbuffer );
			InitPassGBuffer( parms_gbuffer, this, params );

			defParms_shadow parms_shadow;
			SetupParmsShadow( parms_shadow );
			InitPassShadowPass( parms_shadow, this, params );
		}

		defParms_composite parms_composite;
		SetupParmsComposite( parms_composite );
		InitPassComposite( parms_composite, this, params );
	}

	SHADER_FALLBACK
	{
		const bool bTranslucent = IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT );

		if ( !GetDeferredExt()->IsDeferredLightingEnabled() || bTranslucent )
			return "LightmappedGeneric";

		return 0;
	}

	SHADER_DRAW
	{
		if ( pShaderAPI != NULL && *pContextDataPtr == NULL )
			*pContextDataPtr = new CDeferredPerMaterialContextData();

		CDeferredPerMaterialContextData *pDefContext = reinterpret_cast< CDeferredPerMaterialContextData* >(*pContextDataPtr);

		const int iDeferredRenderStage = pShaderAPI ?
			pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE )
			: DEFERRED_RENDER_STAGE_INVALID;

		const bool bDrawToGBuffer = DrawToGBuffer( params );

		Assert( pShaderAPI == NULL ||
			iDeferredRenderStage != DEFERRED_RENDER_STAGE_INVALID );

		if ( bDrawToGBuffer )
		{
			if ( pShaderShadow != NULL ||
				iDeferredRenderStage == DEFERRED_RENDER_STAGE_GBUFFER )
			{
				defParms_gBuffer parms_gbuffer;
				SetupParmsGBuffer( parms_gbuffer );
				DrawPassGBuffer( parms_gbuffer, this, params, pShaderShadow, pShaderAPI,
					vertexCompression, pDefContext );
			}
			else
				SkipPass();

			if ( pShaderShadow != NULL ||
				iDeferredRenderStage == DEFERRED_RENDER_STAGE_SHADOWPASS )
			{
				defParms_shadow parms_shadow;
				SetupParmsShadow( parms_shadow );
				DrawPassShadowPass( parms_shadow, this, params, pShaderShadow, pShaderAPI,
					vertexCompression, pDefContext );
			}
			else
				SkipPass();
		}

		if ( pShaderShadow != NULL ||
			iDeferredRenderStage == DEFERRED_RENDER_STAGE_COMPOSITION )
		{
			defParms_composite parms_composite;
			SetupParmsComposite( parms_composite );
			DrawPassComposite( parms_composite, this, params, pShaderShadow, pShaderAPI,
				vertexCompression, pDefContext );
		}
		else
			SkipPass();

		if ( pShaderAPI != NULL && pDefContext->m_bMaterialVarsChanged )
			pDefContext->m_bMaterialVarsChanged = false;
	}

END_SHADER