#ifndef DEFERRED_UTILITY_H
#define DEFERRED_UTILITY_H


#define DEFAULT_ALPHATESTREF 0.5f
#define DEFAULT_PHONG_SCALE 0.3f
#define DEFAULT_PHONG_EXP 0.15f
#define DEFAULT_PHONG_BOOST 1.0f

#define PARM_VALID( x ) ( x != -1 )

#define PARM_DEFINED( x ) ( x != -1 &&\
	params[ x ]->IsDefined() == true )

#define PARM_NO_DEFAULT( x ) ( PARM_VALID( x ) &&\
	!PARM_DEFINED( x ) )

#define PARM_SET( x ) ( PARM_DEFINED( x ) &&\
	params[ x ]->GetIntValue() != 0 )

#define PARM_TEX( x ) ( PARM_DEFINED( x ) &&\
	params[ x ]->IsTexture() == true )

#define PARM_FLOAT( x ) ( params[x]->GetFloatValue() )

#define PARM_INT( x ) ( params[x]->GetIntValue() )

#define PARM_VALIDATE( x ) Assert( PARM_DEFINED( x ) );


#include "defpass_gbuffer.h"


// psh ## shader is used here to generate a warning if you don't ever call SET_DYNAMIC_PIXEL_SHADER
#define DECLARE_DYNAMIC_PIXEL_SHADER_OLD( shader ) \
	int declaredynpixshader_ ## shader ## _missingcurlybraces = 0; \
	declaredynpixshader_ ## shader ## _missingcurlybraces = declaredynpixshader_ ## shader ## _missingcurlybraces; \
	shader ## _Dynamic_Index _pshIndex; \
	int psh ## shader = 0

// vsh ## shader is used here to generate a warning if you don't ever call SET_DYNAMIC_VERTEX_SHADER
#define DECLARE_DYNAMIC_VERTEX_SHADER_OLD( shader ) \
	int declaredynvertshader_ ## shader ## _missingcurlybraces = 0; \
	declaredynvertshader_ ## shader ## _missingcurlybraces = declaredynvertshader_ ## shader ## _missingcurlybraces; \
	shader ## _Dynamic_Index _vshIndex; \
	int vsh ## shader = 0


// psh ## shader is used here to generate a warning if you don't ever call SET_STATIC_PIXEL_SHADER
#define DECLARE_STATIC_PIXEL_SHADER_OLD( shader ) \
	int declarestaticpixshader_ ## shader ## _missingcurlybraces = 0; \
	declarestaticpixshader_ ## shader ## _missingcurlybraces = declarestaticpixshader_ ## shader ## _missingcurlybraces; \
	shader ## _Static_Index _pshIndex; \
	int psh ## shader = 0

// vsh ## shader is used here to generate a warning if you don't ever call SET_STATIC_VERTEX_SHADER
#define DECLARE_STATIC_VERTEX_SHADER_OLD( shader ) \
	int declarestaticvertshader_ ## shader ## _missingcurlybraces = 0; \
	declarestaticvertshader_ ## shader ## _missingcurlybraces = declarestaticvertshader_ ## shader ## _missingcurlybraces; \
	shader ## _Static_Index _vshIndex; \
	int vsh ## shader = 0


// psh_forgot_to_set_dynamic_ ## var is used to make sure that you set all
// all combos.  If you don't, you will get an undefined variable used error 
// in the SET_DYNAMIC_PIXEL_SHADER block.
#define SET_DYNAMIC_PIXEL_SHADER_COMBO_OLD( var, val ) \
	int dynpixshadercombo_ ## var ## _missingcurlybraces = 0; \
	dynpixshadercombo_ ## var ## _missingcurlybraces = dynpixshadercombo_ ## var ## _missingcurlybraces; \
	_pshIndex.Set ## var( ( val ) ); \
	int psh_forgot_to_set_dynamic_ ## var = 0

// vsh_forgot_to_set_dynamic_ ## var is used to make sure that you set all
// all combos.  If you don't, you will get an undefined variable used error 
// in the SET_DYNAMIC_VERTEX_SHADER block.
#define SET_DYNAMIC_VERTEX_SHADER_COMBO_OLD( var, val ) \
	int dynvertshadercombo_ ## var ## _missingcurlybraces = 0; \
	dynvertshadercombo_ ## var ## _missingcurlybraces = dynvertshadercombo_ ## var ## _missingcurlybraces; \
	_vshIndex.Set ## var( ( val ) ); \
	int vsh_forgot_to_set_dynamic_ ## var = 0


// psh_forgot_to_set_static_ ## var is used to make sure that you set all
// all combos.  If you don't, you will get an undefined variable used error 
// in the SET_STATIC_PIXEL_SHADER block.
#define SET_STATIC_PIXEL_SHADER_COMBO_OLD( var, val ) \
	int staticpixshadercombo_ ## var ## _missingcurlybraces = 0; \
	staticpixshadercombo_ ## var ## _missingcurlybraces = staticpixshadercombo_ ## var ## _missingcurlybraces; \
	_pshIndex.Set ## var( ( val ) ); \
	int psh_forgot_to_set_static_ ## var = 0

