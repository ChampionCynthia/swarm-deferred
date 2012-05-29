//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Responsible for drawing the scene
//
//===========================================================================//

#include "cbase.h"
#include "view.h"
#include "iviewrender.h"
#include "view_shared.h"
#include "ivieweffects.h"
#include "iinput.h"
#include "model_types.h"
#include "clientsideeffects.h"
#include "particlemgr.h"
#include "viewrender.h"
#include "iclientmode.h"
#include "voice_status.h"
#include "glow_overlay.h"
#include "materialsystem/imesh.h"
#include "materialsystem/ITexture.h"
#include "materialsystem/IMaterial.h"
#include "materialsystem/IMaterialVar.h"
#include "materialsystem/imaterialsystem.h"
#include "DetailObjectSystem.h"
#include "tier0/vprof.h"
#include "tier1/mempool.h"
#include "vstdlib/jobthread.h"
#include "datacache/imdlcache.h"
#include "engine/IEngineTrace.h"
#include "engine/ivmodelinfo.h"
#include "tier0/icommandline.h"
#include "view_scene.h"
#include "particles_ez.h"
#include "engine/IStaticPropMgr.h"
#include "engine/ivdebugoverlay.h"
#include "c_pixel_visibility.h"
#include "precache_register.h"
#include "c_rope.h"
#include "c_effects.h"
#include "smoke_fog_overlay.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "vgui_int.h"
#include "ienginevgui.h"
#include "ScreenSpaceEffects.h"
#include "toolframework_client.h"
#include "c_func_reflective_glass.h"
#include "keyvalues.h"
#include "renderparm.h"
#include "modelrendersystem.h"
#include "vgui/ISurface.h"
#include "tier1/callqueue.h"


#include "rendertexture.h"
#include "viewpostprocess.h"
#include "viewdebug.h"

#include "shadereditor/shadereditorsystem.h"
#include "deferred/deferred_shared_common.h"


// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


extern ConVar r_visocclusion;
extern ConVar vcollide_wireframe;
extern ConVar mat_motion_blur_enabled;
extern ConVar r_depthoverlay;

//-----------------------------------------------------------------------------
// Convars related to controlling rendering
//-----------------------------------------------------------------------------
extern ConVar cl_maxrenderable_dist;

extern ConVar r_entityclips; //FIXME: Nvidia drivers before 81.94 on cards that support user clip planes will have problems with this, require driver update? Detect and disable?

// Matches the version in the engine
extern ConVar r_drawopaqueworld;
extern ConVar r_drawtranslucentworld;
extern ConVar r_3dsky;
extern ConVar r_skybox;
extern ConVar r_drawviewmodel;
extern ConVar r_drawtranslucentrenderables;
extern ConVar r_drawopaquerenderables;

extern ConVar r_flashlightdepth_drawtranslucents;

// FIXME: This is not static because we needed to turn it off for TF2 playtests
extern ConVar r_DrawDetailProps;

extern ConVar r_worldlistcache;

//-----------------------------------------------------------------------------
// Convars related to fog color
//-----------------------------------------------------------------------------
extern void GetFogColor( fogparams_t *pFogParams, float *pColor, bool ignoreOverride = false, bool ignoreHDRColorScale = false );
extern float GetFogMaxDensity( fogparams_t *pFogParams, bool ignoreOverride = false );
extern bool GetFogEnable( fogparams_t *pFogParams, bool ignoreOverride = false );
extern float GetFogStart( fogparams_t *pFogParams, bool ignoreOverride = false );
extern float GetFogEnd( fogparams_t *pFogParams, bool ignoreOverride = false );
extern float GetSkyboxFogStart( bool ignoreOverride = false );
extern float GetSkyboxFogEnd( bool ignoreOverride = false );
extern float GetSkyboxFogMaxDensity( bool ignoreOverride = false );
extern void GetSkyboxFogColor( float *pColor, bool ignoreOverride = false, bool ignoreHDRColorScale = false );
// set any of these to use the maps fog
extern ConVar fog_enableskybox;

extern void PositionHudPanels( CUtlVector< vgui::VPANEL > &list, const CViewSetup &view );

//-----------------------------------------------------------------------------
// Water-related convars
//-----------------------------------------------------------------------------
extern ConVar r_debugcheapwater;
#ifndef _X360
extern ConVar r_waterforceexpensive;
#endif
extern ConVar r_waterforcereflectentities;
extern ConVar r_WaterDrawRefraction;
extern ConVar r_WaterDrawReflection;
extern ConVar r_ForceWaterLeaf;
extern ConVar mat_drawwater;
extern ConVar mat_clipz;


//-----------------------------------------------------------------------------
// Other convars
//-----------------------------------------------------------------------------
extern ConVar r_eyewaterepsilon;

//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
static Vector g_vecCurrentRenderOrigin(0,0,0);
static QAngle g_vecCurrentRenderAngles(0,0,0);
static Vector g_vecCurrentVForward(0,0,0), g_vecCurrentVRight(0,0,0), g_vecCurrentVUp(0,0,0);
static VMatrix g_matCurrentCamInverse;
extern bool s_bCanAccessCurrentView;
static bool	g_bRenderingView = false;			// For debugging...
extern int g_CurrentViewID;
extern bool g_bRenderingScreenshot;

//static FrustumCache_t s_FrustumCache;
extern FrustumCache_t *FrustumCache( void );


//-----------------------------------------------------------------------------
// Describes a pruned set of leaves to be rendered this view. Reference counted
// because potentially shared by a number of views
//-----------------------------------------------------------------------------
struct ClientWorldListInfo_t : public CRefCounted1<WorldListInfo_t>
{
	ClientWorldListInfo_t() 
	{ 
		memset( (WorldListInfo_t *)this, 0, sizeof(WorldListInfo_t) ); 
		m_pOriginalLeafIndex = NULL;
		m_bPooledAlloc = false;
	}

	// Allocate a list intended for pruning
	static ClientWorldListInfo_t *AllocPooled( const ClientWorldListInfo_t &exemplar );

	// Because we remap leaves to eliminate unused leaves, we need a remap
	// when drawing translucent surfaces, which requires the *original* leaf index
	// using m_pOriginalLeafIndex[ remapped leaf index ] == actual leaf index
	uint16 *m_pOriginalLeafIndex;

private:
	virtual bool OnFinalRelease();

	bool m_bPooledAlloc;
	static CObjectPool<ClientWorldListInfo_t> gm_Pool;
};


//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------

class CWorldListCache
{
public:
	CWorldListCache()
	{

	}
	void Flush()
	{
		for ( int i = m_Entries.FirstInorder(); i != m_Entries.InvalidIndex(); i = m_Entries.NextInorder( i ) )
		{
			delete m_Entries[i];
		}
		m_Entries.RemoveAll();
	}

	bool Find( const CViewSetup &viewSetup, IWorldRenderList **ppList, ClientWorldListInfo_t **ppListInfo )
	{
		Entry_t lookup( viewSetup );

		int i = m_Entries.Find( &lookup );

		if ( i != m_Entries.InvalidIndex() )
		{
			Entry_t *pEntry = m_Entries[i];
			*ppList = InlineAddRef( pEntry->pList );
			*ppListInfo = InlineAddRef( pEntry->pListInfo );
			return true;
		}

		return false;
	}

	void Add( const CViewSetup &viewSetup, IWorldRenderList *pList, ClientWorldListInfo_t *pListInfo )
	{
		m_Entries.Insert( new Entry_t( viewSetup, pList, pListInfo ) );
	}

private:
	struct Entry_t
	{
		Entry_t( const CViewSetup &viewSetup, IWorldRenderList *pList = NULL, ClientWorldListInfo_t *pListInfo = NULL ) :
			pList( ( pList ) ? InlineAddRef( pList ) : NULL ),
			pListInfo( ( pListInfo ) ? InlineAddRef( pListInfo ) : NULL )
		{
            // @NOTE (toml 8/18/2006): because doing memcmp, need to fill all of the fields and the padding!
			memset( &m_bOrtho, 0, offsetof(Entry_t, pList ) - offsetof(Entry_t, m_bOrtho ) );
			m_bOrtho = viewSetup.m_bOrtho;			
			m_OrthoLeft = viewSetup.m_OrthoLeft;		
			m_OrthoTop = viewSetup.m_OrthoTop;
			m_OrthoRight = viewSetup.m_OrthoRight;
			m_OrthoBottom = viewSetup.m_OrthoBottom;
			fov = viewSetup.fov;				
			origin = viewSetup.origin;					
			angles = viewSetup.angles;				
			zNear = viewSetup.zNear;			
			zFar = viewSetup.zFar;			
			m_flAspectRatio = viewSetup.m_flAspectRatio;
			m_bOffCenter = viewSetup.m_bOffCenter;
			m_flOffCenterTop = viewSetup.m_flOffCenterTop;
			m_flOffCenterBottom = viewSetup.m_flOffCenterBottom;
			m_flOffCenterLeft = viewSetup.m_flOffCenterLeft;
			m_flOffCenterRight = viewSetup.m_flOffCenterRight;
		}

		~Entry_t()
		{
			if ( pList )
				pList->Release();
			if ( pListInfo )
				pListInfo->Release();
		}

		// The fields from CViewSetup that would actually affect the list
		float	m_OrthoLeft;		
		float	m_OrthoTop;
		float	m_OrthoRight;
		float	m_OrthoBottom;
		float	fov;				
		Vector	origin;					
		QAngle	angles;				
		float	zNear;			
		float	zFar;			
		float	m_flAspectRatio;
		float	m_flOffCenterTop;
		float	m_flOffCenterBottom;
		float	m_flOffCenterLeft;
		float	m_flOffCenterRight;
		bool	m_bOrtho;			
		bool	m_bOffCenter;

		IWorldRenderList *pList;
		ClientWorldListInfo_t *pListInfo;
	};

	class CEntryComparator
	{
	public:
		CEntryComparator( int ) {}
		bool operator!() const { return false; }
		bool operator()( const Entry_t *lhs, const Entry_t *rhs ) const 
		{ 
			return ( memcmp( lhs, rhs, sizeof(Entry_t) - ( sizeof(Entry_t) - offsetof(Entry_t, pList ) ) ) < 0 );
		}
	};

	CUtlRBTree<Entry_t *, unsigned short, CEntryComparator> m_Entries;
};

extern CWorldListCache g_WorldListCache;


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
class CBaseWorldViewDeferred : public CRendering3dView
{
	DECLARE_CLASS( CBaseWorldViewDeferred, CRendering3dView );
protected:
	CBaseWorldViewDeferred(CViewRender *pMainView) : CRendering3dView( pMainView ) {}

	virtual bool	AdjustView( float waterHeight );

	void			DrawSetup( float waterHeight, int flags, float waterZAdjust, int iForceViewLeaf = -1, bool bShadowDepth = false );
	void			DrawExecute( float waterHeight, view_id_t viewID, float waterZAdjust, bool bShadowDepth = false );

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	// BUGBUG this causes all sorts of problems
	virtual bool	ShouldCacheLists(){ return false; };

protected:

	void PushComposite();
	void PopComposite();
};


//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
class CSimpleWorldViewDeferred : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CSimpleWorldViewDeferred, CBaseWorldViewDeferred );
public:
	CSimpleWorldViewDeferred(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

	void			Setup( const CViewSetup &view, int nClearFlags, bool bDrawSkybox, const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t& info, ViewCustomVisibility_t *pCustomVisibility = NULL );
	void			Draw();

private: 
	VisibleFogVolumeInfo_t m_fogInfo;

};

class CGBufferView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CGBufferView, CBaseWorldViewDeferred );
public:
	CGBufferView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView )
	{
	}

	void			Setup( const CViewSetup &view, bool bDrewSkybox );
	void			Draw();

	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	void PushGBuffer( bool bInitial, float zScale = 1.0f );
	void PopGBuffer();

private: 
	VisibleFogVolumeInfo_t m_fogInfo;
	bool m_bDrewSkybox;
};

class CSkyboxViewDeferred : public CGBufferView
{
	DECLARE_CLASS( CSkyboxViewDeferred, CRendering3dView );
public:
	CSkyboxViewDeferred(CViewRender *pMainView) : 
		CGBufferView( pMainView ),
		m_pSky3dParams( NULL )
	  {
	  }

