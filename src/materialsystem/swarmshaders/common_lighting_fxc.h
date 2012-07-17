
#ifndef COMMON_LIGHTING_H
#define COMMON_LIGHTING_H

static const float flWorldGridSize = RADIOSITY_BUFFER_GRID_STEP_SIZE * RADIOSITY_BUFFER_SAMPLES;
static const float flRadiosityTexelSizeHalf = 0.5f / RADIOSITY_BUFFER_RES;

float3 DoStandardCookie( sampler sCookie, float2 uvs )
{
	return tex2D( sCookie, uvs ).rgb;
}

float3 DoCubemapCookie( sampler sCubemap, float3 delta )
{
	return texCUBE( sCubemap, delta ).rgb;
}

float4 DoLightFinal( float3 diffuse, float3 ambient, float4 litdot_lamount_spec_fade )
{
#if DEFCFG_CHEAP_LIGHTS
	return float4( diffuse * litdot_lamount_spec_fade.y, 0 );
#else
	return float4( lerp( ambient,
		diffuse * ( litdot_lamount_spec_fade.x ),
		litdot_lamount_spec_fade.y ),
		litdot_lamount_spec_fade.z * litdot_lamount_spec_fade.y ) * litdot_lamount_spec_fade.w;
#endif
}

float4 DoLightFinalCookied( float3 diffuse, float3 ambient, float4 litdot_lamount_spec_fade, float3 vecCookieRGB )
{
#if DEFCFG_CHEAP_LIGHTS
	return float4( diffuse * vecCookieRGB * litdot_lamount_spec_fade.y, 0 );
#else
	return float4( lerp( ambient,
		diffuse * vecCookieRGB * ( litdot_lamount_spec_fade.x ),
		litdot_lamount_spec_fade.y ),
		litdot_lamount_spec_fade.z * litdot_lamount_spec_fade.y * dot( vecCookieRGB, float3( 0.3f, 0.59f, 0.11f ) ) )
		* litdot_lamount_spec_fade.w;
#endif
}

float3 DoRadiosity( float3 worldPos,
	sampler RadiositySampler, float3 vecRadiosityOrigin, float flRadiositySettings )
{
	float3 vecDelta = ( worldPos - vecRadiosityOrigin ) / flWorldGridSize;

#if VENDOR == VENDOR_FXC_AMD
	AMD_PRE_5K_NON_COMPLIANT
#else
	clip( 0.5f - any( floor( vecDelta ) ) );
#endif

	float2 flGridUVLocal = vecDelta.xy / RADIOSITY_BUFFER_GRIDS_PER_AXIS;

	float2 flGridIndexSplit;
	flGridIndexSplit.x = modf( vecDelta.z * RADIOSITY_BUFFER_GRIDS_PER_AXIS, flGridIndexSplit.y );

	flGridIndexSplit.x *= RADIOSITY_BUFFER_GRIDS_PER_AXIS;
	float flSampleFrac = modf( flGridIndexSplit.x, flGridIndexSplit.x );

	flGridIndexSplit /= RADIOSITY_BUFFER_GRIDS_PER_AXIS;

	float2 flGridUVLow = flGridUVLocal + flGridIndexSplit + flRadiosityTexelSizeHalf;

	flGridIndexSplit.x = modf(
		( floor( vecDelta.z * RADIOSITY_BUFFER_SAMPLES ) + 1 ) / RADIOSITY_BUFFER_GRIDS_PER_AXIS,
		flGridIndexSplit.y );
	flGridIndexSplit.y /= RADIOSITY_BUFFER_GRIDS_PER_AXIS;

	float2 flGridUVHigh = flGridUVLocal + flGridIndexSplit + flRadiosityTexelSizeHalf;

	return lerp( tex2D( RadiositySampler, flGridUVLow ).rgb,
		tex2D( RadiositySampler, flGridUVHigh ).rgb, flSampleFrac ) * flRadiositySettings.x;
}

#endif