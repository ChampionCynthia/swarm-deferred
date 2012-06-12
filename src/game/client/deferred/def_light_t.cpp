
#include "cbase.h"
#include "deferred/deferred_shared_common.h"

#include "view_shared.h"
#include "BSPTreeData.h"
#include "CollisionUtils.h"

class CLightLeafEnum : public ISpatialLeafEnumerator
{
public:
	bool EnumerateLeaf( int leaf, int context )
	{
		m_LeafList.AddToTail( leaf );
		return true;
	}

	CUtlVectorFixedGrowable< int, 512 > m_LeafList;
};

BEGIN_DATADESC_NO_BASE( def_light_t )

	DEFINE_KEYFIELD( pos, FIELD_VECTOR, GetLightParamName( LPARAM_ORIGIN ) ),
	DEFINE_KEYFIELD( ang, FIELD_VECTOR, GetLightParamName( LPARAM_ANGLES ) ),

	DEFINE_KEYFIELD( col_diffuse, FIELD_VECTOR, GetLightParamName( LPARAM_DIFFUSE ) ),
	DEFINE_KEYFIELD( col_ambient, FIELD_VECTOR, GetLightParamName( LPARAM_AMBIENT ) ),

	DEFINE_KEYFIELD( flRadius, FIELD_FLOAT, GetLightParamName( LPARAM_RADIUS ) ),
	DEFINE_KEYFIELD( flFalloffPower, FIELD_FLOAT, GetLightParamName( LPARAM_POWER ) ),
	DEFINE_KEYFIELD( flSpotCone_Inner, FIELD_FLOAT, GetLightParamName( LPARAM_SPOTCONE_INNER ) ),
	DEFINE_KEYFIELD( flSpotCone_Outer, FIELD_FLOAT, GetLightParamName( LPARAM_SPOTCONE_OUTER ) ),

	DEFINE_KEYFIELD( iVisible_Dist, FIELD_SHORT, GetLightParamName( LPARAM_VIS_DIST ) ),
	DEFINE_KEYFIELD( iVisible_Range, FIELD_SHORT, GetLightParamName( LPARAM_VIS_RANGE ) ),
	DEFINE_KEYFIELD( iShadow_Dist, FIELD_SHORT, GetLightParamName( LPARAM_SHADOW_DIST ) ),
	DEFINE_KEYFIELD( iShadow_Range, FIELD_SHORT, GetLightParamName( LPARAM_SHADOW_RANGE ) ),

	DEFINE_KEYFIELD( iLighttype, FIELD_CHARACTER, GetLightParamName( LPARAM_LIGHTTYPE ) ),
	DEFINE_KEYFIELD( iFlags, FIELD_CHARACTER, GetLightParamName( LPARAM_SPAWNFLAGS ) ),
	DEFINE_KEYFIELD( iCookieIndex, FIELD_CHARACTER, GetLightParamName( LPARAM_COOKIETEX ) ),

	DEFINE_KEYFIELD( flStyle_Amount, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_AMT ) ),
	DEFINE_KEYFIELD( flStyle_Speed, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_SPEED ) ),
	DEFINE_KEYFIELD( flStyle_Smooth, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_SMOOTH ) ),
	DEFINE_KEYFIELD( flStyle_Random, FIELD_FLOAT, GetLightParamName( LPARAM_STYLE_RANDOM ) ),
	DEFINE_KEYFIELD( iStyleSeed, FIELD_SHORT, GetLightParamName( LPARAM_STYLE_SEED ) ),

END_DATADESC()

