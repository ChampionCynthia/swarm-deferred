#ifndef DEFERRED_RT_H
#define DEFERRED_RT_H

class ITexture;

float GetDepthMapDepthResolution( float zDelta );
void DefRTsOnModeChanged();
void InitDeferredRTs( bool bInitial = false );

ITexture *GetDefRT_Normals();
ITexture *GetDefRT_Depth();
ITexture *GetDefRT_LightCtrl();
ITexture *GetDefRT_Lightaccum();

ITexture *GetDefRT_VolumePrepass();
ITexture *GetDefRT_VolumetricsBuffer( int index );

int GetShadowResolution_Spot();
int GetShadowResolution_Point();

ITexture *GetShadowColorRT_Ortho( int index );
ITexture *GetShadowDepthRT_Ortho( int index );

ITexture *GetShadowColorRT_Proj( int index );
ITexture *GetShadowDepthRT_Proj( int index );

ITexture *GetShadowColorRT_DP( int index );
ITexture *GetShadowDepthRT_DP( int index );

ITexture *GetProjectableVguiRT( int index );

#endif