	virtual bool	ShouldCacheLists(){ return false; };

	bool			Setup( const CViewSetup &view, bool bGBuffer, SkyboxVisibility_t *pSkyboxVisible );
	void			Draw();

protected:

	virtual SkyboxVisibility_t	ComputeSkyboxVisibility();
	bool			GetSkyboxFogEnable();

	void			Enable3dSkyboxFog( void );
	void			DrawInternal( view_id_t iSkyBoxViewID = VIEW_3DSKY, bool bInvokePreAndPostRender = true, ITexture *pRenderTarget = NULL );

	sky3dparams_t *	PreRender3dSkyboxWorld( SkyboxVisibility_t nSkyboxVisible );
	sky3dparams_t *m_pSky3dParams;

	bool		m_bGBufferPass;
};

abstract_class CBaseShadowView : public CBaseWorldViewDeferred
{
	DECLARE_CLASS( CBaseShadowView, CBaseWorldViewDeferred );
public:
	CBaseShadowView(CViewRender *pMainView) : CBaseWorldViewDeferred( pMainView ) {}

	void			Setup( const CViewSetup &view,
						ITexture *pDepthTexture,
						ITexture *pDummyTexture );
	void			Draw();

	virtual bool	AdjustView( float waterHeight );
	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	virtual void	CalcShadowView() = 0;
	virtual void	CommitData(){};

	virtual int		GetShadowMode() = 0;

private:

	ITexture *m_pDepthTexture;
	ITexture *m_pDummyTexture;
	ViewCustomVisibility_t shadowVis;
};

class COrthoShadowView : public CBaseShadowView
{
	DECLARE_CLASS( COrthoShadowView, CBaseShadowView );
public:
	COrthoShadowView(CViewRender *pMainView, const int &index)
		: CBaseShadowView( pMainView )
	{
			iCascadeIndex = index;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_ORTHO;
	};

private:
	int iCascadeIndex;
};

class CDualParaboloidShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CDualParaboloidShadowView, CBaseShadowView );
public:
	CDualParaboloidShadowView(CViewRender *pMainView,
		def_light_t *pLight,
		const bool &bSecondary)
		: CBaseShadowView( pMainView )
	{
			m_pLight = pLight;
			m_bSecondary = bSecondary;
	}
	virtual bool	AdjustView( float waterHeight );
	virtual void	PushView( float waterHeight );
	virtual void	PopView();

	virtual void	CalcShadowView();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_DPSM;
	};

private:
	bool m_bSecondary;
	def_light_t *m_pLight;
};

class CSpotLightShadowView : public CBaseShadowView
{
	DECLARE_CLASS( CSpotLightShadowView, CBaseShadowView );
public:
	CSpotLightShadowView(CViewRender *pMainView,
		def_light_t *pLight, int index )
		: CBaseShadowView( pMainView )
	{
			m_pLight = pLight;
			m_iIndex = index;
	}

	virtual void	CalcShadowView();
	virtual void	CommitData();

	virtual int		GetShadowMode(){
		return DEFERRED_SHADOW_MODE_PROJECTED;
	};

private:
	def_light_t *m_pLight;
	int m_iIndex;
};

//-----------------------------------------------------------------------------
// Computes draw flags for the engine to build its world surface lists
//-----------------------------------------------------------------------------
static inline unsigned long BuildEngineDrawWorldListFlags( unsigned nDrawFlags )
{
	unsigned long nEngineFlags = 0;

	if ( ( nDrawFlags & DF_SKIP_WORLD ) == 0 )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_WORLD_GEOMETRY;
	}

	if ( ( nDrawFlags & DF_SKIP_WORLD_DECALS_AND_OVERLAYS ) == 0 )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_DECALS_AND_OVERLAYS;
	}

	if ( nDrawFlags & DF_DRAWSKYBOX )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_SKYBOX;
	}

	if ( nDrawFlags & DF_RENDER_ABOVEWATER )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_STRICTLYABOVEWATER;
		nEngineFlags |= DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	}

	if ( nDrawFlags & DF_RENDER_UNDERWATER )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_STRICTLYUNDERWATER;
		nEngineFlags |= DRAWWORLDLISTS_DRAW_INTERSECTSWATER;
	}

	if ( nDrawFlags & DF_RENDER_WATER )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_WATERSURFACE;
	}

	if( nDrawFlags & DF_CLIP_SKYBOX )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_CLIPSKYBOX;
	}

	if( nDrawFlags & DF_SHADOW_DEPTH_MAP )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_SHADOWDEPTH;
		nEngineFlags &= ~DRAWWORLDLISTS_DRAW_DECALS_AND_OVERLAYS;
	}

	if( nDrawFlags & DF_RENDER_REFRACTION )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_REFRACTION;
	}

	if( nDrawFlags & DF_RENDER_REFLECTION )
	{
		nEngineFlags |= DRAWWORLDLISTS_DRAW_REFLECTION;
	}

	return nEngineFlags;
}

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
static void SetClearColorToFogColor()
{
	unsigned char ucFogColor[3];
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->GetFogColor( ucFogColor );
	if( g_pMaterialSystemHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
	{
		// @MULTICORE (toml 8/16/2006): Find a way to not do this twice in eye above water case
		float scale = LinearToGammaFullRange( pRenderContext->GetToneMappingScaleLinear().x );
		ucFogColor[0] *= scale;
		ucFogColor[1] *= scale;
		ucFogColor[2] *= scale;
	}
	pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );
}

//-----------------------------------------------------------------------------
// Precache of necessary materials
//-----------------------------------------------------------------------------

PRECACHE_REGISTER_BEGIN( GLOBAL, PrecacheDeferredPostProcessingEffects )
	//PRECACHE( MATERIAL, "dev/blurfiltery_and_add_nohdr" )
PRECACHE_REGISTER_END( )


//-----------------------------------------------------------------------------
// Methods to set the current view/guard access to view parameters
//-----------------------------------------------------------------------------
extern void AllowCurrentViewAccess( bool allow );
extern bool IsCurrentViewAccessAllowed();

extern void SetupCurrentView( const Vector &vecOrigin, const QAngle &angles, view_id_t viewID, bool bDrawWorldNormal = false, bool bCullFrontFaces = false );

extern view_id_t CurrentViewID();

//-----------------------------------------------------------------------------
// Purpose: Portal views are considered 'Main' views. This function tests a view id 
//			against all view ids used by portal renderables, as well as the main view.
//-----------------------------------------------------------------------------
extern bool IsMainView ( view_id_t id );

extern void FinishCurrentView();


//#if !defined( INFESTED_DLL )
//static CViewRender g_ViewRender;
//IViewRender *GetViewRenderInstance()
//{
//	return &g_ViewRender;
//}
//#endif


//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CDeferredViewRender::CDeferredViewRender()
{
}

void CDeferredViewRender::Init()
{
	BaseClass::Init();
}

void CDeferredViewRender::Shutdown()
{
	BaseClass::Shutdown();
}

void CDeferredViewRender::LevelInit()
{
	BaseClass::LevelInit();

	ResetCascadeDelay();
}

void CDeferredViewRender::LevelShutdown()
{
	BaseClass::LevelShutdown();
}

void CDeferredViewRender::ResetCascadeDelay()
{
	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
		m_flRenderDelay[i] = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Renders world and all entities, etc.
//-----------------------------------------------------------------------------
void CDeferredViewRender::ViewDrawSceneDeferred( const CViewSetup &view, int nClearFlags, view_id_t viewID, bool bDrawViewModel )
{
	VPROF( "CViewRender::ViewDrawScene" );

	bool bDrew3dSkybox = false;
	SkyboxVisibility_t nSkyboxVisible = SKYBOX_NOT_VISIBLE;

	ViewDrawGBuffer( view, bDrew3dSkybox, nSkyboxVisible );

	PerformLighting( view );

	ViewDrawComposite( view, bDrew3dSkybox, nSkyboxVisible, nClearFlags, viewID );

	g_ShaderEditorSystem->UpdateSkymask( bDrew3dSkybox );

	GetLightingManager()->RenderVolumetrics( view );

	// Disable fog for the rest of the stuff
	DisableFog();

	// UNDONE: Don't do this with masked brush models, they should probably be in a separate list
	// render->DrawMaskEntities()

	// Here are the overlays...
	CGlowOverlay::DrawOverlays( view.m_bCacheFullSceneState );

	// issue the pixel visibility tests
	PixelVisibility_EndCurrentView();

	// Draw rain..
	DrawPrecipitation();

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	// Debugging info goes over the top
	CDebugViewRender::Draw3DDebuggingInfo( view );

	// Draw client side effects
	// NOTE: These are not sorted against the rest of the frame
	clienteffects->DrawEffects( gpGlobals->frametime );	

	// Mark the frame as locked down for client fx additions
	SetFXCreationAllowed( false );

	// Invoke post-render methods
	IGameSystem::PostRenderAllSystems();

	FinishCurrentView();

	// Set int rendering parameters back to defaults
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_ENABLE_FIXED_LIGHTING, 0 );

	if ( view.m_bCullFrontFaces )
	{
		pRenderContext->FlipCulling( false );
	}
}

void CDeferredViewRender::ViewDrawGBuffer( const CViewSetup &view, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible )
{
	MDLCACHE_CRITICAL_SECTION();

	int oldViewID = g_CurrentViewID;
	g_CurrentViewID = VIEW_DEFERRED_GBUFFER;

	CSkyboxViewDeferred *pSkyView = new CSkyboxViewDeferred( this );
	if ( ( bDrew3dSkybox = pSkyView->Setup( view, true, &nSkyboxVisible ) ) != false )
		AddViewToScene( pSkyView );

	SafeRelease( pSkyView );

	// Start view
	unsigned int visFlags;
	SetupVis( view, visFlags, NULL );

	CRefPtr<CGBufferView> pGBufferView = new CGBufferView( this );
	pGBufferView->Setup( view, bDrew3dSkybox );
	AddViewToScene( pGBufferView );

	g_CurrentViewID = oldViewID;
}

void CDeferredViewRender::ViewDrawComposite( const CViewSetup &view, bool &bDrew3dSkybox, SkyboxVisibility_t &nSkyboxVisible,
		int nClearFlags, view_id_t viewID )
{
	DrawSkyboxComposite( view, bDrew3dSkybox );

	// this allows the refract texture to be updated once per *scene* on 360
	// (e.g. once for a monitor scene and once for the main scene)
	g_viewscene_refractUpdateFrame = gpGlobals->framecount - 1;

	m_BaseDrawFlags = 0;

	SetupCurrentView( view.origin, view.angles, viewID, view.m_bDrawWorldNormal, view.m_bCullFrontFaces );

	// Invoke pre-render methods
	IGameSystem::PreRenderAllSystems();

	// Start view
	unsigned int visFlags;
	SetupVis( view, visFlags, NULL );

	if ( !bDrew3dSkybox && 
		( nSkyboxVisible == SKYBOX_NOT_VISIBLE ) ) //&& ( visFlags & IVRenderView::VIEW_SETUP_VIS_EX_RETURN_FLAGS_USES_RADIAL_VIS ) )
	{
		// This covers the case where we don't see a 3dskybox, yet radial vis is clipping
		// the far plane.  Need to clear to fog color in this case.
		nClearFlags |= VIEW_CLEAR_COLOR;
		SetClearColorToFogColor( );
	}
	else
		nClearFlags |= VIEW_CLEAR_DEPTH;

	bool drawSkybox = r_skybox.GetBool();
	if ( bDrew3dSkybox || ( nSkyboxVisible == SKYBOX_NOT_VISIBLE ) )
	{
		drawSkybox = false;
	}

	ParticleMgr()->IncrementFrameCode();

	DrawWorldComposite( view, nClearFlags, drawSkybox );
}

void CDeferredViewRender::DrawSkyboxComposite( const CViewSetup &view, const bool &bDrew3dSkybox )
{
	if ( !bDrew3dSkybox )
		return;

	CSkyboxViewDeferred *pSkyView = new CSkyboxViewDeferred( this );
	SkyboxVisibility_t nSkyboxVisible = SKYBOX_NOT_VISIBLE;
	if ( pSkyView->Setup( view, false, &nSkyboxVisible ) )
	{
		AddViewToScene( pSkyView );
		g_ShaderEditorSystem->UpdateSkymask();
	}

	SafeRelease( pSkyView );
	Assert( nSkyboxVisible == SKYBOX_3DSKYBOX_VISIBLE );
}

