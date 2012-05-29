
#ifndef COMMON_LIGHTING_H
#define COMMON_LIGHTING_H


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


#endif