def_light_t::def_light_t( bool bWorld )
{
	bWorldLight = bWorld;

	pos.Init();
	ang.Init();

	col_diffuse.Init( 1, 1, 1 );
	col_ambient.Init();

	flRadius = 256.0f;
	flFalloffPower = 2.0f;

	iLighttype = DEFLIGHTTYPE_POINT;
	iFlags = DEFLIGHT_DIRTY_XFORMS | DEFLIGHT_DIRTY_RENDERMESH;
	iCookieIndex = 0;
	iOldCookieIndex = 0;
	pCookie = NULL;

	flSpotCone_Inner = SPOT_DEGREE_TO_RAD( 35 );
	flSpotCone_Outer = SPOT_DEGREE_TO_RAD( 45 );
	flFOV = 90.0f;

	iVisible_Dist = 2048;
	iVisible_Range = 512;
	iShadow_Dist = 1536;
	iShadow_Range = 512;

	iStyleSeed = RandomInt( 0, DEFLIGHT_SEED_MAX );
	flStyle_Amount = 0;
	flStyle_Smooth = 0;
	flStyle_Random = 0;
	flStyle_Speed = 0;
	flLastRandomTime = 0;
	flLastRandomValue = 0;

	iNumLeaves = 0;

	worldTransform.Identity();

#if DEBUG
	pMesh_Debug = NULL;
	pMesh_Debug_Volumetrics = NULL;
#endif

	pMesh_World = NULL;
	pMesh_Volumetrics = NULL;
	pMesh_VolumPrepass = NULL;
}

def_light_t::~def_light_t()
{
	ClearCookie();
}

def_light_t *def_light_t::AllocateFromKeyValues( KeyValues *pKV )
{
	def_light_t *pRet = new def_light_t();

	pRet->ApplyKeyValueProperties( pKV );

	return pRet;
}

KeyValues *def_light_t::AllocateAsKeyValues()
{
	typedescription_t *dt = GetDataDescMap()->dataDesc;
	int iNumFields = GetDataDescMap()->dataNumFields;

	KeyValues *pRet = new KeyValues( "deflight" );

	for ( int iField = 0; iField < iNumFields; iField++ )
	{
		typedescription_t &pField = dt[ iField ];
		const char *pszFieldName = pField.externalName;

		if ( !Q_stricmp( pszFieldName, GetLightParamName( LPARAM_DIFFUSE ) ) ||
			!Q_stricmp( pszFieldName, GetLightParamName( LPARAM_AMBIENT ) ) )
		{
			Vector col;
			Q_memcpy( col.Base(), (void*)((int)this + (int)pField.fieldOffset), pField.fieldSizeInBytes );

			char tmp[256] = {0};
			vecToStringCol( col, tmp, sizeof( tmp ) );

			pRet->SetString( pszFieldName, tmp );
		}
		else if ( !Q_stricmp( pszFieldName, GetLightParamName( LPARAM_COOKIETEX ) ) )
		{
			int iCookieIndex = 0;
			Q_memcpy( &iCookieIndex, (void*)((int)this + (int)pField.fieldOffset), pField.fieldSizeInBytes );

			const char *pszCookieName = g_pStringTable_LightCookies->GetString( iCookieIndex );

			if ( pszCookieName != NULL && *pszCookieName )
			{
				pRet->SetString( pszFieldName, pszCookieName );
			}
		}
		else if ( !Q_stricmp( pszFieldName, GetLightParamName( LPARAM_ORIGIN ) ) ||
			!Q_stricmp( pszFieldName, GetLightParamName( LPARAM_ANGLES ) ) )
		{
			Vector pos;
			Q_memcpy( pos.Base(), (void*)((int)this + (int)pField.fieldOffset), pField.fieldSizeInBytes );

			pRet->SetString( pszFieldName, VarArgs( "%.2f %.2f %.2f", XYZ( pos ) ) );
		}
		else
		{
			switch ( pField.fieldType )
			{
			default:
					Assert( 0 );
				break;
			case FIELD_FLOAT:
				{
					float fl1 = 0;
					Q_memcpy( &fl1, (void*)((int)this + (int)pField.fieldOffset), pField.fieldSizeInBytes );

					if ( !Q_stricmp( pszFieldName, GetLightParamName( LPARAM_SPOTCONE_INNER ) ) ||
						!Q_stricmp( pszFieldName, GetLightParamName( LPARAM_SPOTCONE_OUTER ) ) )
						fl1 = SPOT_RAD_TO_DEGREE( fl1 );

					pRet->SetFloat( pszFieldName, fl1 );
				}
				break;
			case FIELD_SHORT:
			case FIELD_CHARACTER:
				{
					int it1 = 0;
					Q_memcpy( &it1, (void*)((int)this + (int)pField.fieldOffset), pField.fieldSizeInBytes );
					pRet->SetInt( pszFieldName, it1 );
				}
				break;
			}
		}
	}

	return pRet;
}

