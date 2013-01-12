
#ifndef COMMON_DEFERRED_FXC_H
#define COMMON_DEFERRED_FXC_H

// SKIP:		!$MODEL && $MORPHING_VTEX

// SKIP:		!$MODEL && $COMPRESSED_VERTS
// SKIP:		!$MODEL && $SKINNING
// SKIP:		!$MODEL && $MORPHING_VTEX

#include "common_fxc.h"
#include "deferred_global_common.h"
#include "common_lighting_fxc.h"
#include "common_shadowmapping_fxc.h"


float WriteDepth( float3 eyeworldvec, float3 eyeforward, float scale )
{
	return dot( eyeforward, eyeworldvec ) * scale;
}
float WriteDepth( float3 worldpos, float3 eyepos, float3 eyeforward, float scale )
{
	float3 d = worldpos - eyepos;
	return WriteDepth( d, eyeforward, scale );
}

float4 WriteLighting( float4 normalizedLight )
{
#if DEFCFG_LIGHTACCUM_COMPRESSED
	return normalizedLight / DEFCFG_LIGHTSCALE_COMPRESS_RATIO;
#else
	return normalizedLight;
#endif
}

float4 ReadLighting( float4 compressedLight )
{
#if DEFCFG_LIGHTACCUM_COMPRESSED
	return compressedLight * DEFCFG_LIGHTSCALE_COMPRESS_RATIO;
#else
	return compressedLight;
#endif
}


// phong exp 6						-half lambert 1	- litface 1
// 128 + 64 + 32 + 16 + 8 + 4		- 2				- 1
// 6 bits, 63						- 1				- 1

float PackLightingControls( int phong_exp, int half_lambert, int litface )
{
	return ( litface +
		half_lambert * 2 +
		phong_exp * 4 ) * 0.0039215686f;
}

void UnpackLightingControls( float mixed,
	out float phong_exp, out bool half_lambert, out bool litface )
{
	mixed *= 255.0f;

	litface = fmod( mixed, 2.0f );
	float tmp = fmod( mixed -= litface, 4.0f );
	phong_exp = fmod( mixed -= tmp, 256.0f );

#if 0
	// normalized values
	half_lambert /= 2.0f;
	phong_exp /= 252.0f;
#else
	// pre-scaled values for lighting
	//half_lambert *= 0.5f;
	half_lambert = tmp * 0.5f;
	phong_exp = pow( SPECULAREXP_BASE, 1 + phong_exp * 0.02f );
#endif
}

// compensates for point sampling
float2 GetLightAccumUVs( float3 projXYW, float2 halfScreenTexelSize )
{
	return projXYW.xy / projXYW.z *
		float2( 0.5f, -0.5f ) +
		0.5f +
		halfScreenTexelSize;
}

float2 GetTransformedUVs( in float2 uv, in float4 transform[2] )
{
	float2 uvOut;

	uvOut.x = dot( uv, transform[0] ) + transform[0].w;
	uvOut.y = dot( uv, transform[1] ) + transform[1].w;

	return uvOut;
}

float GetModulatedBlend( in float flBlendAmount, in float2 modt )
{
	float minb = max( 0, modt.g - modt.r );
	float maxb = min( 1, modt.g + modt.r );

	return smoothstep( minb, maxb, flBlendAmount );
}

float GetMultiBlend( const float BlendAmount, inout float Remaining )
{
	float Result = smoothstep( 0.0, 1.0f, BlendAmount );
	Result = min( Result, Remaining );
	Remaining -= Result;

	return Result;
}

float GetMultiBlendModulated( in float2 modt, const float flBlendAmount, const float AlphaBlend, inout float Remaining )
{
	//modt.r = lerp( modt.r, 1, AlphaBlend );

	float Result = any( flBlendAmount )
		* GetModulatedBlend( flBlendAmount, modt );

	float alpha = 2.0 - AlphaBlend - modt.g;
	Result *= lerp( saturate( alpha ), 1, step( AlphaBlend, modt.g ) );

	Result = min( Result, Remaining );
	Remaining -= Result;

	return Result;
}

#endif