void CDeferredViewRender::DrawWorldComposite( const CViewSetup &view, int nClearFlags, bool bDrawSkybox )
{
	MDLCACHE_CRITICAL_SECTION();

	VisibleFogVolumeInfo_t fogVolumeInfo;

	render->GetVisibleFogVolume( view.origin, &fogVolumeInfo );

	WaterRenderInfo_t info;
	DetermineWaterRenderInfo( fogVolumeInfo, info );

	CRefPtr<CSimpleWorldViewDeferred> pNoWaterView = new CSimpleWorldViewDeferred( this );
	pNoWaterView->Setup( view, nClearFlags, bDrawSkybox, fogVolumeInfo, info );
	AddViewToScene( pNoWaterView );

	// Blat out the visible fog leaf if we're not going to use it
	//if ( !r_ForceWaterLeaf.GetBool() )
	//{
	//	fogVolumeInfo.m_nVisibleFogVolumeLeaf = -1;
	//}

	//// We can see water of some sort
	//if ( !fogVolumeInfo.m_bEyeInFogVolume )
	//{
	//	CRefPtr<CAboveWaterView> pAboveWaterView = new CAboveWaterView( this );
	//	pAboveWaterView->Setup( viewIn, bDrawSkybox, fogVolumeInfo, info );
	//	AddViewToScene( pAboveWaterView );
	//}
	//else
	//{
	//	CRefPtr<CUnderWaterView> pUnderWaterView = new CUnderWaterView( this );
	//	pUnderWaterView->Setup( viewIn, bDrawSkybox, fogVolumeInfo, info );
	//	AddViewToScene( pUnderWaterView );
	//}
}

static lightData_Global_t GetActiveGlobalLightState()
{
	lightData_Global_t data;
	CLightingEditor *pEditor = GetLightingEditor();

	if ( pEditor->IsEditorLightingActive() && pEditor->GetKVGlobalLight() != NULL )
	{
		data = pEditor->GetGlobalState();
	}
	else if ( GetGlobalLight() != NULL )
	{
		data = GetGlobalLight()->GetState();
	}

	return data;
}

void CDeferredViewRender::PerformLighting( const CViewSetup &view )
{
	bool bResetLightAccum = false;

	if ( GetGlobalLight() != NULL )
	{
		struct defData_setGlobalLightState
		{
		public:
			lightData_Global_t state;

			static void Fire( defData_setGlobalLightState d )
			{
				GetDeferredExt()->CommitLightData_Global( d.state );
			};
		};

		defData_setGlobalLightState lightDataState;
		lightDataState.state = GetActiveGlobalLightState();

		if ( !GetLightingEditor()->IsEditorLightingActive() &&
			deferred_override_globalLight_enable.GetBool() )
		{
			lightDataState.state.bShadow = deferred_override_globalLight_shadow_enable.GetBool();
			UTIL_StringToVector( lightDataState.state.diff.AsVector3D().Base(), deferred_override_globalLight_diffuse.GetString() );
			UTIL_StringToVector( lightDataState.state.ambh.AsVector3D().Base(), deferred_override_globalLight_ambient_high.GetString() );
			UTIL_StringToVector( lightDataState.state.ambl.AsVector3D().Base(), deferred_override_globalLight_ambient_low.GetString() );

			lightDataState.state.bEnabled = ( lightDataState.state.diff.LengthSqr() > 0.01f ||
				lightDataState.state.ambh.LengthSqr() > 0.01f ||
				lightDataState.state.ambl.LengthSqr() > 0.01f );
		}

		QUEUE_FIRE( defData_setGlobalLightState, Fire, lightDataState );

		if ( lightDataState.state.bEnabled )
		{
			bool bShadowedGlobal = lightDataState.state.bShadow;

			if ( bShadowedGlobal )
			{
				Vector origins[2] = { view.origin, view.origin + lightDataState.state.vecLight.AsVector3D() * 1024 };
				render->ViewSetupVis( false, 2, origins );

				RenderCascadedShadows( view );
			}
		}
		else
			bResetLightAccum = true;
	}
	else
		bResetLightAccum = true;

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( GetDefRT_Lightaccum() );

	if ( bResetLightAccum )
	{
		pRenderContext->ClearColor4ub( 0, 0, 0, 0 );
		pRenderContext->ClearBuffers( true, false );
	}
	else
		DrawLightPassFullscreen( GetDeferredManager()->GetDeferredMaterial( DEF_MAT_LIGHT_GLOBAL ), view.width, view.height );

	pRenderContext.SafeRelease();

	GetLightingManager()->RenderLights( view, this );

	pRenderContext.GetFrom( materials );
	pRenderContext->PopRenderTargetAndViewport();
}

void CDeferredViewRender::RenderCascadedShadows( const CViewSetup &view )
{
	for ( int i = 0; i < SHADOW_NUM_CASCADES; i++ )
	{
		float delta = m_flRenderDelay[i] - gpGlobals->curtime;
		if ( delta > 0.0f && delta < 1.0f )
			continue;

		m_flRenderDelay[i] = gpGlobals->curtime + GetCascadeInfo(i).flUpdateDelay;

#if CSM_USE_COMPOSITED_TARGET == 0
		int textureIndex = i;
#else
		int textureIndex = 0;
#endif

		CRefPtr<COrthoShadowView> pOrthoDepth = new COrthoShadowView( this, i );
		pOrthoDepth->Setup( view, GetShadowDepthRT_Ortho( textureIndex ), GetShadowColorRT_Ortho( textureIndex ) );
		AddViewToScene( pOrthoDepth );
	}
}