void def_light_t::ApplyKeyValueProperties( KeyValues *pKV )
{
	typedescription_t *dt = GetDataDescMap()->dataDesc;
	int iNumFields = GetDataDescMap()->dataNumFields;

	for ( KeyValues *pValue = pKV->GetFirstValue(); pValue; pValue = pValue->GetNextValue() )
	{
		const char *pszKeyName = pValue->GetName();

		if ( !pszKeyName || !*pszKeyName )
			continue;

		for ( int iField = 0; iField < iNumFields; iField++ )
		{
			typedescription_t &pField = dt[ iField ];

			if ( !Q_stricmp( pField.externalName, pszKeyName ) )
			{
				if ( !Q_stricmp( pszKeyName, GetLightParamName( LPARAM_DIFFUSE ) ) ||
					!Q_stricmp( pszKeyName, GetLightParamName( LPARAM_AMBIENT ) ) )
				{
					Vector col = stringColToVec( pValue->GetString() );
					Q_memcpy( (void*)((int)this + (int)pField.fieldOffset), col.Base(), pField.fieldSizeInBytes );
				}
				else if ( !Q_stricmp( pszKeyName, GetLightParamName( LPARAM_COOKIETEX ) ) )
				{
					const char *pszTextureName = pValue->GetString();
					IDefCookie *pCookieValidate = CreateCookieInstance( pszTextureName );

					if ( pCookieValidate != NULL )
					{
						delete pCookieValidate;

						int iAddedCookie = g_pStringTable_LightCookies->AddString( true, pszTextureName );
						Q_memcpy( (void*)((int)this + (int)pField.fieldOffset), &iAddedCookie, pField.fieldSizeInBytes );
					}
				}
				else if ( !Q_stricmp( pszKeyName, GetLightParamName( LPARAM_ORIGIN ) ) ||
					!Q_stricmp( pszKeyName, GetLightParamName( LPARAM_ANGLES ) ) )
				{
					float f[3] = { 0 };
					UTIL_StringToFloatArray( f, 3, pValue->GetString() );
					Q_memcpy( (void*)((int)this + (int)pField.fieldOffset), f, pField.fieldSizeInBytes );
				}
				else
				{
					switch ( pField.fieldType )
					{
					default:
							Assert( 0 );
						break;
					case FIELD_FLOAT:
						{
							float fl1 = pValue->GetFloat();

							if ( !Q_stricmp( pszKeyName, GetLightParamName( LPARAM_SPOTCONE_INNER ) ) ||
								!Q_stricmp( pszKeyName, GetLightParamName( LPARAM_SPOTCONE_OUTER ) ) )
								fl1 = SPOT_DEGREE_TO_RAD( fl1 );

							Q_memcpy( (void*)((int)this + (int)pField.fieldOffset), &fl1, pField.fieldSizeInBytes );
						}
						break;
					case FIELD_SHORT:
					case FIELD_CHARACTER:
						{
							int it1 = pValue->GetInt();
							Q_memcpy( (void*)((int)this + (int)pField.fieldOffset), &it1, pField.fieldSizeInBytes );
						}
						break;
					}
				}

				break;
			}
		}
	}

	MakeDirtyAll();
}

void def_light_t::UpdateCookieTexture()
{
	if ( !HasCookie() )
		return;

	if ( iOldCookieIndex == iCookieIndex &&
		pCookie != NULL && pCookie->IsValid() )
		return;

	const char *pszCookieName = g_pStringTable_LightCookies->GetString( iCookieIndex );

	if ( pszCookieName == NULL || *pszCookieName == '\0' )
		return;

	delete pCookie;
	pCookie = NULL;

	pCookie = CreateCookieInstance( pszCookieName );

	iOldCookieIndex = iCookieIndex;
}

bool def_light_t::IsCookieReady()
{
	return pCookie != NULL && pCookie->IsValid();
}

ITexture *def_light_t::GetCookieForDraw( const int iTargetIndex )
{
	Assert( pCookie != NULL );

	pCookie->PreRender( iTargetIndex );
	return pCookie->GetCookieTarget( iTargetIndex );
}