// vsh_forgot_to_set_static_ ## var is used to make sure that you set all
// all combos.  If you don't, you will get an undefined variable used error 
// in the SET_STATIC_VERTEX_SHADER block.
#define SET_STATIC_VERTEX_SHADER_COMBO_OLD( var, val ) \
	int staticvertshadercombo_ ## var ## _missingcurlybraces = 0; \
	staticvertshadercombo_ ## var ## _missingcurlybraces = staticvertshadercombo_ ## var ## _missingcurlybraces; \
	_vshIndex.Set ## var( ( val ) ); \
	int vsh_forgot_to_set_static_ ## var = 0


// psh_testAllCombos adds up all of the psh_forgot_to_set_dynamic_ ## var's from 
// SET_DYNAMIC_PIXEL_SHADER_COMBO so that an error is generated if they aren't set.
// psh_testAllCombos is set to itself to avoid an unused variable warning.
// psh ## shader being set to itself ensures that DECLARE_DYNAMIC_PIXEL_SHADER 
// was called for this particular shader.
#define SET_DYNAMIC_PIXEL_SHADER_OLD( shader ) \
	int dynamicpixshader_ ## shader ## _missingcurlybraces = 0; \
	dynamicpixshader_ ## shader ## _missingcurlybraces = dynamicpixshader_ ## shader ## _missingcurlybraces; \
	int psh_testAllCombos = shaderDynamicTest_ ## shader; \
	psh_testAllCombos = psh_testAllCombos; \
	psh ## shader = psh ## shader; \
	pShaderAPI->SetPixelShaderIndex( _pshIndex.GetIndex() )

#define SET_DYNAMIC_PIXEL_SHADER_CMD_OLD( cmdstream, shader ) \
	int dynamicpixshader_ ## shader ## _missingcurlybraces = 0; \
	dynamicpixshader_ ## shader ## _missingcurlybraces = dynamicpixshader_ ## shader ## _missingcurlybraces; \
	int psh_testAllCombos = shaderDynamicTest_ ## shader; \
	psh_testAllCombos = psh_testAllCombos; \
	psh ## shader = psh ## shader; \
	cmdstream.SetPixelShaderIndex( _pshIndex.GetIndex() )


// vsh_testAllCombos adds up all of the vsh_forgot_to_set_dynamic_ ## var's from 
// SET_DYNAMIC_VERTEX_SHADER_COMBO so that an error is generated if they aren't set.
// vsh_testAllCombos is set to itself to avoid an unused variable warning.
// vsh ## shader being set to itself ensures that DECLARE_DYNAMIC_VERTEX_SHADER 
// was called for this particular shader.
#define SET_DYNAMIC_VERTEX_SHADER_OLD( shader ) \
	int dynamicvertshader_ ## shader ## _missingcurlybraces = 0; \
	dynamicvertshader_ ## shader ## _missingcurlybraces = dynamicvertshader_ ## shader ## _missingcurlybraces; \
	int vsh_testAllCombos = shaderDynamicTest_ ## shader; \
	vsh_testAllCombos = vsh_testAllCombos; \
	vsh ## shader = vsh ## shader; \
	pShaderAPI->SetVertexShaderIndex( _vshIndex.GetIndex() )

#define SET_DYNAMIC_VERTEX_SHADER_CMD_OLD( cmdstream, shader ) \
	int dynamicvertshader_ ## shader ## _missingcurlybraces = 0; \
	dynamicvertshader_ ## shader ## _missingcurlybraces = dynamicvertshader_ ## shader ## _missingcurlybraces; \
	int vsh_testAllCombos = shaderDynamicTest_ ## shader; \
	vsh_testAllCombos = vsh_testAllCombos; \
	vsh ## shader = vsh ## shader; \
	cmdstream.SetVertexShaderIndex( _vshIndex.GetIndex() )


// psh_testAllCombos adds up all of the psh_forgot_to_set_static_ ## var's from 
// SET_STATIC_PIXEL_SHADER_COMBO so that an error is generated if they aren't set.
// psh_testAllCombos is set to itself to avoid an unused variable warning.
// psh ## shader being set to itself ensures that DECLARE_STATIC_PIXEL_SHADER 
// was called for this particular shader.
#define SET_STATIC_PIXEL_SHADER_OLD( shader ) \
	int staticpixshader_ ## shader ## _missingcurlybraces = 0; \
	staticpixshader_ ## shader ## _missingcurlybraces = staticpixshader_ ## shader ## _missingcurlybraces; \
	int psh_testAllCombos = shaderStaticTest_ ## shader; \
	psh_testAllCombos = psh_testAllCombos; \
	psh ## shader = psh ## shader; \
	pShaderShadow->SetPixelShader( #shader, _pshIndex.GetIndex() )

// vsh_testAllCombos adds up all of the vsh_forgot_to_set_static_ ## var's from 
// SET_STATIC_VERTEX_SHADER_COMBO so that an error is generated if they aren't set.
// vsh_testAllCombos is set to itself to avoid an unused variable warning.
// vsh ## shader being set to itself ensures that DECLARE_STATIC_VERTEX_SHADER 
// was called for this particular shader.
#define SET_STATIC_VERTEX_SHADER_OLD( shader ) \
	int staticvertshader_ ## shader ## _missingcurlybraces = 0; \
	staticvertshader_ ## shader ## _missingcurlybraces = staticvertshader_ ## shader ## _missingcurlybraces; \
	int vsh_testAllCombos = shaderStaticTest_ ## shader; \
	vsh_testAllCombos = vsh_testAllCombos; \
	vsh ## shader = vsh ## shader; \
	pShaderShadow->SetVertexShader( #shader, _vshIndex.GetIndex() )

#endif