void CDeferredViewRender::DrawLightShadowView( const CViewSetup &view, int iDesiredShadowmap, def_light_t *l )
{
	CViewSetup setup;
	setup.origin = l->pos;
	setup.angles = l->ang;
	setup.m_bOrtho = false;
	setup.m_flAspectRatio = 1;
	setup.x = setup.y = 0;

	Vector origins[2] = { view.origin, l->pos };
	render->ViewSetupVis( false, 2, origins );

	switch ( l->iLighttype )
	{
	default:
		Assert( 0 );
		break;
	case DEFLIGHTTYPE_POINT:
		{
			CRefPtr<CDualParaboloidShadowView> pDPView0 = new CDualParaboloidShadowView( this, l, false );
			pDPView0->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_DP( iDesiredShadowmap ) );
			AddViewToScene( pDPView0 );

			CRefPtr<CDualParaboloidShadowView> pDPView1 = new CDualParaboloidShadowView( this, l, true );
			pDPView1->Setup( setup, GetShadowDepthRT_DP( iDesiredShadowmap ), GetShadowColorRT_DP( iDesiredShadowmap ) );
			AddViewToScene( pDPView1 );
		}
		break;
	case DEFLIGHTTYPE_SPOT:
		{
			CRefPtr<CSpotLightShadowView> pProjView = new CSpotLightShadowView( this, l, iDesiredShadowmap );
			pProjView->Setup( setup, GetShadowDepthRT_Proj( iDesiredShadowmap ), GetShadowColorRT_Proj( iDesiredShadowmap ) );
			AddViewToScene( pProjView );
		}
		break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: This renders the entire 3D view and the in-game hud/viewmodel
// Input  : &view - 
//			whatToDraw - 
//-----------------------------------------------------------------------------
// This renders the entire 3D view.
void CDeferredViewRender::RenderView( const CViewSetup &view, const CViewSetup &hudViewSetup, int nClearFlags, int whatToDraw )
{
	m_UnderWaterOverlayMaterial.Shutdown();					// underwater view will set

	ASSERT_LOCAL_PLAYER_RESOLVABLE();
	int slot = GET_ACTIVE_SPLITSCREEN_SLOT();

	CViewSetup worldView = view;

	CLightingEditor *pLightEditor = GetLightingEditor();

	if ( pLightEditor->IsEditorActive() )
		pLightEditor->GetEditorView( &worldView.origin, &worldView.angles );
	else
		pLightEditor->SetEditorView( &worldView.origin, &worldView.angles );

	m_CurrentView = worldView;

	C_BaseAnimating::AutoAllowBoneAccess boneaccess( true, true );
	VPROF( "CViewRender::RenderView" );

	{
		// HACK: server-side weapons use the viewmodel model, and client-side weapons swap that out for
		// the world model in DrawModel.  This is too late for some bone setup work that happens before
		// DrawModel, so here we just iterate all weapons we know of and fix them up ahead of time.
		MDLCACHE_CRITICAL_SECTION();
		CUtlLinkedList< CBaseCombatWeapon * > &weaponList = C_BaseCombatWeapon::GetWeaponList();
		FOR_EACH_LL( weaponList, it )
		{
			C_BaseCombatWeapon *weapon = weaponList[it];
			if ( !weapon->IsDormant() )
			{
				weapon->EnsureCorrectRenderingModel();
			}
		}
	}

	CMatRenderContextPtr pRenderContext( materials );
	ITexture *saveRenderTarget = pRenderContext->GetRenderTarget();
	pRenderContext.SafeRelease(); // don't want to hold for long periods in case in a locking active share thread mode

	{
		g_bRenderingView = true;

		RenderPreScene( worldView );

		// Must be first 
		render->SceneBegin();

		g_pColorCorrectionMgr->UpdateColorCorrection();

		// Send the current tonemap scalar to the material system
		UpdateMaterialSystemTonemapScalar();

		// clear happens here probably
		SetupMain3DView( slot, worldView, hudViewSetup, nClearFlags, saveRenderTarget );

		g_pClientShadowMgr->UpdateSplitscreenLocalPlayerShadowSkip();

		ProcessDeferredGlobals( worldView );
		GetLightingManager()->LightSetup( worldView );

		PreViewDrawScene( worldView );

		// Force it to clear the framebuffer if they're in solid space.
		if ( ( nClearFlags & VIEW_CLEAR_COLOR ) == 0 )
		{
			MDLCACHE_CRITICAL_SECTION();
			if ( enginetrace->GetPointContents( worldView.origin ) == CONTENTS_SOLID )
			{
				nClearFlags |= VIEW_CLEAR_COLOR;
			}
		}

		ViewDrawSceneDeferred( worldView, nClearFlags, VIEW_MAIN, whatToDraw & RENDERVIEW_DRAWVIEWMODEL );

		// We can still use the 'current view' stuff set up in ViewDrawScene
		AllowCurrentViewAccess( true );

		// must happen before teardown
		pLightEditor->OnRender();

		GetLightingManager()->LightTearDown();

		PostViewDrawScene( worldView );

		engine->DrawPortals();

		DisableFog();

		// Finish scene
		render->SceneEnd();

		// Draw lightsources if enabled
		//render->DrawLights();

		//RenderPlayerSprites();

		// Image-space motion blur and depth of field
		#if defined( _X360 )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushVertexShaderGPRAllocation( 16 ); //Max out pixel shader threads
			pRenderContext.SafeRelease();
		}
		#endif

		if ( !building_cubemaps.GetBool() )
		{
			if ( IsDepthOfFieldEnabled() )
			{
				pRenderContext.GetFrom( materials );
				{
					PIXEVENT( pRenderContext, "DoDepthOfField()" );
					DoDepthOfField( worldView );
				}
				pRenderContext.SafeRelease();
			}

			if ( ( worldView.m_nMotionBlurMode != MOTION_BLUR_DISABLE ) && ( mat_motion_blur_enabled.GetInt() ) )
			{
				pRenderContext.GetFrom( materials );
				{
					PIXEVENT( pRenderContext, "DoImageSpaceMotionBlur()" );
					DoImageSpaceMotionBlur( worldView );
				}
				pRenderContext.SafeRelease();
			}
		}

		#if defined( _X360 )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PopVertexShaderGPRAllocation();
			pRenderContext.SafeRelease();
		}
		#endif

		// Now actually draw the viewmodel
		//DrawViewModels( view, whatToDraw & RENDERVIEW_DRAWVIEWMODEL );

		DrawUnderwaterOverlay();

		PixelVisibility_EndScene();

		#if defined( _X360 )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PushVertexShaderGPRAllocation( 16 ); //Max out pixel shader threads
			pRenderContext.SafeRelease();
		}
		#endif

		// Draw fade over entire screen if needed
		byte color[4];
		bool blend;
		GetViewEffects()->GetFadeParams( &color[0], &color[1], &color[2], &color[3], &blend );

		// Store off color fade params to be applied in fullscreen postprocess pass
		SetViewFadeParams( color[0], color[1], color[2], color[3], blend );

		// Draw an overlay to make it even harder to see inside smoke particle systems.
		DrawSmokeFogOverlay();

		// Overlay screen fade on entire screen
		PerformScreenOverlay( worldView.x, worldView.y, worldView.width, worldView.height );

		// Prevent sound stutter if going slow
		engine->Sound_ExtraUpdate();	

		if ( g_pMaterialSystemHardwareConfig->GetHDRType() != HDR_TYPE_NONE )
		{
			pRenderContext.GetFrom( materials );
			pRenderContext->SetToneMappingScaleLinear(Vector(1,1,1));
			pRenderContext.SafeRelease();
		}

		if ( !building_cubemaps.GetBool() && worldView.m_bDoBloomAndToneMapping )
		{
			pRenderContext.GetFrom( materials );
			{
				static bool bAlreadyShowedLoadTime = false;
				
				if ( ! bAlreadyShowedLoadTime )
				{
					bAlreadyShowedLoadTime = true;
					if ( CommandLine()->CheckParm( "-timeload" ) )
					{
						Warning( "time to initial render = %f\n", Plat_FloatTime() );
					}
				}

				PIXEVENT( pRenderContext, "DoEnginePostProcessing()" );

				bool bFlashlightIsOn = false;
				C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
				if ( pLocal )
				{
					bFlashlightIsOn = pLocal->IsEffectActive( EF_DIMLIGHT );
				}
				DoEnginePostProcessing( worldView.x, worldView.y, worldView.width, worldView.height, bFlashlightIsOn );
			}
			pRenderContext.SafeRelease();
		}

		g_ShaderEditorSystem->CustomPostRender();

		// And here are the screen-space effects

		if ( IsPC() )
		{
			// Grab the pre-color corrected frame for editing purposes
			engine->GrabPreColorCorrectedFrame( worldView.x, worldView.y, worldView.width, worldView.height );
		}

		PerformScreenSpaceEffects( worldView.x, worldView.y, worldView.width, worldView.height );


		#if defined( _X360 )
		{
			CMatRenderContextPtr pRenderContext( materials );
			pRenderContext->PopVertexShaderGPRAllocation();
			pRenderContext.SafeRelease();
		}
		#endif

		GetClientMode()->DoPostScreenSpaceEffects( &worldView );

		CleanupMain3DView( worldView );

		if ( m_FreezeParams[ slot ].m_bTakeFreezeFrame )
		{
			pRenderContext = materials->GetRenderContext();
			if ( IsX360() )
			{
				// 360 doesn't create the Fullscreen texture
				pRenderContext->CopyRenderTargetToTextureEx( GetFullFrameFrameBufferTexture( 1 ), 0, NULL, NULL );
			}
			else
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetFullscreenTexture(), 0, NULL, NULL );
			}
			pRenderContext.SafeRelease();
			m_FreezeParams[ slot ].m_bTakeFreezeFrame = false;
		}

		pRenderContext = materials->GetRenderContext();
		pRenderContext->SetRenderTarget( saveRenderTarget );
		pRenderContext.SafeRelease();

		// Draw the overlay
		if ( m_bDrawOverlay )
		{	   
			// This allows us to be ok if there are nested overlay views
			CViewSetup currentView = m_CurrentView;
			CViewSetup tempView = m_OverlayViewSetup;
			tempView.fov = ScaleFOVByWidthRatio( tempView.fov, tempView.m_flAspectRatio / ( 4.0f / 3.0f ) );
			tempView.m_bDoBloomAndToneMapping = false;				// FIXME: Hack to get Mark up and running
			tempView.m_nMotionBlurMode = MOTION_BLUR_DISABLE;		// FIXME: Hack to get Mark up and running
			m_bDrawOverlay = false;
			RenderView( tempView, hudViewSetup, m_OverlayClearFlags, m_OverlayDrawFlags );
			m_CurrentView = currentView;
		}
	}

	// Clear a row of pixels at the edge of the viewport if it isn't at the edge of the screen
	if ( VGui_IsSplitScreen() )
	{
		CMatRenderContextPtr pRenderContext( materials );
		pRenderContext->PushRenderTargetAndViewport();

		int nScreenWidth, nScreenHeight;
		g_pMaterialSystem->GetBackBufferDimensions( nScreenWidth, nScreenHeight );

		// NOTE: view.height is off by 1 on the PC in a release build, but debug is correct! I'm leaving this here to help track this down later.
		// engine->Con_NPrintf( 25 + hh, "view( %d, %d, %d, %d ) GetBackBufferDimensions( %d, %d )\n", view.x, view.y, view.width, view.height, nScreenWidth, nScreenHeight );

		if ( worldView.x != 0 ) // if left of viewport isn't at 0
		{
			pRenderContext->Viewport( worldView.x, worldView.y, 1, worldView.height );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
		}

		if ( ( worldView.x + worldView.width ) != nScreenWidth ) // if right of viewport isn't at edge of screen
		{
			pRenderContext->Viewport( worldView.x + worldView.width - 1, worldView.y, 1, worldView.height );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
		}

		if ( worldView.y != 0 ) // if top of viewport isn't at 0
		{
			pRenderContext->Viewport( worldView.x, worldView.y, worldView.width, 1 );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
		}

		if ( ( worldView.y + worldView.height ) != nScreenHeight ) // if bottom of viewport isn't at edge of screen
		{
			pRenderContext->Viewport( worldView.x, worldView.y + worldView.height - 1, worldView.width, 1 );
			pRenderContext->ClearColor3ub( 0, 0, 0 );
			pRenderContext->ClearBuffers( true, false );
		}

		pRenderContext->PopRenderTargetAndViewport();
		pRenderContext->Release();
	}

	// Draw the 2D graphics
	m_CurrentView = hudViewSetup;
	pRenderContext = materials->GetRenderContext();
	if ( true )
	{
		PIXEVENT( pRenderContext, "2D Client Rendering" );

		render->Push2DView( hudViewSetup, 0, saveRenderTarget, GetFrustum() );

		Render2DEffectsPreHUD( hudViewSetup );

		if ( whatToDraw & RENDERVIEW_DRAWHUD )
		{
			VPROF_BUDGET( "VGui_DrawHud", VPROF_BUDGETGROUP_OTHER_VGUI );
			// paint the vgui screen
			VGui_PreRender();

			CUtlVector< vgui::VPANEL > vecHudPanels;

			vecHudPanels.AddToTail( VGui_GetClientDLLRootPanel() );

			// This block is suspect - why are we resizing fullscreen panels to be the size of the hudViewSetup
			// which is potentially only half the screen
			if ( GET_ACTIVE_SPLITSCREEN_SLOT() == 0 )
			{
				vecHudPanels.AddToTail( VGui_GetFullscreenRootVPANEL() );

#if defined( TOOLFRAMEWORK_VGUI_REFACTOR )
				vecHudPanels.AddToTail( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
#endif
				vecHudPanels.AddToTail( enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS ) );
			}

			PositionHudPanels( vecHudPanels, hudViewSetup );

			// The crosshair, etc. needs to get at the current setup stuff
			AllowCurrentViewAccess( true );

			// Draw the in-game stuff based on the actual viewport being used
			render->VGui_Paint( PAINT_INGAMEPANELS );

			AllowCurrentViewAccess( false );

			VGui_PostRender();

			GetClientMode()->PostRenderVGui();
			pRenderContext->Flush();
		}

		CDebugViewRender::Draw2DDebuggingInfo( hudViewSetup );

		Render2DEffectsPostHUD( hudViewSetup );

		g_bRenderingView = false;

		// We can no longer use the 'current view' stuff set up in ViewDrawScene
		AllowCurrentViewAccess( false );

		if ( IsPC() )
		{
			CDebugViewRender::GenerateOverdrawForTesting();
		}

		render->PopView( GetFrustum() );
	}
	pRenderContext.SafeRelease();

	g_WorldListCache.Flush();

	m_CurrentView = worldView;

#ifdef PARTICLE_USAGE_DEMO
	ParticleUsageDemo();
#endif
}

struct defData_setGlobals
{
public:
	Vector orig, fwd;
	float zDists[2];
	VMatrix frustumDeltas;

	static void Fire( defData_setGlobals d )
	{
		IDeferredExtension *pDef = GetDeferredExt();
		pDef->CommitOrigin( d.orig );
		pDef->CommitViewForward( d.fwd );
		pDef->CommitZDists( d.zDists[0], d.zDists[1] );
		pDef->CommitFrustumDeltas( d.frustumDeltas );
	};
};

void CDeferredViewRender::ProcessDeferredGlobals( const CViewSetup &view )
{
	VMatrix matPerspective, matView, matViewProj, screen2world;
	matView.Identity();
	matView.SetupMatrixOrgAngles( vec3_origin, view.angles );

	MatrixSourceToDeviceSpace( matView );

	matView = matView.Transpose3x3();
	Vector viewPosition;

	Vector3DMultiply( matView, view.origin, viewPosition );
	matView.SetTranslation( -viewPosition );
	MatrixBuildPerspectiveX( matPerspective, view.fov, view.m_flAspectRatio,
		view.zNear, view.zFar );
	MatrixMultiply( matPerspective, matView, matViewProj );

	MatrixInverseGeneral( matViewProj, screen2world );

	GetLightingManager()->SetRenderConstants( screen2world, view );

	Vector frustum_c0, frustum_cc, frustum_1c;
	float projDistance = 1.0f;
	Vector3DMultiplyPositionProjective( screen2world, Vector(0,projDistance,projDistance), frustum_c0 );
	Vector3DMultiplyPositionProjective( screen2world, Vector(0,0,projDistance), frustum_cc );
	Vector3DMultiplyPositionProjective( screen2world, Vector(projDistance,0,projDistance), frustum_1c );

	frustum_c0 -= view.origin;
	frustum_cc -= view.origin;
	frustum_1c -= view.origin;

	Vector frustum_up = frustum_c0 - frustum_cc;
	Vector frustum_right = frustum_1c - frustum_cc;

	frustum_cc /= view.zFar;
	frustum_right /= view.zFar;
	frustum_up /= view.zFar;

	defData_setGlobals data;
	data.orig = view.origin;
	AngleVectors( view.angles, &data.fwd );
	data.zDists[0] = view.zNear;
	data.zDists[1] = view.zFar;

	data.frustumDeltas.Identity();
	data.frustumDeltas.SetBasisVectors( frustum_cc, frustum_right, frustum_up );
	data.frustumDeltas = data.frustumDeltas.Transpose3x3();

	QUEUE_FIRE( defData_setGlobals, Fire, data );
}

//-----------------------------------------------------------------------------
// Returns true if the view plane intersects the water
//-----------------------------------------------------------------------------
extern bool DoesViewPlaneIntersectWater( float waterZ, int leafWaterDataID );