void def_light_t::SetCookie( IDefCookie *pCookie )
{
	ClearCookie();
	this->pCookie = pCookie;
}

void def_light_t::ClearCookie()
{
	delete pCookie;
	pCookie = NULL;
}

IDefCookie *def_light_t::CreateCookieInstance( const char *pszCookieName )
{
	CVGUIProjectable *pVProj = CProjectableFactory::AllocateProjectableByScript( pszCookieName );
	if ( pVProj != NULL )
		return new CDefCookieProjectable( pVProj );

	ITexture *pTex = materials->FindTexture( pszCookieName, TEXTURE_GROUP_OTHER );
	if ( !IsErrorTexture( pTex ) )
		return new CDefCookieTexture( pTex );

	return NULL;
}

void def_light_t::UpdateMatrix()
{
	Assert( iLighttype == DEFLIGHTTYPE_SPOT );

	float aCOS = acos( flSpotCone_Outer );
	flFOV = RAD2DEG( aCOS ) * 2.0f;
	//flConeRadius = tan( aCOS ) * flRadius;

	VMatrix matPerspective, matView, matViewProj;

	matView.Identity();
	matView.SetupMatrixOrgAngles( vec3_origin, ang );
	MatrixSourceToDeviceSpace( matView );
	matView = matView.Transpose3x3();

	Vector viewPosition;
	Vector3DMultiply( matView, pos, viewPosition );
	matView.SetTranslation( -viewPosition );

	MatrixBuildPerspectiveX( matPerspective, flFOV, 1, DEFLIGHT_SPOT_ZNEAR, flRadius );
	MatrixMultiply( matPerspective, matView, matViewProj );

	MatrixInverseGeneral( matViewProj, spotMVPInv );
	MatrixInverseGeneral( matPerspective, spotVPInv );

	VMatrix screenToTexture;
	MatrixBuildScale( screenToTexture, 0.5f, -0.5f, 1.0f );
	screenToTexture[0][3] = screenToTexture[1][3] = 0.5f;
	MatrixMultiply( screenToTexture, matViewProj, spotWorldToTex );

	spotWorldToTex = spotWorldToTex.Transpose();

	UpdateFrustum();
}

void def_light_t::UpdateFrustum()
{
	GeneratePerspectiveFrustum( pos, ang, DEFLIGHT_SPOT_ZNEAR, flRadius, flFOV, 1, spotFrustum );
}

