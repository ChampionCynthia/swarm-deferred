
#include "deferred_includes.h"

#include "radiosity_gen_global_ps30.inc"
#include "radiosity_gen_vs30.inc"

BEGIN_VS_SHADER( RADIOSITY_GLOBAL, "" )
	BEGIN_SHADER_PARAMS

	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_INIT
	{
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->SetDefaultState();

			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );

			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );

			DECLARE_STATIC_VERTEX_SHADER( radiosity_gen_vs30 );
			SET_STATIC_VERTEX_SHADER( radiosity_gen_vs30 );

			DECLARE_STATIC_PIXEL_SHADER( radiosity_gen_global_ps30 );
			SET_STATIC_PIXEL_SHADER( radiosity_gen_global_ps30 );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();

			DECLARE_DYNAMIC_VERTEX_SHADER( radiosity_gen_vs30 );
			SET_DYNAMIC_VERTEX_SHADER( radiosity_gen_vs30 );

			DECLARE_DYNAMIC_PIXEL_SHADER( radiosity_gen_global_ps30 );
			SET_DYNAMIC_PIXEL_SHADER( radiosity_gen_global_ps30 );

			BindTexture( SHADER_SAMPLER0, GetDeferredExt()->GetTexture_ShadowRad_Ortho_Albedo() );
			BindTexture( SHADER_SAMPLER1, GetDeferredExt()->GetTexture_ShadowRad_Ortho_Normal() );
			BindTexture( SHADER_SAMPLER2, GetDeferredExt()->GetTexture_ShadowDepth_Ortho( 0 ) );

			const radiosityData_t &data = GetDeferredExt()->GetRadiosityData();

			Vector vecOrigin = data.vecOrigin;
			float fl0[4] = { vecOrigin.x, vecOrigin.y, vecOrigin.z };
			pShaderAPI->SetPixelShaderConstant( 0, fl0 );

			CommitShadowProjectionConstants_Ortho_Single( pShaderAPI, 0, 1 );
		}

		Draw();
	}
END_SHADER