//-----------------------------------------------------------------------------
// Fakes per-entity clip planes on cards that don't support user clip planes.
//  Achieves the effect by drawing an invisible box that writes to the depth buffer
//  around the clipped area. It's not perfect, but better than nothing.
//-----------------------------------------------------------------------------
static void DrawClippedDepthBox( IClientRenderable *pEnt, float *pClipPlane )
{
//#define DEBUG_DRAWCLIPPEDDEPTHBOX //uncomment to draw the depth box as a colorful box

	static const int iQuads[6][5] = {	{ 0, 4, 6, 2, 0 }, //always an extra copy of first index at end to make some algorithms simpler
										{ 3, 7, 5, 1, 3 },
										{ 1, 5, 4, 0, 1 },
										{ 2, 6, 7, 3, 2 },
										{ 0, 2, 3, 1, 0 },
										{ 5, 7, 6, 4, 5 } };

	static const int iLines[12][2] = {	{ 0, 1 },
										{ 0, 2 },
										{ 0, 4 },
										{ 1, 3 },
										{ 1, 5 },
										{ 2, 3 },
										{ 2, 6 },
										{ 3, 7 },
										{ 4, 6 },
										{ 4, 5 },
										{ 5, 7 },
										{ 6, 7 } };


#ifdef DEBUG_DRAWCLIPPEDDEPTHBOX
	static const float fColors[6][3] = {	{ 1.0f, 0.0f, 0.0f },
											{ 0.0f, 1.0f, 1.0f },
											{ 0.0f, 1.0f, 0.0f },
											{ 1.0f, 0.0f, 1.0f },
											{ 0.0f, 0.0f, 1.0f },
											{ 1.0f, 1.0f, 0.0f } };
#endif

	
	

	Vector vNormal = *(Vector *)pClipPlane;
	float fPlaneDist = pClipPlane[3];

	Vector vMins, vMaxs;
	pEnt->GetRenderBounds( vMins, vMaxs );

	Vector vOrigin = pEnt->GetRenderOrigin();
	QAngle qAngles = pEnt->GetRenderAngles();
	
	Vector vForward, vUp, vRight;
	AngleVectors( qAngles, &vForward, &vRight, &vUp );

	Vector vPoints[8];
	vPoints[0] = vOrigin + (vForward * vMins.x) + (vRight * vMins.y) + (vUp * vMins.z);
	vPoints[1] = vOrigin + (vForward * vMaxs.x) + (vRight * vMins.y) + (vUp * vMins.z);
	vPoints[2] = vOrigin + (vForward * vMins.x) + (vRight * vMaxs.y) + (vUp * vMins.z);
	vPoints[3] = vOrigin + (vForward * vMaxs.x) + (vRight * vMaxs.y) + (vUp * vMins.z);
	vPoints[4] = vOrigin + (vForward * vMins.x) + (vRight * vMins.y) + (vUp * vMaxs.z);
	vPoints[5] = vOrigin + (vForward * vMaxs.x) + (vRight * vMins.y) + (vUp * vMaxs.z);
	vPoints[6] = vOrigin + (vForward * vMins.x) + (vRight * vMaxs.y) + (vUp * vMaxs.z);
	vPoints[7] = vOrigin + (vForward * vMaxs.x) + (vRight * vMaxs.y) + (vUp * vMaxs.z);

	int iClipped[8];
	float fDists[8];
	for( int i = 0; i != 8; ++i )
	{
		fDists[i] = vPoints[i].Dot( vNormal ) - fPlaneDist;
		iClipped[i] = (fDists[i] > 0.0f) ? 1 : 0;
	}

	Vector vSplitPoints[8][8]; //obviously there are only 12 lines, not 64 lines or 64 split points, but the indexing is way easier like this
	int iLineStates[8][8]; //0 = unclipped, 2 = wholly clipped, 3 = first point clipped, 4 = second point clipped

	//categorize lines and generate split points where needed
	for( int i = 0; i != 12; ++i )
	{
		const int *pPoints = iLines[i];
		int iLineState = (iClipped[pPoints[0]] + iClipped[pPoints[1]]);
		if( iLineState != 1 ) //either both points are clipped, or neither are clipped
		{
			iLineStates[pPoints[0]][pPoints[1]] = 
				iLineStates[pPoints[1]][pPoints[0]] = 
					iLineState;
		}
		else
		{
			//one point is clipped, the other is not
			if( iClipped[pPoints[0]] == 1 )
			{
				//first point was clipped, index 1 has the negative distance
				float fInvTotalDist = 1.0f / (fDists[pPoints[0]] - fDists[pPoints[1]]);
				vSplitPoints[pPoints[0]][pPoints[1]] = 
					vSplitPoints[pPoints[1]][pPoints[0]] =
						(vPoints[pPoints[1]] * (fDists[pPoints[0]] * fInvTotalDist)) - (vPoints[pPoints[0]] * (fDists[pPoints[1]] * fInvTotalDist));
				
				Assert( fabs( vNormal.Dot( vSplitPoints[pPoints[0]][pPoints[1]] ) - fPlaneDist ) < 0.01f );

				iLineStates[pPoints[0]][pPoints[1]] = 3;
				iLineStates[pPoints[1]][pPoints[0]] = 4;
			}
			else
			{
				//second point was clipped, index 0 has the negative distance
				float fInvTotalDist = 1.0f / (fDists[pPoints[1]] - fDists[pPoints[0]]);
				vSplitPoints[pPoints[0]][pPoints[1]] = 
					vSplitPoints[pPoints[1]][pPoints[0]] =
						(vPoints[pPoints[0]] * (fDists[pPoints[1]] * fInvTotalDist)) - (vPoints[pPoints[1]] * (fDists[pPoints[0]] * fInvTotalDist));

				Assert( fabs( vNormal.Dot( vSplitPoints[pPoints[0]][pPoints[1]] ) - fPlaneDist ) < 0.01f );

				iLineStates[pPoints[0]][pPoints[1]] = 4;
				iLineStates[pPoints[1]][pPoints[0]] = 3;
			}
		}
	}

	extern CMaterialReference g_material_WriteZ;
	CMatRenderContextPtr pRenderContext( materials );
	
#ifdef DEBUG_DRAWCLIPPEDDEPTHBOX
	pRenderContext->Bind( materials->FindMaterial( "debug/debugvertexcolor", TEXTURE_GROUP_OTHER ), NULL );
#else
	pRenderContext->Bind( g_material_WriteZ, NULL );
#endif

	CMeshBuilder meshBuilder;
	IMesh* pMesh = pRenderContext->GetDynamicMesh( false );
	meshBuilder.Begin( pMesh, MATERIAL_TRIANGLES, 18 ); //6 sides, possible one cut per side. Any side is capable of having 3 tri's. Lots of padding for things that aren't possible

	//going to draw as a collection of triangles, arranged as a triangle fan on each side
	for( int i = 0; i != 6; ++i )
	{
		const int *pPoints = iQuads[i];

		//can't start the fan on a wholly clipped line, so seek to one that isn't
		int j = 0;
		do
		{
			if( iLineStates[pPoints[j]][pPoints[j+1]] != 2 ) //at least part of this line will be drawn
				break;

			++j;
		} while( j != 3 );

		if( j == 3 ) //not enough lines to even form a triangle
			continue;

		float *pStartPoint;
		float *pTriangleFanPoints[4]; //at most, one of our fans will have 5 points total, with the first point being stored separately as pStartPoint
		int iTriangleFanPointCount = 1; //the switch below creates the first for sure
		
		//figure out how to start the fan
		switch( iLineStates[pPoints[j]][pPoints[j+1]] )
		{
		case 0: //uncut
			pStartPoint = &vPoints[pPoints[j]].x;
			pTriangleFanPoints[0] = &vPoints[pPoints[j+1]].x;
			break;

		case 4: //second index was clipped
			pStartPoint = &vPoints[pPoints[j]].x;
			pTriangleFanPoints[0] = &vSplitPoints[pPoints[j]][pPoints[j+1]].x;
			break;

		case 3: //first index was clipped
			pStartPoint = &vSplitPoints[pPoints[j]][pPoints[j+1]].x;
			pTriangleFanPoints[0] = &vPoints[pPoints[j + 1]].x;
			break;

		default:
			Assert( false );
			break;
		};

		for( ++j; j != 3; ++j ) //add end points for the rest of the indices, we're assembling a triangle fan
		{
			switch( iLineStates[pPoints[j]][pPoints[j+1]] )
			{
			case 0: //uncut line, normal endpoint
				pTriangleFanPoints[iTriangleFanPointCount] = &vPoints[pPoints[j+1]].x;
				++iTriangleFanPointCount;
				break;

			case 2: //wholly cut line, no endpoint
				break;

			case 3: //first point is clipped, normal endpoint
				//special case, adds start and end point
				pTriangleFanPoints[iTriangleFanPointCount] = &vSplitPoints[pPoints[j]][pPoints[j+1]].x;
				++iTriangleFanPointCount;

				pTriangleFanPoints[iTriangleFanPointCount] = &vPoints[pPoints[j+1]].x;
				++iTriangleFanPointCount;
				break;

			case 4: //second point is clipped
				pTriangleFanPoints[iTriangleFanPointCount] = &vSplitPoints[pPoints[j]][pPoints[j+1]].x;
				++iTriangleFanPointCount;
				break;

			default:
				Assert( false );
				break;
			};
		}
		
		//special case endpoints, half-clipped lines have a connecting line between them and the next line (first line in this case)
		switch( iLineStates[pPoints[j]][pPoints[j+1]] )
		{
		case 3:
		case 4:
			pTriangleFanPoints[iTriangleFanPointCount] = &vSplitPoints[pPoints[j]][pPoints[j+1]].x;
			++iTriangleFanPointCount;
			break;
		};

		Assert( iTriangleFanPointCount <= 4 );

		//add the fan to the mesh
		int iLoopStop = iTriangleFanPointCount - 1;
		for( int k = 0; k != iLoopStop; ++k )
		{
			meshBuilder.Position3fv( pStartPoint );
#ifdef DEBUG_DRAWCLIPPEDDEPTHBOX
			float fHalfColors[3] = { fColors[i][0] * 0.5f, fColors[i][1] * 0.5f, fColors[i][2] * 0.5f };
			meshBuilder.Color3fv( fHalfColors );
#endif
			meshBuilder.AdvanceVertex();
			
			meshBuilder.Position3fv( pTriangleFanPoints[k] );
#ifdef DEBUG_DRAWCLIPPEDDEPTHBOX
			meshBuilder.Color3fv( fColors[i] );
#endif
			meshBuilder.AdvanceVertex();

			meshBuilder.Position3fv( pTriangleFanPoints[k+1] );
#ifdef DEBUG_DRAWCLIPPEDDEPTHBOX
			meshBuilder.Color3fv( fColors[i] );
#endif
			meshBuilder.AdvanceVertex();
		}
	}

	meshBuilder.End();
	pMesh->Draw();
	pRenderContext->Flush( false );
}

//-----------------------------------------------------------------------------
// Unified bit of draw code for opaque and translucent renderables
//-----------------------------------------------------------------------------
static inline void DrawRenderable( IClientRenderable *pEnt, int flags, const RenderableInstance_t &instance )
{
	float *pRenderClipPlane = NULL;
	if( r_entityclips.GetBool() )
		pRenderClipPlane = pEnt->GetRenderClipPlane();

	if( pRenderClipPlane )	
	{
		CMatRenderContextPtr pRenderContext( materials );
		if( !materials->UsingFastClipping() ) //do NOT change the fast clip plane mid-scene, depth problems result. Regular user clip planes are fine though
			pRenderContext->PushCustomClipPlane( pRenderClipPlane );
		else
			DrawClippedDepthBox( pEnt, pRenderClipPlane );
		Assert( view->GetCurrentlyDrawingEntity() == NULL );
		view->SetCurrentlyDrawingEntity( pEnt->GetIClientUnknown()->GetBaseEntity() );
		bool bBlockNormalDraw = false;
		if( !bBlockNormalDraw )
			pEnt->DrawModel( flags, instance );
		view->SetCurrentlyDrawingEntity( NULL );

		if( !materials->UsingFastClipping() )	
			pRenderContext->PopCustomClipPlane();
	}
	else
	{
		Assert( view->GetCurrentlyDrawingEntity() == NULL );
		view->SetCurrentlyDrawingEntity( pEnt->GetIClientUnknown()->GetBaseEntity() );
		bool bBlockNormalDraw = false;
		if( !bBlockNormalDraw )
			pEnt->DrawModel( flags, instance );
		view->SetCurrentlyDrawingEntity( NULL );
	}
}