void def_light_t::UpdateXForms()
{
	normalizeAngles( ang );

	Vector fwd;
	AngleVectors( ang, &fwd );
	backDir = -fwd;

	flMaxDistSqr = iVisible_Dist + iVisible_Range;
	flMaxDistSqr *= flMaxDistSqr;

	trace_t tr;

#define __ND0 1.0f
#define __ND1 0.57735f

	switch ( iLighttype )
	{
	default:
		Assert( 0 );
	case DEFLIGHTTYPE_POINT:
		{
			Vector points[14];
			const int numPoints = ARRAYSIZE( points );

			const Vector _dirs[numPoints] = {
				Vector( __ND0, __ND0, __ND0 ),
				Vector( -__ND0, __ND0, __ND0 ),
				Vector( __ND0, -__ND0, __ND0 ),
				Vector( __ND0, __ND0, -__ND0 ),

				Vector( -__ND0, -__ND0, __ND0 ),
				Vector( __ND0, -__ND0, -__ND0 ),
				Vector( -__ND0, __ND0, -__ND0 ),
				Vector( -__ND0, -__ND0, -__ND0 ),

				Vector( __ND0, 0, 0 ),
				Vector( -__ND0, 0, 0 ),
				Vector( 0, __ND0, 0 ),
				Vector( 0, -__ND0, 0 ),
				Vector( 0, 0, __ND0 ),
				Vector( 0, 0, -__ND0 ),
			};

			for ( int i = 0; i < numPoints; i++ )
				points[i] =_dirs[i] * flRadius + pos;

			Vector list[numPoints];
			CTraceFilterWorldOnly filter;
			for ( int i = 0; i < numPoints; i++ )
			{
				UTIL_TraceLine( pos, points[i], MASK_SOLID, &filter, &tr );
				list[ i ] = tr.endpos;
			}

			CalcBoundaries( list, numPoints, bounds_min, bounds_max );

			bounds_min_naive = pos - _dirs[0] * flRadius;
			bounds_max_naive = pos + _dirs[0] * flRadius;

			boundsCenter = pos;
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			Vector points[5];
			const int numPoints = ARRAYSIZE( points );

			static const Vector _normPos[4] = {
				Vector( 1, 1, 1 ),
				Vector( -1, 1, 1 ),
				Vector( -1, -1, 1 ),
				Vector( 1, -1, 1 ),
			};

			points[4] = pos;

			for ( int i = 0; i < 4; i++ )
				Vector3DMultiplyPositionProjective( spotMVPInv, _normPos[i], points[i] );

			Vector list[6];
			Q_memcpy( list, points, sizeof( _normPos ) );
			list[4] = pos + fwd * flRadius;
			list[5] = pos;

			for ( int i = 0; i < 5; i++ )
			{
				UTIL_TraceLine( pos, list[i], MASK_SOLID, NULL, COLLISION_GROUP_DEBRIS, &tr );
				list[ i ] = tr.endpos;
			}

			CalcBoundaries( list, 6, bounds_min, bounds_max );

			CalcBoundaries( points, numPoints, bounds_min_naive, bounds_max_naive );

			boundsCenter = bounds_min_naive + ( bounds_max_naive - bounds_min_naive ) * 0.5f;
		}
		break;
	}

	if ( ( bounds_max - bounds_min ).LengthSqr() < 1 )
	{
		bounds_max = pos + Vector( __ND1, __ND1, __ND1 );
		bounds_min = pos - Vector( __ND1, __ND1, __ND1 );
	}

	QAngle worldAng = ang;
	if ( IsPoint() )
		worldAng.Init();

	worldTransform.SetupMatrixOrgAngles( pos, worldAng );

	CLightLeafEnum leaves;
	ISpatialQuery* pQuery = engine->GetBSPTreeQuery();
	pQuery->EnumerateLeavesInBox( bounds_min, bounds_max, &leaves, 0 );

	AssertMsgOnce( leaves.m_LeafList.Count() <= DEFLIGHT_MAX_LEAVES, "You sure have got a huge light there, or crappy leaves." );

	iNumLeaves = MIN( DEFLIGHT_MAX_LEAVES, leaves.m_LeafList.Count() );
	Q_memcpy( iLeaveIDs, leaves.m_LeafList.Base(), sizeof(int) * iNumLeaves );
}

void def_light_t::UpdateRenderMesh()
{
	CMatRenderContextPtr pRenderContext( materials );

#if DEBUG
	if ( pMesh_Debug != NULL )
		pRenderContext->DestroyStaticMesh( pMesh_Debug );

	pMesh_Debug = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD_SIZE( 0, 2 ),
		TEXTURE_GROUP_OTHER,
		GetDeferredManager()->GetDeferredMaterial( DEF_MAT_WIREFRAME_DEBUG ) );

	switch ( iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
			BuildSphere( pMesh_Debug );
		break;
	case DEFLIGHTTYPE_SPOT:
			BuildCone( pMesh_Debug );
		break;
	}
#endif

	if ( pMesh_World != NULL )
		pRenderContext->DestroyStaticMesh( pMesh_World );

	IMaterial *pWorldMaterial = NULL;

	switch ( iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
			pWorldMaterial = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_POINT_WORLD );
		break;
	case DEFLIGHTTYPE_SPOT:
			pWorldMaterial = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_SPOT_WORLD );
		break;
	}

	if ( pWorldMaterial == NULL )
		return;

	pMesh_World = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
		TEXTURE_GROUP_OTHER, pWorldMaterial );

	switch ( iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
			BuildSphere( pMesh_World );
		break;
	case DEFLIGHTTYPE_SPOT:
			BuildCone( pMesh_World );
		break;
	}
}

