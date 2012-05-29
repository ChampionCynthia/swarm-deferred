#ifndef CDEF_LIGHT_H
#define CDEF_LIGHT_H

#include "cbase.h"

#ifdef CLIENT_DLL
struct def_light_t;
#endif

class CDeferredLight : public CBaseEntity
{
	DECLARE_CLASS( CDeferredLight, CBaseEntity );
	DECLARE_NETWORKCLASS();
#ifdef GAME_DLL
	DECLARE_DATADESC();
#endif

public:

	CDeferredLight();
	~CDeferredLight();

#ifdef GAME_DLL
	virtual void Activate();

	virtual int UpdateTransmitState();
#else
	virtual void PostDataUpdate( DataUpdateType_t t );

	virtual void UpdateOnRemove();

	virtual void ClientThink();
#endif

	inline Vector GetColor_Diffuse();
	inline Vector GetColor_Ambient();

	inline float GetRadius();
	inline float GetFalloffPower();

	inline float GetSpotCone_Inner();
	inline float GetSpotCone_Outer();

	inline int GetVisible_Distance();
	inline int GetVisible_FadeRange();

	inline int GetShadow_Distance();
	inline int GetShadow_FadeRange();

	inline int GetStyle_Seed();
	inline float GetStyle_Amount();
	inline float GetStyle_Speed();
	inline float GetStyle_Smooth();
	inline float GetStyle_Random();

	inline int GetLight_Type();
	inline int GetLight_Flags();
	inline int GetCookieIndex();

#ifdef GAME_DLL
	void SetRadius( float r );
#endif

private:

#ifdef GAME_DLL
	bool m_bShouldTransmit;

	string_t m_str_CookieString;
	string_t m_str_Diff;
	string_t m_str_Ambient;

	void UpdateSize();
#else
	def_light_t *m_pLight;

	void ApplyDataToLight();
#endif

	CNetworkVector( m_vecColor_Diff );
	CNetworkVector( m_vecColor_Ambient );

	CNetworkVar( float, m_flRadius );
	CNetworkVar( float, m_flFalloffPower );
	CNetworkVar( float, m_flSpotConeInner );
	CNetworkVar( float, m_flSpotConeOuter );

	CNetworkVar( int, m_iVisible_Dist );
	CNetworkVar( int, m_iVisible_FadeRange );
	CNetworkVar( int, m_iShadow_Dist );
	CNetworkVar( int, m_iShadow_FadeRange );

	CNetworkVar( float, m_flStyle_Amount );
	CNetworkVar( float, m_flStyle_Speed );
	CNetworkVar( float, m_flStyle_Smooth );
	CNetworkVar( float, m_flStyle_Random );
	CNetworkVar( int, m_iStyle_Seed );

	CNetworkVar( int, m_iLightType );
	CNetworkVar( int, m_iDefFlags );
	CNetworkVar( int, m_iCookieIndex );
};


Vector CDeferredLight::GetColor_Diffuse()
{
	return m_vecColor_Diff;
}
Vector CDeferredLight::GetColor_Ambient()
{
	return m_vecColor_Ambient;
}

float CDeferredLight::GetRadius()
{
	return m_flRadius;
}
float CDeferredLight::GetFalloffPower()
{
	return m_flFalloffPower;
}

float CDeferredLight::GetSpotCone_Inner()
{
	return m_flSpotConeInner;
}
float CDeferredLight::GetSpotCone_Outer()
{
	return m_flSpotConeOuter;
}

int CDeferredLight::GetVisible_Distance()
{
	return m_iVisible_Dist;
}
int CDeferredLight::GetVisible_FadeRange()
{
	return m_iVisible_FadeRange;
}

int CDeferredLight::GetShadow_Distance()
{
	return m_iShadow_Dist;
}
int CDeferredLight::GetShadow_FadeRange()
{
	return m_iShadow_FadeRange;
}

int CDeferredLight::GetStyle_Seed()
{
	return m_iStyle_Seed;
}
float CDeferredLight::GetStyle_Amount()
{
	return m_flStyle_Amount;
}
float CDeferredLight::GetStyle_Speed()
{
	return m_flStyle_Speed;
}
float CDeferredLight::GetStyle_Smooth()
{
	return m_flStyle_Smooth;
}
float CDeferredLight::GetStyle_Random()
{
	return m_flStyle_Random;
}

int CDeferredLight::GetLight_Type()
{
	return m_iLightType;
}
int CDeferredLight::GetLight_Flags()
{
	return m_iDefFlags;
}
int CDeferredLight::GetCookieIndex()
{
	return m_iCookieIndex;
}

#endif