//-----------------------------------------------------------------------------
// Draws all opaque renderables in leaves that were rendered
//-----------------------------------------------------------------------------
static inline void DrawOpaqueRenderable( IClientRenderable *pEnt, bool bTwoPass, bool bShadowDepth )
{
	ASSERT_LOCAL_PLAYER_RESOLVABLE();
	float color[3];

	Assert( !IsSplitScreenSupported() || pEnt->ShouldDrawForSplitScreenUser( GET_ACTIVE_SPLITSCREEN_SLOT() ) );
	Assert( (pEnt->GetIClientUnknown() == NULL) || (pEnt->GetIClientUnknown()->GetIClientEntity() == NULL) || (pEnt->GetIClientUnknown()->GetIClientEntity()->IsBlurred() == false) );
	pEnt->GetColorModulation( color );
	render->SetColorModulation(	color );

	int flags = STUDIO_RENDER;
	if ( bTwoPass )
	{
		flags |= STUDIO_TWOPASS;
	}

	if ( bShadowDepth )
	{
		flags |= STUDIO_SHADOWDEPTHTEXTURE;
	}

	RenderableInstance_t instance;
	instance.m_nAlpha = 255;
	DrawRenderable( pEnt, flags, instance );
}

//-------------------------------------


static void SetupBonesOnBaseAnimating( C_BaseAnimating *&pBaseAnimating )
{
	pBaseAnimating->SetupBones( NULL, -1, -1, gpGlobals->curtime );
}


static void DrawOpaqueRenderables_DrawBrushModels( int nCount, CClientRenderablesList::CEntry **ppEntities, bool bShadowDepth )
{
	for( int i = 0; i < nCount; ++i )
	{
		Assert( !ppEntities[i]->m_TwoPass );
		DrawOpaqueRenderable( ppEntities[i]->m_pRenderable, false, bShadowDepth );
	}
}

static void DrawOpaqueRenderables_DrawStaticProps( int nCount, CClientRenderablesList::CEntry **ppEntities, bool bShadowDepth )
{
	if ( nCount == 0 )
		return;

	float one[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	render->SetColorModulation(	one );
	render->SetBlend( 1.0f );
	
	const int MAX_STATICS_PER_BATCH = 512;
	IClientRenderable *pStatics[ MAX_STATICS_PER_BATCH ];
	RenderableInstance_t pInstances[ MAX_STATICS_PER_BATCH ];
	
	int numScheduled = 0, numAvailable = MAX_STATICS_PER_BATCH;

	for( int i = 0; i < nCount; ++i )
	{
		CClientRenderablesList::CEntry *itEntity = ppEntities[i];
		if ( itEntity->m_pRenderable )
			NULL;
		else
			continue;

		pInstances[ numScheduled ] = itEntity->m_InstanceData;
		pStatics[ numScheduled ++ ] = itEntity->m_pRenderable;
		if ( -- numAvailable > 0 )
			continue; // place a hint for compiler to predict more common case in the loop
		
		staticpropmgr->DrawStaticProps( pStatics, pInstances, numScheduled, bShadowDepth, vcollide_wireframe.GetBool() );
		numScheduled = 0;
		numAvailable = MAX_STATICS_PER_BATCH;
	}
	
	if ( numScheduled )
		staticpropmgr->DrawStaticProps( pStatics, pInstances, numScheduled, bShadowDepth, vcollide_wireframe.GetBool() );
}

static void DrawOpaqueRenderables_Range( int nCount, CClientRenderablesList::CEntry **ppEntities, bool bShadowDepth )
{
	for ( int i = 0; i < nCount; ++i )
	{
		CClientRenderablesList::CEntry *itEntity = ppEntities[i]; 
		if ( itEntity->m_pRenderable )
		{
			DrawOpaqueRenderable( itEntity->m_pRenderable, ( itEntity->m_TwoPass != 0 ), bShadowDepth );
		}
	}
}

extern ConVar cl_modelfastpath;
extern ConVar cl_skipslowpath;
extern ConVar r_drawothermodels;
static void	DrawOpaqueRenderables_ModelRenderables( int nCount, ModelRenderSystemData_t* pModelRenderables, bool bShadowDepth )
{
	g_pModelRenderSystem->DrawModels( pModelRenderables, nCount, bShadowDepth ? MODEL_RENDER_MODE_SHADOW_DEPTH : MODEL_RENDER_MODE_NORMAL );
}

static void	DrawOpaqueRenderables_NPCs( int nCount, CClientRenderablesList::CEntry **ppEntities, bool bShadowDepth )
{
	DrawOpaqueRenderables_Range( nCount, ppEntities, bShadowDepth );
}

//-----------------------------------------------------------------------------
// Renders all translucent entities in the render list
//-----------------------------------------------------------------------------
static inline void DrawTranslucentRenderable( IClientRenderable *pEnt, const RenderableInstance_t &instance, bool twoPass, bool bShadowDepth )
{
	ASSERT_LOCAL_PLAYER_RESOLVABLE();

	Assert( !IsSplitScreenSupported() || pEnt->ShouldDrawForSplitScreenUser( GET_ACTIVE_SPLITSCREEN_SLOT() ) );

	// Renderable list building should already have caught this
	Assert( instance.m_nAlpha > 0 );

	// Determine blending amount and tell engine
	float blend = (float)( instance.m_nAlpha / 255.0f );

	// Tell engine
	render->SetBlend( blend );

	float color[3];
	pEnt->GetColorModulation( color );
	render->SetColorModulation(	color );

	int flags = STUDIO_RENDER | STUDIO_TRANSPARENCY;
	if ( twoPass )
		flags |= STUDIO_TWOPASS;

	if ( bShadowDepth )
		flags |= STUDIO_SHADOWDEPTHTEXTURE;

	DrawRenderable( pEnt, flags, instance );
}




static ConVar r_unlimitedrefract( "r_unlimitedrefract", "0" );
extern ConVar cl_tlucfastpath;
extern ConVar cl_colorfastpath;


//-----------------------------------------------------------------------------
// Standard 3d skybox view
//-----------------------------------------------------------------------------
SkyboxVisibility_t CSkyboxViewDeferred::ComputeSkyboxVisibility()
{
	if ( ( enginetrace->GetPointContents( origin ) & CONTENTS_SOLID ) != 0 )
		return SKYBOX_NOT_VISIBLE;

	return engine->IsSkyboxVisibleFromPoint( origin );
}


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
bool CSkyboxViewDeferred::GetSkyboxFogEnable()
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return false;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	static ConVarRef fog_override( "fog_override" );
	if( fog_override.GetInt() )
	{
		if( fog_enableskybox.GetInt() )
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return !!local->m_skybox3d.fog.enable;
	}
}


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CSkyboxViewDeferred::Enable3dSkyboxFog( void )
{
	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();
	if( !pbp )
	{
		return;
	}
	CPlayerLocalData	*local		= &pbp->m_Local;

	CMatRenderContextPtr pRenderContext( materials );

	if( GetSkyboxFogEnable() )
	{
		float fogColor[3];
		GetSkyboxFogColor( fogColor );
		float scale = 1.0f;
		if ( local->m_skybox3d.scale > 0.0f )
		{
			scale = 1.0f / local->m_skybox3d.scale;
		}
		pRenderContext->FogMode( MATERIAL_FOG_LINEAR );
		pRenderContext->FogColor3fv( fogColor );
		pRenderContext->FogStart( GetSkyboxFogStart() * scale );
		pRenderContext->FogEnd( GetSkyboxFogEnd() * scale );
		pRenderContext->FogMaxDensity( GetSkyboxFogMaxDensity() );
	}
	else
	{
		pRenderContext->FogMode( MATERIAL_FOG_NONE );
	}
}


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
sky3dparams_t *CSkyboxViewDeferred::PreRender3dSkyboxWorld( SkyboxVisibility_t nSkyboxVisible )
{
	if ( ( nSkyboxVisible != SKYBOX_3DSKYBOX_VISIBLE ) && r_3dsky.GetInt() != 2 )
		return NULL;

	// render the 3D skybox
	if ( !r_3dsky.GetInt() )
		return NULL;

	C_BasePlayer *pbp = C_BasePlayer::GetLocalPlayer();

	// No local player object yet...
	if ( !pbp )
		return NULL;

	CPlayerLocalData* local = &pbp->m_Local;
	if ( local->m_skybox3d.area == 255 )
		return NULL;

	return &local->m_skybox3d;
}


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CSkyboxViewDeferred::DrawInternal( view_id_t iSkyBoxViewID, bool bInvokePreAndPostRender, ITexture *pRenderTarget )
{
	bInvokePreAndPostRender = !m_bGBufferPass;

	if ( m_bGBufferPass )
	{
		m_DrawFlags |= DF_SKIP_WORLD_DECALS_AND_OVERLAYS;
	}

	unsigned char **areabits = render->GetAreaBits();
	unsigned char *savebits;
	unsigned char tmpbits[ 32 ];
	savebits = *areabits;
	memset( tmpbits, 0, sizeof(tmpbits) );

	// set the sky area bit
	tmpbits[m_pSky3dParams->area>>3] |= 1 << (m_pSky3dParams->area&7);

	*areabits = tmpbits;

	// if you can get really close to the skybox geometry it's possible that you'll be able to clip into it
	// with this near plane.  If so, move it in a bit.  It's at 2.0 to give us more precision.  That means you 
	// need to keep the eye position at least 2 * scale away from the geometry in the skybox
	zNear = 2.0;
	zFar = 10000.0f; //MAX_TRACE_LENGTH;

	float skyScale = 1.0f;
	// scale origin by sky scale and translate to sky origin
	{
		skyScale = (m_pSky3dParams->scale > 0) ? m_pSky3dParams->scale : 1.0f;
		float scale = 1.0f / skyScale;

		Vector vSkyOrigin = m_pSky3dParams->origin;
		VectorScale( origin, scale, origin );
		VectorAdd( origin, vSkyOrigin, origin );

		if( m_bCustomViewMatrix )
		{
			Vector vTransformedSkyOrigin;
			VectorRotate( vSkyOrigin, m_matCustomViewMatrix, vTransformedSkyOrigin ); //Rotate instead of transform because we haven't scale the existing offset yet

			//scale existing translation, and tack on the skybox offset (subtract because it's a view matrix)
			m_matCustomViewMatrix.m_flMatVal[0][3] = (m_matCustomViewMatrix.m_flMatVal[0][3] * scale) - vTransformedSkyOrigin.x;
			m_matCustomViewMatrix.m_flMatVal[1][3] = (m_matCustomViewMatrix.m_flMatVal[1][3] * scale) - vTransformedSkyOrigin.y;
			m_matCustomViewMatrix.m_flMatVal[2][3] = (m_matCustomViewMatrix.m_flMatVal[2][3] * scale) - vTransformedSkyOrigin.z;
		}
	}

	if ( !m_bGBufferPass )
		Enable3dSkyboxFog();

	// BUGBUG: Fix this!!!  We shouldn't need to call setup vis for the sky if we're connecting
	// the areas.  We'd have to mark all the clusters in the skybox area in the PVS of any 
	// cluster with sky.  Then we could just connect the areas to do our vis.
	//m_bOverrideVisOrigin could hose us here, so call direct
	render->ViewSetupVis( false, 1, &m_pSky3dParams->origin.Get() );
	render->Push3DView( (*this), m_ClearFlags, pRenderTarget, GetFrustum() );

	if ( m_bGBufferPass )
		PushGBuffer( true, skyScale );
	else
		PushComposite();

	// Store off view origin and angles
	SetupCurrentView( origin, angles, iSkyBoxViewID );

#if defined( _X360 )
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushVertexShaderGPRAllocation( 32 );
	pRenderContext.SafeRelease();
#endif

	// Invoke pre-render methods
	if ( bInvokePreAndPostRender )
	{
		IGameSystem::PreRenderAllSystems();
	}

	BuildWorldRenderLists( true, -1, ShouldCacheLists() );

	BuildRenderableRenderLists( m_bGBufferPass ? VIEW_SHADOW_DEPTH_TEXTURE : iSkyBoxViewID );

	DrawWorld( 0.0f );

	// Iterate over all leaves and render objects in those leaves
	DrawOpaqueRenderables( false );

	if ( !m_bGBufferPass )
	{
		// Iterate over all leaves and render objects in those leaves
		DrawTranslucentRenderables( true, false );
		DrawNoZBufferTranslucentRenderables();
	}

	if ( !m_bGBufferPass )
	{
		m_pMainView->DisableFog();

		CGlowOverlay::UpdateSkyOverlays( zFar, m_bCacheFullSceneState );

		PixelVisibility_EndCurrentView();
	}

	// restore old area bits
	*areabits = savebits;

	// Invoke post-render methods
	if( bInvokePreAndPostRender )
	{
		IGameSystem::PostRenderAllSystems();
		FinishCurrentView();
	}

	if ( m_bGBufferPass )
		PopGBuffer();
	else
		PopComposite();

	render->PopView( GetFrustum() );

#if defined( _X360 )
	pRenderContext.GetFrom( materials );
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
bool CSkyboxViewDeferred::Setup( const CViewSetup &view, bool bGBuffer, SkyboxVisibility_t *pSkyboxVisible )
{
	BaseClass::Setup( view );

	// The skybox might not be visible from here
	*pSkyboxVisible = ComputeSkyboxVisibility();
	m_pSky3dParams = PreRender3dSkyboxWorld( *pSkyboxVisible );

	if ( !m_pSky3dParams )
	{
		return false;
	}

	m_bGBufferPass = bGBuffer;
	// At this point, we've cleared everything we need to clear
	// The next path will need to clear depth, though.
	m_ClearFlags = VIEW_CLEAR_DEPTH; //*pClearFlags;
	//*pClearFlags &= ~( VIEW_CLEAR_COLOR | VIEW_CLEAR_DEPTH | VIEW_CLEAR_STENCIL | VIEW_CLEAR_FULL_TARGET );
	//*pClearFlags |= VIEW_CLEAR_DEPTH; // Need to clear depth after rednering the skybox

	m_DrawFlags = DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER | DF_RENDER_WATER;
	if( !m_bGBufferPass && r_skybox.GetBool() )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	return true;
}


//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
void CSkyboxViewDeferred::Draw()
{
	VPROF_BUDGET( "CViewRender::Draw3dSkyboxworld", "3D Skybox" );

	DrawInternal();
}



void CGBufferView::Setup( const CViewSetup &view, bool bDrewSkybox )
{
	m_fogInfo.m_bEyeInFogVolume = false;
	m_bDrewSkybox = bDrewSkybox;

	BaseClass::Setup( view );
	m_bDrawWorldNormal = true;

	m_ClearFlags = 0;
	m_DrawFlags = DF_DRAW_ENTITITES;

	m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
}

void CGBufferView::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldViewDeferred::Draw" );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 ); //lean toward pixel shader threads
#endif

	SetupCurrentView( origin, angles, VIEW_DEFERRED_GBUFFER );

	pRenderContext.SafeRelease();

	DrawSetup( 0, m_DrawFlags, 0 );

	pRenderContext.SafeRelease();

	DrawExecute( 0, CurrentViewID(), 0, true );

	pRenderContext.GetFrom( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

void CGBufferView::PushView( float waterHeight )
{
	PushGBuffer( !m_bDrewSkybox );
}

void CGBufferView::PopView()
{
	PopGBuffer();
}

void CGBufferView::PushGBuffer( bool bInitial, float zScale )
{
	static ITexture *pNormals = GetDefRT_Normals();
	static ITexture *pDepth = GetDefRT_Depth();

	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->ClearColor4ub( 0, 0, 0, 0 );

	if ( bInitial )
	{
		pRenderContext->PushRenderTargetAndViewport( pDepth );
		pRenderContext->ClearBuffers( true, false );
		pRenderContext->PopRenderTargetAndViewport();
	}

	pRenderContext->PushRenderTargetAndViewport( pNormals );
	pRenderContext->ClearBuffers( false, true );

	pRenderContext->SetRenderTargetEx( 1, pDepth );

	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_GBUFFER );

	struct defData_setZScale
	{
	public:
		float zScale;

		static void Fire( defData_setZScale d )
		{
			GetDeferredExt()->CommitZScale( d.zScale );
		};
	};

	defData_setZScale data;
	data.zScale = zScale;
	QUEUE_FIRE( defData_setZScale, Fire, data );
}

void CGBufferView::PopGBuffer()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );

	pRenderContext->PopRenderTargetAndViewport();
}