void def_light_t::UpdateVolumetrics()
{
	CMatRenderContextPtr pRenderContext( materials );

#if DEBUG
	if ( pMesh_Debug_Volumetrics != NULL )
		pRenderContext->DestroyStaticMesh( pMesh_Debug_Volumetrics );
	pMesh_Debug_Volumetrics = NULL;

	if ( HasVolumetrics() )
	{
		pMesh_Debug_Volumetrics = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TEXCOORD_SIZE( 0, 2 ),
			TEXTURE_GROUP_OTHER,
			GetDeferredManager()->GetDeferredMaterial( DEF_MAT_WIREFRAME_DEBUG ) );

		switch ( iLighttype )
		{
			default:
			Assert( 0 );
			break;
		case DEFLIGHTTYPE_POINT:
				BuildSphere( pMesh_Debug_Volumetrics );
			break;
		case DEFLIGHTTYPE_SPOT:
				BuildCone( pMesh_Debug_Volumetrics );
			break;
		}
	}
#endif
	
	if ( pMesh_Volumetrics != NULL )
		pRenderContext->DestroyStaticMesh( pMesh_Volumetrics );
	pMesh_Volumetrics = NULL;

	if ( pMesh_VolumPrepass != NULL )
		pRenderContext->DestroyStaticMesh( pMesh_VolumPrepass );
	pMesh_VolumPrepass = NULL;

	if ( !HasVolumetrics() )
		return;

	switch ( iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
		{
			IMaterial *pVolumeMaterial = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_POINT_WORLD );

			if ( pVolumeMaterial != NULL )
			{
				pMesh_Volumetrics = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
					TEXTURE_GROUP_OTHER, pVolumeMaterial );
				BuildSphere( pMesh_Volumetrics );
			}
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			IMaterial *pVolumeMaterial = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_SPOT_WORLD );
			IMaterial *pVolumePrepassMaterial = GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_VOLUME_PREPASS );

			if ( pVolumeMaterial != NULL && pVolumePrepassMaterial != NULL )
			{
				pMesh_Volumetrics = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
					TEXTURE_GROUP_OTHER, pVolumeMaterial );
				BuildCone( pMesh_Volumetrics );

				pMesh_VolumPrepass = pRenderContext->CreateStaticMesh( VERTEX_POSITION | VERTEX_TEXCOORD_SIZE( 0, 2 ),
					TEXTURE_GROUP_OTHER, pVolumePrepassMaterial );
				BuildConeFlipped( pMesh_VolumPrepass );
			}
		}
		break;
	}
}


void def_light_t::BuildBox( IMesh *pMesh )
{
	CMeshBuilder meshBuilder;

	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 6 );

	meshBuilder.Position3f( -flRadius, -flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, -flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, flRadius, -flRadius );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3f( flRadius, flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, -flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, -flRadius, flRadius );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3f( -flRadius, flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, -flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, -flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, flRadius, -flRadius );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3f( flRadius, flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, flRadius, -flRadius );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3f( flRadius, -flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, -flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, -flRadius, -flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, -flRadius, flRadius );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3f( flRadius, flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( flRadius, -flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, -flRadius, flRadius );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3f( -flRadius, flRadius, flRadius );
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
}