//-----------------------------------------------------------------------------
// Pops a water render target
//-----------------------------------------------------------------------------
bool CBaseWorldViewDeferred::AdjustView( float waterHeight )
{
	if( m_DrawFlags & DF_RENDER_REFRACTION )
	{
		ITexture *pTexture = GetWaterRefractionTexture();

		// Use the aspect ratio of the main view! So, don't recompute it here
		x = y = 0;
		width = pTexture->GetActualWidth();
		height = pTexture->GetActualHeight();

		return true;
	}

	if( m_DrawFlags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();

		// Use the aspect ratio of the main view! So, don't recompute it here
		x = y = 0;
		width = pTexture->GetActualWidth();
		height = pTexture->GetActualHeight();
		angles[0] = -angles[0];
		angles[2] = -angles[2];
		origin[2] -= 2.0f * ( origin[2] - (waterHeight));
		return true;
	}

	return false;
}


//-----------------------------------------------------------------------------
// Pops a water render target
//-----------------------------------------------------------------------------
void CBaseWorldViewDeferred::PushView( float waterHeight )
{
	float spread = 2.0f;
	if( m_DrawFlags & DF_FUDGE_UP )
	{
		waterHeight += spread;
	}
	else
	{
		waterHeight -= spread;
	}

	MaterialHeightClipMode_t clipMode = MATERIAL_HEIGHTCLIPMODE_DISABLE;
	if ( ( m_DrawFlags & DF_CLIP_Z ) && mat_clipz.GetBool() )
	{
		if( m_DrawFlags & DF_CLIP_BELOW )
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_ABOVE_HEIGHT;
		}
		else
		{
			clipMode = MATERIAL_HEIGHTCLIPMODE_RENDER_BELOW_HEIGHT;
		}
	}

	CMatRenderContextPtr pRenderContext( materials );

	if( m_DrawFlags & DF_RENDER_REFRACTION )
	{
		pRenderContext->SetFogZ( waterHeight );
		pRenderContext->SetHeightClipZ( waterHeight );
		pRenderContext->SetHeightClipMode( clipMode );

		// Have to re-set up the view since we reset the size
		render->Push3DView( *this, m_ClearFlags, GetWaterRefractionTexture(), GetFrustum() );

		return;
	}

	if( m_DrawFlags & DF_RENDER_REFLECTION )
	{
		ITexture *pTexture = GetWaterReflectionTexture();

		pRenderContext->SetFogZ( waterHeight );

		bool bSoftwareUserClipPlane = g_pMaterialSystemHardwareConfig->UseFastClipping();
		if( bSoftwareUserClipPlane && ( origin[2] > waterHeight - r_eyewaterepsilon.GetFloat() ) )
		{
			waterHeight = origin[2] + r_eyewaterepsilon.GetFloat();
		}

		pRenderContext->SetHeightClipZ( waterHeight );
		pRenderContext->SetHeightClipMode( clipMode );

		render->Push3DView( *this, m_ClearFlags, pTexture, GetFrustum() );
		return;
	}

	if ( m_ClearFlags & ( VIEW_CLEAR_DEPTH | VIEW_CLEAR_COLOR | VIEW_CLEAR_STENCIL ) )
	{
		if ( m_ClearFlags & VIEW_CLEAR_OBEY_STENCIL )
		{
			pRenderContext->ClearBuffersObeyStencil( ( m_ClearFlags & VIEW_CLEAR_COLOR ) ? true : false, ( m_ClearFlags & VIEW_CLEAR_DEPTH ) ? true : false );
		}
		else
		{
			pRenderContext->ClearBuffers( ( m_ClearFlags & VIEW_CLEAR_COLOR ) ? true : false, ( m_ClearFlags & VIEW_CLEAR_DEPTH ) ? true : false, ( m_ClearFlags & VIEW_CLEAR_STENCIL ) ? true : false );
		}
	}

	pRenderContext->SetHeightClipMode( clipMode );
	if ( clipMode != MATERIAL_HEIGHTCLIPMODE_DISABLE )
	{   
		pRenderContext->SetHeightClipZ( waterHeight );
	}
}


//-----------------------------------------------------------------------------
// Pops a water render target
//-----------------------------------------------------------------------------
void CBaseWorldViewDeferred::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );

	pRenderContext->SetHeightClipMode( MATERIAL_HEIGHTCLIPMODE_DISABLE );
	if( m_DrawFlags & (DF_RENDER_REFRACTION | DF_RENDER_REFLECTION) )
	{
		if ( IsX360() )
		{
			// these renders paths used their surfaces, so blit their results
			if ( m_DrawFlags & DF_RENDER_REFRACTION )
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetWaterRefractionTexture(), NULL, NULL );
			}
			if ( m_DrawFlags & DF_RENDER_REFLECTION )
			{
				pRenderContext->CopyRenderTargetToTextureEx( GetWaterReflectionTexture(), NULL, NULL );
			}
		}

		render->PopView( GetFrustum() );
	}
}


//-----------------------------------------------------------------------------
// Draws the world + entities
//-----------------------------------------------------------------------------
void CBaseWorldViewDeferred::DrawSetup( float waterHeight, int nSetupFlags, float waterZAdjust, int iForceViewLeaf, bool bShadowDepth )
{
	int savedViewID = g_CurrentViewID;
	g_CurrentViewID = bShadowDepth ? VIEW_DEFERRED_SHADOW : VIEW_MAIN;

	bool bViewChanged = AdjustView( waterHeight );

	if ( bViewChanged )
	{
		render->Push3DView( *this, 0, NULL, GetFrustum() );
	}

	bool bDrawEntities = ( nSetupFlags & DF_DRAW_ENTITITES ) != 0;
	bool bDrawReflection = ( nSetupFlags & DF_RENDER_REFLECTION ) != 0;
	BuildWorldRenderLists( bDrawEntities, iForceViewLeaf, ShouldCacheLists(), false, bDrawReflection ? &waterHeight : NULL );

	PruneWorldListInfo();

	if ( bDrawEntities )
	{
		bool bOptimized = bShadowDepth || savedViewID == VIEW_DEFERRED_GBUFFER;
		BuildRenderableRenderLists( bOptimized ? VIEW_SHADOW_DEPTH_TEXTURE : savedViewID );
	}

	if ( bViewChanged )
	{
		render->PopView( GetFrustum() );
	}

	g_CurrentViewID = savedViewID;
}


void CBaseWorldViewDeferred::DrawExecute( float waterHeight, view_id_t viewID, float waterZAdjust, bool bShadowDepth )
{
	// @MULTICORE (toml 8/16/2006): rethink how, where, and when this is done...
	//g_pClientShadowMgr->ComputeShadowTextures( *this, m_pWorldListInfo->m_LeafCount, m_pWorldListInfo->m_pLeafDataList );

	// Make sure sound doesn't stutter
	engine->Sound_ExtraUpdate();

	int savedViewID = g_CurrentViewID;
	g_CurrentViewID = viewID;

	// Update our render view flags.
	int iDrawFlagsBackup = m_DrawFlags;
	m_DrawFlags |= m_pMainView->GetBaseDrawFlags();

	PushView( waterHeight );

	CMatRenderContextPtr pRenderContext( materials );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 );
#endif

	ITexture *pSaveFrameBufferCopyTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
	pRenderContext->SetFrameBufferCopyTexture( GetPowerOfTwoFrameBufferTexture() );
	pRenderContext.SafeRelease();


	Begin360ZPass();
	m_DrawFlags |= DF_SKIP_WORLD_DECALS_AND_OVERLAYS;
	DrawWorld( waterZAdjust );
	m_DrawFlags &= ~DF_SKIP_WORLD_DECALS_AND_OVERLAYS;
	if ( m_DrawFlags & DF_DRAW_ENTITITES )
	{
		DrawOpaqueRenderables( false );
	}
	End360ZPass();		// DrawOpaqueRenderables currently already calls End360ZPass. No harm in calling it again to make sure we're always ending it

	// Only draw decals on opaque surfaces after now. Benefit is two-fold: Early Z benefits on PC, and
	// we're pulling out stuff that uses the dynamic VB from the 360 Z pass
	// (which can lead to rendering corruption if we overflow the dyn. VB ring buffer).
	if ( !bShadowDepth )
	{
		m_DrawFlags |= DF_SKIP_WORLD;
		DrawWorld( waterZAdjust );
		m_DrawFlags &= ~DF_SKIP_WORLD;
	}
		
	if ( !m_bDrawWorldNormal )
	{
		if ( m_DrawFlags & DF_DRAW_ENTITITES )
		{
			DrawTranslucentRenderables( false, false );
			if ( !bShadowDepth )
				DrawNoZBufferTranslucentRenderables();
		}
		else
		{
			// Draw translucent world brushes only, no entities
			DrawTranslucentWorldInLeaves( false );
		}
	}

	pRenderContext.GetFrom( materials );
	pRenderContext->SetFrameBufferCopyTexture( pSaveFrameBufferCopyTexture );
	PopView();

	m_DrawFlags = iDrawFlagsBackup;

	g_CurrentViewID = savedViewID;

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}

void CBaseWorldViewDeferred::PushComposite()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_COMPOSITION );
}

void CBaseWorldViewDeferred::PopComposite()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );
}

//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
void CSimpleWorldViewDeferred::Setup( const CViewSetup &view, int nClearFlags, bool bDrawSkybox,
	const VisibleFogVolumeInfo_t &fogInfo, const WaterRenderInfo_t &waterInfo, ViewCustomVisibility_t *pCustomVisibility )
{
	BaseClass::Setup( view );

	m_ClearFlags = nClearFlags;
	m_DrawFlags = DF_DRAW_ENTITITES;

	if ( !waterInfo.m_bOpaqueWater )
	{
		m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	}
	else
	{
		bool bViewIntersectsWater = DoesViewPlaneIntersectWater( fogInfo.m_flWaterHeight, fogInfo.m_nVisibleFogVolume );
		if( bViewIntersectsWater )
		{
			// have to draw both sides if we can see both.
			m_DrawFlags |= DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
		}
		else if ( fogInfo.m_bEyeInFogVolume )
		{
			m_DrawFlags |= DF_RENDER_UNDERWATER;
		}
		else
		{
			m_DrawFlags |= DF_RENDER_ABOVEWATER;
		}
	}
	if ( waterInfo.m_bDrawWaterSurface )
	{
		m_DrawFlags |= DF_RENDER_WATER;
	}

	if ( !fogInfo.m_bEyeInFogVolume && bDrawSkybox )
	{
		m_DrawFlags |= DF_DRAWSKYBOX;
	}

	m_pCustomVisibility = pCustomVisibility;
	m_fogInfo = fogInfo;
}


//-----------------------------------------------------------------------------
// Draws the scene when there's no water or only cheap water
//-----------------------------------------------------------------------------
void CSimpleWorldViewDeferred::Draw()
{
	VPROF( "CViewRender::ViewDrawScene_NoWater" );

	CMatRenderContextPtr pRenderContext( materials );
	PIXEVENT( pRenderContext, "CSimpleWorldViewDeferred::Draw" );

#if defined( _X360 )
	pRenderContext->PushVertexShaderGPRAllocation( 32 ); //lean toward pixel shader threads
#endif
	pRenderContext.SafeRelease();

	PushComposite();

	DrawSetup( 0, m_DrawFlags, 0 );

	if ( !m_fogInfo.m_bEyeInFogVolume )
	{
		EnableWorldFog();
	}
	else
	{
		m_ClearFlags |= VIEW_CLEAR_COLOR;

		SetFogVolumeState( m_fogInfo, false );

		pRenderContext.GetFrom( materials );

		unsigned char ucFogColor[3];
		pRenderContext->GetFogColor( ucFogColor );
		pRenderContext->ClearColor4ub( ucFogColor[0], ucFogColor[1], ucFogColor[2], 255 );
	}

	pRenderContext.SafeRelease();

	DrawExecute( 0, CurrentViewID(), 0 );

	PopComposite();

	pRenderContext.GetFrom( materials );
	pRenderContext->ClearColor4ub( 0, 0, 0, 255 );

#if defined( _X360 )
	pRenderContext->PopVertexShaderGPRAllocation();
#endif
}



void CBaseShadowView::Setup( const CViewSetup &view, ITexture *pDepthTexture, ITexture *pDummyTexture )
{
	m_pDepthTexture = pDepthTexture;
	m_pDummyTexture = pDummyTexture;

	BaseClass::Setup( view );

	m_bDrawWorldNormal = true;

	m_DrawFlags = DF_DRAW_ENTITITES | DF_RENDER_UNDERWATER | DF_RENDER_ABOVEWATER;
	m_ClearFlags = 0;

	CalcShadowView();

	m_pCustomVisibility = &shadowVis;
	shadowVis.AddVisOrigin( origin );
}

void CBaseShadowView::Draw()
{
	int oldViewID = g_CurrentViewID;
	SetupCurrentView( origin, angles, VIEW_DEFERRED_SHADOW );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_SHADOWPASS );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_MODE,
		GetShadowMode() );
	pRenderContext.SafeRelease();

	DrawSetup( 0, m_DrawFlags, 0, -1, true );

	DrawExecute( 0, CurrentViewID(), 0, true );

	pRenderContext.GetFrom( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_RENDER_STAGE,
		DEFERRED_RENDER_STAGE_INVALID );

	g_CurrentViewID = oldViewID;
}

bool CBaseShadowView::AdjustView( float waterHeight )
{
	CommitData();

	return true;
}

void CBaseShadowView::PushView( float waterHeight )
{
	render->Push3DView( *this, 0, m_pDummyTexture, GetFrustum(), m_pDepthTexture );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PushRenderTargetAndViewport( m_pDummyTexture, m_pDepthTexture, x, y, width, height );

#if defined( DEBUG ) || defined( SHADOWMAPPING_USE_COLOR )
	pRenderContext->ClearColor4ub( 255, 255, 255, 255 );
	pRenderContext->ClearBuffers( true, true );
#else
	pRenderContext->ClearBuffers( false, true );
#endif
}

void CBaseShadowView::PopView()
{
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->PopRenderTargetAndViewport();

	render->PopView( GetFrustum() );
}


void COrthoShadowView::CalcShadowView()
{
	const cascade_t &m_data = GetCascadeInfo( iCascadeIndex );
	Vector mainFwd;
	AngleVectors( angles, &mainFwd );

	lightData_Global_t state = GetActiveGlobalLightState();
	QAngle lightAng;
	VectorAngles( -state.vecLight.AsVector3D(), lightAng );

	Vector viewFwd, viewRight, viewUp;
	AngleVectors( lightAng, &viewFwd, &viewRight, &viewUp );

	const float halfOrthoSize = m_data.flProjectionSize * 0.5f;

	origin += -viewFwd * m_data.flOriginOffset +
		viewUp * halfOrthoSize -
		viewRight * halfOrthoSize +
		mainFwd * halfOrthoSize;

	angles = lightAng;

	x = 0;
	y = 0;
	height = m_data.iResolution;
	width = m_data.iResolution;

	m_bOrtho = true;
	m_OrthoLeft = 0;
	m_OrthoTop = -m_data.flProjectionSize;
	m_OrthoRight = m_data.flProjectionSize;
	m_OrthoBottom = 0;

	zNear = zNearViewmodel = 0;
	zFar = zFarViewmodel = m_data.flFarZ;
	m_flAspectRatio = 0;

	float mapping_world = m_data.flProjectionSize / m_data.iResolution;
	origin -= fmod( DotProduct( viewRight, origin ), mapping_world ) * viewRight;
	origin -= fmod( DotProduct( viewUp, origin ), mapping_world ) * viewUp;

	origin -= fmod( DotProduct( viewFwd, origin ), GetDepthMapDepthResolution( zFar - zNear ) ) * viewFwd;

#if CSM_USE_COMPOSITED_TARGET
	x = m_data.iViewport_x;
	y = m_data.iViewport_y;
#endif
}

void COrthoShadowView::CommitData()
{
	struct sendShadowDataOrtho
	{
		shadowData_ortho_t data;
		int index;
		static void Fire( sendShadowDataOrtho d )
		{
			IDeferredExtension *pDef = GetDeferredExt();
			pDef->CommitShadowData_Ortho( d.index, d.data );
		};
	};

	const cascade_t &data = GetCascadeInfo( iCascadeIndex );

	Vector fwd, right, down;
	AngleVectors( angles, &fwd, &right, &down );
	down *= -1.0f;

	Vector vecScale( m_OrthoRight, abs( m_OrthoTop ), zFar - zNear );

	sendShadowDataOrtho shadowData;
	shadowData.index = iCascadeIndex;

#if CSM_USE_COMPOSITED_TARGET
	shadowData.data.iRes_x = CSM_COMP_RES_X;
	shadowData.data.iRes_y = CSM_COMP_RES_Y;
#else
	shadowData.data.iRes_x = width;
	shadowData.data.iRes_y = height;
#endif

	shadowData.data.vecSlopeSettings.Init(
		data.flSlopeScaleMin, data.flSlopeScaleMax, data.flNormalScaleMax, zFar
		);
	shadowData.data.vecOrigin.Init( origin );

	Vector4D matrix_scale_offset( 0.5f, -0.5f, 0.5f, 0.5f );

#if CSM_USE_COMPOSITED_TARGET
	shadowData.data.vecUVTransform.Init( x / (float)CSM_COMP_RES_X,
		y / (float)CSM_COMP_RES_Y,
		width / (float) CSM_COMP_RES_X,
		height / (float) CSM_COMP_RES_Y );
#endif

	VMatrix a,b,c,d,screenToTexture;
	render->GetMatricesForView( *this, &a, &b, &c, &d );
	MatrixBuildScale( screenToTexture, matrix_scale_offset.x,
		matrix_scale_offset.y,
		1.0f );

	screenToTexture[0][3] = matrix_scale_offset.z;
	screenToTexture[1][3] = matrix_scale_offset.w;

	MatrixMultiply( screenToTexture, c, shadowData.data.matWorldToTexture );

	QUEUE_FIRE( sendShadowDataOrtho, Fire, shadowData );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, iCascadeIndex );
}


bool CDualParaboloidShadowView::AdjustView( float waterHeight )
{
	BaseClass::AdjustView( waterHeight );

	// HACK: when pushing our actual view the renderer fails building the worldlist right!
	// So we can't. Shit.
	return false;
}

void CDualParaboloidShadowView::PushView( float waterHeight )
{
	BaseClass::PushView( waterHeight );
}

void CDualParaboloidShadowView::PopView()
{
	BaseClass::PopView();
}

void CDualParaboloidShadowView::CalcShadowView()
{
	float flRadius = m_pLight->flRadius;

	m_bOrtho = true;
	m_OrthoTop = m_OrthoLeft = -flRadius;
	m_OrthoBottom = m_OrthoRight = flRadius;

	int dpsmRes = GetShadowResolution_Point();

	width = dpsmRes;
	height = dpsmRes;

	zNear = zNearViewmodel = 0;
	zFar = zFarViewmodel = flRadius;

	if ( m_bSecondary )
	{
		y = dpsmRes;

		Vector fwd, up;
		AngleVectors( angles, &fwd, NULL, &up );
		VectorAngles( -fwd, up, angles );
	}
}


void CSpotLightShadowView::CalcShadowView()
{
	float flRadius = m_pLight->flRadius;

	int spotRes = GetShadowResolution_Spot();

	width = spotRes;
	height = spotRes;

	zNear = zNearViewmodel = DEFLIGHT_SPOT_ZNEAR;
	zFar = zFarViewmodel = flRadius;

	fov = fovViewmodel = m_pLight->GetFOV();
}

void CSpotLightShadowView::CommitData()
{
	struct sendShadowDataProj
	{
		shadowData_proj_t data;
		int index;
		static void Fire( sendShadowDataProj d )
		{
			IDeferredExtension *pDef = GetDeferredExt();
			pDef->CommitShadowData_Proj( d.index, d.data );
		};
	};

	Vector fwd;
	AngleVectors( angles, &fwd );

	sendShadowDataProj data;
	data.index = m_iIndex;
	data.data.vecForward.Init( fwd );
	data.data.vecOrigin.Init( origin );
	// slope min, slope max, normal max, depth
	//data.data.vecSlopeSettings.Init( 0.005f, 0.02f, 3, zFar );
	data.data.vecSlopeSettings.Init( 0.001f, 0.005f, 3, zFar );

	QUEUE_FIRE( sendShadowDataProj, Fire, data );

	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_DEFERRED_SHADOW_INDEX, m_iIndex );
}