void def_light_t::BuildSphere( IMesh *pMesh )
{
	CMeshBuilder meshBuilder;

	const float flAngle = 45.0f;
	const float halfAngle = flAngle / 2.0f;
	const float halfAngleCos = abs( FastCos( DEG2RAD( halfAngle ) ) );
	float rayMultiply = flRadius / (halfAngleCos * halfAngleCos);
	float rayMultiply_outer = flRadius / (halfAngleCos);

	const int numQuads = 8 * 4;

	meshBuilder.Begin( pMesh, MATERIAL_QUADS, numQuads );

	const int numCachedPoints = 8 * 4;
	Vector cachedPoints[numCachedPoints];

	for ( int iLevel = 0; iLevel < 4; iLevel++ )
	{
		QAngle ang( 90 - halfAngle - flAngle * iLevel,
			halfAngle,
			0 );

		Vector fwd;

		for ( int iPoint = 0; iPoint < 8; iPoint++ )
		{
			AngleVectors( ang, &fwd );

			if ( iLevel > 0 && iLevel < 3 )
				fwd *= rayMultiply;
			else
				fwd *= rayMultiply_outer;

			cachedPoints[ iLevel * 8 + iPoint ] = fwd;

			ang.y -= flAngle;
		}
	}

	for ( int iLevel = 0; iLevel < 3; iLevel++ )
	{
		for ( int iPoint = 0; iPoint < 8; iPoint++ )
		{
			int iBase2 = iPoint + 1;

			if ( iPoint == 7 )
				iBase2 = 0;

			meshBuilder.Position3fv( cachedPoints[iPoint + iLevel * 8].Base() ); meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( cachedPoints[iBase2 + iLevel * 8].Base() ); meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( cachedPoints[iBase2 + (iLevel+1) * 8].Base() ); meshBuilder.AdvanceVertex();
			meshBuilder.Position3fv( cachedPoints[iPoint + (iLevel+1) * 8].Base() ); meshBuilder.AdvanceVertex();
		}
	}

	Vector top( 0, 0, flRadius );
	Vector bottom = -top;

	for ( int iSide = 0; iSide < 2; iSide++ )
	{
		const bool bBottom = iSide == 1;

		Vector side = bBottom ? bottom : top;

		for ( int iPlane = 0; iPlane < 4; iPlane++ )
		{
			int iBase = ( bBottom ? 0 : (8*3) );
			int iPoint = iPlane * 2 + iBase;
			int iPoint2 = iPoint;

			if ( iPlane == 3 )
				iPoint2 = iBase - 2;

			if ( bBottom )
			{
				meshBuilder.Position3fv( side.Base() ); meshBuilder.AdvanceVertex();
				meshBuilder.Position3fv( cachedPoints[iPoint2+2].Base() ); meshBuilder.AdvanceVertex();
				meshBuilder.Position3fv( cachedPoints[iPoint+1].Base() ); meshBuilder.AdvanceVertex();
				meshBuilder.Position3fv( cachedPoints[iPoint].Base() ); meshBuilder.AdvanceVertex();
			}
			else
			{
				meshBuilder.Position3fv( cachedPoints[iPoint].Base() ); meshBuilder.AdvanceVertex();
				meshBuilder.Position3fv( cachedPoints[iPoint+1].Base() ); meshBuilder.AdvanceVertex();
				meshBuilder.Position3fv( cachedPoints[iPoint2+2].Base() ); meshBuilder.AdvanceVertex();
				meshBuilder.Position3fv( side.Base() ); meshBuilder.AdvanceVertex();
			}
		}
	}

	meshBuilder.End();
}

void def_light_t::BuildCone( IMesh *pMesh )
{
	CMeshBuilder meshBuilder;

	static const Vector _normPos[4] = {
		Vector( 1, 1, 1 ),
		Vector( -1, 1, 1 ),
		Vector( -1, -1, 1 ),
		Vector( 1, -1, 1 ),
	};

	Vector world[4];

	for ( int i = 0; i < 4; i++ )
	{
		Vector3DMultiplyPositionProjective( spotVPInv, _normPos[i], world[i] );

		VectorDeviceToSourceSpace( world[i] );
	}

	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 6 );

	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[1].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[3].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[1].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[3].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[1].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[3].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
}

void def_light_t::BuildConeFlipped( IMesh *pMesh )
{
	CMeshBuilder meshBuilder;

	// same as worldmesh, just flipped
	// flipping culling the shader seems broken, don't wanna even try flipping it from the client.
	static const Vector _normPos[4] = {
		Vector( 1, 1, 1 ),
		Vector( -1, 1, 1 ),
		Vector( -1, -1, 1 ),
		Vector( 1, -1, 1 ),
	};

	Vector world[4];

	for ( int i = 0; i < 4; i++ )
	{
		Vector3DMultiplyPositionProjective( spotVPInv, _normPos[i], world[i] );

		VectorDeviceToSourceSpace( world[i] );
	}

	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 6 );

	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[1].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( world[3].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[1].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[3].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( world[1].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[2].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.Position3fv( world[3].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( world[0].Base() );
	meshBuilder.AdvanceVertex();
	meshBuilder.Position3fv( vec3_origin.Base() );
	meshBuilder.AdvanceVertex();

	meshBuilder.End();
}
