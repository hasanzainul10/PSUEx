#include "FrameTrace.h"
#include "../Misc/Log.h"
#include <cstring>

namespace FrameTrace
{
	static unsigned s_frame = 0;
	static bool s_started = false;
	static bool s_done = false;

	static unsigned s_beginScene = 0;
	static unsigned s_endScene = 0;
	static unsigned s_setRT = 0;
	static unsigned s_setView = 0;
	static unsigned s_setProj = 0;
	static unsigned s_uiDraw = 0;

	static bool s_inSceneCallback = false;
	static unsigned s_cbAddress = 0;
	static unsigned s_enterLeaveFrame = 0;
	static bool s_enterLeaveDone = false;

	static bool s_gameplay = false;
	static unsigned s_gameplayLeaveLogs = 0;
	static bool s_dipCaptureDone = false;

	static void *s_dipInPre[ kMaxDipSamples ]{};
	static unsigned s_dipInPreCount = 0;
	static void *s_dipInPost[ kMaxDipSamples ]{};
	static unsigned s_dipInPostCount = 0;
	static void *s_dipOut[ kMaxDipSamples ]{};
	static unsigned s_dipOutCount = 0;

	static unsigned s_cbUiDraw = 0;
	static unsigned s_cbDipPre = 0;
	static unsigned s_cbDipPost = 0;
	static bool s_loggedFirstUiInside = false;

	// Render-pass tracing (0x004FD990 / 0x004FDBC0).
	struct PassStats
	{
		unsigned address = 0;
		unsigned logged = 0;
		unsigned uniqueLogged = 0;
		void *transformRets[ 8 ]{};
		unsigned transformRetCount = 0;
		unsigned vscKeys[ 12 ]{};
		unsigned vscKeyCount = 0;
	};
	constexpr unsigned kMaxPasses = 4;
	constexpr unsigned kMaxPassLogs = 8;
	static PassStats s_passes[ kMaxPasses ];
	static PassStats *s_curPass = nullptr;
	static unsigned s_pDip = 0;
	static unsigned s_pDp = 0;
	static unsigned s_pDpUp = 0;
	static unsigned s_pUi = 0;
	static unsigned s_pView = 0;
	static unsigned s_pProj = 0;
	static unsigned s_pWorld = 0;
	static unsigned s_pVsc = 0;
	static unsigned s_pFirstUiAfterDip = 0;

	// Camera trace: every VIEW/PROJ set (any caller) for a few town frames.
	constexpr unsigned kCamTraceFrames = 3;
	static bool s_camTraceArmed = false;
	static bool s_camTraceOn = false;
	static unsigned s_camTraceFrame = 0;
	static unsigned s_camWorldLogged = 0;

	static PassStats *FindPass( unsigned address )
	{
		for( auto &p : s_passes )
		{
			if( p.address == address )
			{
				return &p;
			}
		}
		for( auto &p : s_passes )
		{
			if( p.address == 0 )
			{
				p.address = address;
				return &p;
			}
		}
		return nullptr;
	}

	static void ResetCounters()
	{
		s_beginScene = 0;
		s_endScene = 0;
		s_setRT = 0;
		s_setView = 0;
		s_setProj = 0;
		s_uiDraw = 0;
	}

	static bool AddUnique( void **arr, unsigned &count, void *ret )
	{
		for( unsigned i = 0; i < count; ++i )
		{
			if( arr[ i ] == ret )
			{
				return false;
			}
		}
		if( count >= kMaxDipSamples )
		{
			return false;
		}
		arr[ count++ ] = ret;
		return true;
	}

	static void MaybeFinishDipCapture()
	{
		if( s_dipCaptureDone )
		{
			return;
		}
		// Done on a full bucket, or once gameplay has produced any out-of-SceneCB DIP
		// (town funnels all DIP through one wrapper — unique count may stay at 1).
		const bool inFull = ( s_dipInPreCount >= kMaxDipSamples ) || ( s_dipInPostCount >= kMaxDipSamples );
		const bool outFull = ( s_dipOutCount >= kMaxDipSamples );
		const bool outTownHit = s_gameplay && ( s_dipOutCount > 0 ) && ( s_gameplayLeaveLogs >= 3 );
		if( !inFull && !outFull && !outTownHit )
		{
			return;
		}
		s_dipCaptureDone = true;
		PSU_LOG( "SceneCB: 3D DIP capture done inPre=%u inPost=%u out=%u\n",
			s_dipInPreCount, s_dipInPostCount, s_dipOutCount );
		for( unsigned i = 0; i < s_dipInPreCount; ++i )
		{
			PSU_LOG( "SceneCB DIP(pre-UI) ret[%u]=%p\n", i, s_dipInPre[ i ] );
		}
		for( unsigned i = 0; i < s_dipInPostCount; ++i )
		{
			PSU_LOG( "SceneCB DIP(post-UI) ret[%u]=%p\n", i, s_dipInPost[ i ] );
		}
		for( unsigned i = 0; i < s_dipOutCount; ++i )
		{
			PSU_LOG( "SceneCB DIP(out) ret[%u]=%p\n", i, s_dipOut[ i ] );
		}
	}

	bool Active()
	{
		return s_started && !s_done;
	}

	bool InSceneCallback()
	{
		return s_inSceneCallback;
	}

	bool GameplayArmed()
	{
		return s_gameplay;
	}

	void NotifyGameplay()
	{
		if( s_gameplay )
		{
			return;
		}
		s_gameplay = true;
		s_gameplayLeaveLogs = 0;
		// Re-arm if title left us empty.
		if( s_dipInPreCount == 0 && s_dipInPostCount == 0 && s_dipOutCount == 0 )
		{
			s_dipCaptureDone = false;
		}
		PSU_LOG( "FrameTrace: gameplay armed — SceneCB leave samples + DIP in/out/pre/post until capture done\n" );
	}

	static bool EnterLeaveActive()
	{
		return !s_enterLeaveDone && s_enterLeaveFrame < kMaxCallbackFrames;
	}

	void SceneCallbackEnter( unsigned callbackAddress )
	{
		s_inSceneCallback = true;
		s_cbAddress = callbackAddress;
		s_cbUiDraw = 0;
		s_cbDipPre = 0;
		s_cbDipPost = 0;
		s_loggedFirstUiInside = false;
		if( EnterLeaveActive() )
		{
			PSU_LOG( "SceneCB[%u] ENTER 0x%08X\n", s_enterLeaveFrame, callbackAddress );
		}
	}

	void SceneCallbackLeave()
	{
		const unsigned dipTotal = s_cbDipPre + s_cbDipPost;

		if( EnterLeaveActive() )
		{
			PSU_LOG( "SceneCB[%u] LEAVE uiDraw=%u dipPre=%u dipPost=%u (global pre=%u post=%u out=%u)\n",
				s_enterLeaveFrame, s_cbUiDraw, s_cbDipPre, s_cbDipPost,
				s_dipInPreCount, s_dipInPostCount, s_dipOutCount );
			++s_enterLeaveFrame;
			if( s_enterLeaveFrame >= kMaxCallbackFrames )
			{
				s_enterLeaveDone = true;
				PSU_LOG( "SceneCB: ENTER/LEAVE breadcrumbs done (%u frames); DIP capture still armed\n",
					kMaxCallbackFrames );
			}
		}
		else if( s_gameplay && !s_dipCaptureDone && s_gameplayLeaveLogs < kMaxGameplayLeaveLogs )
		{
			++s_gameplayLeaveLogs;
			PSU_LOG( "SceneCB LEAVE (gameplay#%u) cb=0x%08X uiDraw=%u dipPre=%u dipPost=%u global pre=%u post=%u out=%u\n",
				s_gameplayLeaveLogs, s_cbAddress, s_cbUiDraw, s_cbDipPre, s_cbDipPost,
				s_dipInPreCount, s_dipInPostCount, s_dipOutCount );
		}
		else if( dipTotal > 0 && !s_dipCaptureDone )
		{
			PSU_LOG( "SceneCB LEAVE (3D) cb=0x%08X uiDraw=%u dipPre=%u dipPost=%u global pre=%u post=%u out=%u\n",
				s_cbAddress, s_cbUiDraw, s_cbDipPre, s_cbDipPost,
				s_dipInPreCount, s_dipInPostCount, s_dipOutCount );
		}

		MaybeFinishDipCapture();
		s_inSceneCallback = false;
	}

	void PassEnter( unsigned passAddress )
	{
		if( !s_gameplay )
		{
			return;
		}
		PassStats *p = FindPass( passAddress );
		if( !p )
		{
			return;
		}
		s_curPass = p;
		s_pDip = s_pDp = s_pDpUp = s_pUi = 0;
		s_pView = s_pProj = s_pWorld = s_pVsc = 0;
		s_pFirstUiAfterDip = 0;
		if( s_camTraceOn )
		{
			PSU_LOG( "CamTrace[%u] enter pass 0x%08X\n", s_camTraceFrame, passAddress );
		}
	}

	unsigned CurrentPassAddress()
	{
		return s_curPass ? s_curPass->address : 0;
	}

	void PassLeave()
	{
		PassStats *p = s_curPass;
		if( !p )
		{
			return;
		}
		s_curPass = nullptr;
		if( s_camTraceOn )
		{
			PSU_LOG( "CamTrace[%u] leave pass 0x%08X dip=%u\n", s_camTraceFrame, p->address, s_pDip );
		}
		if( p->logged >= kMaxPassLogs )
		{
			return;
		}
		++p->logged;
		PSU_LOG( "Pass 0x%08X #%u cb=0x%08X dip=%u dp=%u dpUP=%u uiDraw=%u (uiAfterDip=%u) setView=%u setProj=%u setWorld=%u vsConst=%u\n",
			p->address, p->logged, s_inSceneCallback ? s_cbAddress : 0,
			s_pDip, s_pDp, s_pDpUp, s_pUi, s_pFirstUiAfterDip,
			s_pView, s_pProj, s_pWorld, s_pVsc );
		if( p->address == 0x004FD990 && p->logged == kMaxPassLogs && s_pDip > 0 && !s_camTraceArmed )
		{
			s_camTraceArmed = true;
			PSU_LOG( "CamTrace: armed for %u frames from next Present\n", kCamTraceFrames );
		}
	}

	static void PassNoteTransform( D3DTRANSFORMSTATETYPE state, void *returnAddress )
	{
		PassStats *p = s_curPass;
		if( !p )
		{
			return;
		}
		if( state == D3DTS_VIEW ) ++s_pView;
		else if( state == D3DTS_PROJECTION ) ++s_pProj;
		else if( state == D3DTS_WORLD ) ++s_pWorld;
		else return;

		if( state == D3DTS_WORLD )
		{
			return;
		}
		for( unsigned i = 0; i < p->transformRetCount; ++i )
		{
			if( p->transformRets[ i ] == returnAddress )
			{
				return;
			}
		}
		if( p->transformRetCount < 8 )
		{
			p->transformRets[ p->transformRetCount++ ] = returnAddress;
			PSU_LOG( "Pass 0x%08X SetTransform %s ret=%p\n", p->address,
				state == D3DTS_VIEW ? "VIEW" : "PROJ", returnAddress );
		}
	}

	void OnDrawPrimitive( bool userPointer )
	{
		if( !s_curPass )
		{
			return;
		}
		if( userPointer ) ++s_pDpUp;
		else ++s_pDp;
	}

	void OnVertexShaderConstantF( unsigned startRegister, unsigned vectorCount, void *returnAddress )
	{
		PassStats *p = s_curPass;
		if( !p )
		{
			return;
		}
		++s_pVsc;
		const unsigned key = ( startRegister << 16 ) | ( vectorCount & 0xFFFF );
		for( unsigned i = 0; i < p->vscKeyCount; ++i )
		{
			if( p->vscKeys[ i ] == key )
			{
				return;
			}
		}
		if( p->vscKeyCount < 12 )
		{
			p->vscKeys[ p->vscKeyCount++ ] = key;
			PSU_LOG( "Pass 0x%08X VSConstF start=c%u count=%u ret=%p\n",
				p->address, startRegister, vectorCount, returnAddress );
		}
	}

	void OnBeginScene( void *returnAddress )
	{
		if( !Active() )
		{
			return;
		}
		++s_beginScene;
		if( s_beginScene <= kMaxEventsPerKind )
		{
			PSU_LOG( "FrameTrace[%u] BeginScene ret=%p\n", s_frame, returnAddress );
		}
	}

	void OnEndScene( void *returnAddress )
	{
		if( !Active() )
		{
			return;
		}
		++s_endScene;
		if( s_endScene <= kMaxEventsPerKind )
		{
			PSU_LOG( "FrameTrace[%u] EndScene ret=%p\n", s_frame, returnAddress );
		}
	}

	void OnSetRenderTarget( IDirect3DDevice9 *device, DWORD index, IDirect3DSurface9 *rt, void *returnAddress )
	{
		if( !Active() || !device )
		{
			return;
		}
		++s_setRT;
		if( s_setRT > kMaxEventsPerKind )
		{
			return;
		}

		bool isBB = false;
		UINT w = 0, h = 0;
		D3DFORMAT fmt = D3DFMT_UNKNOWN;
		if( rt )
		{
			D3DSURFACE_DESC desc{};
			if( SUCCEEDED( rt->GetDesc( &desc ) ) )
			{
				w = desc.Width;
				h = desc.Height;
				fmt = desc.Format;
			}
			IDirect3DSurface9 *bb = nullptr;
			if( SUCCEEDED( device->GetBackBuffer( 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb ) ) && bb )
			{
				isBB = ( bb == rt );
				bb->Release();
			}
		}

		PSU_LOG( "FrameTrace[%u] SetRT idx=%u %s %ux%u fmt=%u ret=%p\n",
			s_frame, ( unsigned )index, isBB ? "BB" : "OFFSCREEN",
			w, h, ( unsigned )fmt, returnAddress );
	}

	static const char *ModuleOf( void *address, char *buffer, unsigned size )
	{
		HMODULE module = nullptr;
		if( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast< LPCSTR >( address ), &module ) || !GetModuleFileNameA( module, buffer, size ) )
		{
			return "?";
		}
		const char *slash = strrchr( buffer, '\\' );
		return slash ? slash + 1 : buffer;
	}

	static void LogMatrix( const char *tag, const D3DMATRIX *m )
	{
		if( !m )
		{
			PSU_LOG( "  %s (null)\n", tag );
			return;
		}
		PSU_LOG( "  %s [% .3f % .3f % .3f % .3f | % .3f % .3f % .3f % .3f | % .3f % .3f % .3f % .3f | % .3f % .3f % .3f % .3f]\n",
			tag,
			m->_11, m->_12, m->_13, m->_14, m->_21, m->_22, m->_23, m->_24,
			m->_31, m->_32, m->_33, m->_34, m->_41, m->_42, m->_43, m->_44 );
	}

	static void CamTraceTransform( D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix, void *returnAddress )
	{
		if( !s_camTraceOn )
		{
			return;
		}
		const char *name = nullptr;
		if( state == D3DTS_VIEW ) name = "VIEW";
		else if( state == D3DTS_PROJECTION ) name = "PROJ";
		else if( state == D3DTS_WORLD && s_curPass && s_curPass->address == 0x004FD990 && s_camWorldLogged < 2 )
		{
			++s_camWorldLogged;
			name = "WORLD(passA)";
		}
		if( !name )
		{
			return;
		}
		char path[ MAX_PATH ];
		PSU_LOG( "CamTrace[%u] %s ret=%p (%s) inPass=0x%08X inCB=%u\n",
			s_camTraceFrame, name, returnAddress, ModuleOf( returnAddress, path, MAX_PATH ),
			s_curPass ? s_curPass->address : 0, s_inSceneCallback ? 1u : 0u );
		LogMatrix( name, matrix );
	}

	void OnSetTransform( D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix, void *returnAddress )
	{
		CamTraceTransform( state, matrix, returnAddress );
		PassNoteTransform( state, returnAddress );
		if( !Active() )
		{
			return;
		}
		if( state == D3DTS_VIEW )
		{
			++s_setView;
			if( s_setView <= kMaxEventsPerKind )
			{
				PSU_LOG( "FrameTrace[%u] SetTransform VIEW ret=%p\n", s_frame, returnAddress );
			}
		}
		else if( state == D3DTS_PROJECTION )
		{
			++s_setProj;
			if( s_setProj <= kMaxEventsPerKind )
			{
				PSU_LOG( "FrameTrace[%u] SetTransform PROJ ret=%p\n", s_frame, returnAddress );
			}
		}
	}

	void OnUiDraw( void *returnAddress )
	{
		if( s_curPass )
		{
			++s_pUi;
			if( s_pDip > 0 && s_pFirstUiAfterDip == 0 )
			{
				s_pFirstUiAfterDip = s_pDip;
			}
		}
		const bool presentTrace = Active();
		const bool enterLeave = EnterLeaveActive() && s_inSceneCallback;
		const bool dipArm = !s_dipCaptureDone && s_inSceneCallback;
		if( !presentTrace && !enterLeave && !dipArm )
		{
			return;
		}

		if( presentTrace )
		{
			++s_uiDraw;
			if( s_uiDraw <= kMaxEventsPerKind )
			{
				PSU_LOG( "FrameTrace[%u] UIDraw(0x793C40) ret=%p%s\n",
					s_frame, returnAddress, s_inSceneCallback ? " IN_SceneCB" : "" );
			}
		}

		if( !s_inSceneCallback )
		{
			return;
		}

		++s_cbUiDraw;
		if( !s_loggedFirstUiInside )
		{
			s_loggedFirstUiInside = true;
			if( enterLeave || ( dipArm && ( s_cbDipPre > 0 || s_gameplay ) ) )
			{
				PSU_LOG( "SceneCB FIRST UIDraw ret=%p preUiDip=%u\n", returnAddress, s_cbDipPre );
			}
		}
		else if( enterLeave && s_cbUiDraw <= 4 )
		{
			PSU_LOG( "SceneCB[%u] UIDraw ret=%p\n", s_enterLeaveFrame, returnAddress );
		}
	}

	void OnDrawIndexed( void *returnAddress )
	{
		if( s_curPass )
		{
			++s_pDip;
		}
		if( s_dipCaptureDone )
		{
			return;
		}

		if( s_inSceneCallback )
		{
			if( !s_loggedFirstUiInside )
			{
				++s_cbDipPre;
				if( AddUnique( s_dipInPre, s_dipInPreCount, returnAddress ) )
				{
					PSU_LOG( "SceneCB DIP(pre-UI) ret=%p (%u/%u)\n",
						returnAddress, s_dipInPreCount, kMaxDipSamples );
				}
			}
			else
			{
				++s_cbDipPost;
				if( AddUnique( s_dipInPost, s_dipInPostCount, returnAddress ) )
				{
					PSU_LOG( "SceneCB DIP(post-UI) ret=%p (%u/%u)\n",
						returnAddress, s_dipInPostCount, kMaxDipSamples );
				}
			}
		}
		else if( s_gameplay )
		{
			if( AddUnique( s_dipOut, s_dipOutCount, returnAddress ) )
			{
				PSU_LOG( "SceneCB DIP(out) ret=%p (%u/%u)\n",
					returnAddress, s_dipOutCount, kMaxDipSamples );
			}
		}

		MaybeFinishDipCapture();
	}

	void OnPresent()
	{
		if( s_camTraceOn )
		{
			PSU_LOG( "CamTrace[%u] Present\n", s_camTraceFrame );
			s_camWorldLogged = 0;
			if( ++s_camTraceFrame >= kCamTraceFrames )
			{
				s_camTraceOn = false;
				PSU_LOG( "CamTrace: done\n" );
			}
		}
		else if( s_camTraceArmed && s_camTraceFrame == 0 )
		{
			s_camTraceOn = true;
			PSU_LOG( "CamTrace: start\n" );
		}

		if( s_done )
		{
			return;
		}

		if( !s_started )
		{
			s_started = true;
			s_frame = 0;
			ResetCounters();
			PSU_LOG( "FrameTrace: start (will log %u frames); DIP capture armed (in/out after gameplay)\n",
				kMaxFrames );
			return;
		}

		PSU_LOG( "FrameTrace[%u] summary Begin=%u End=%u SetRT=%u View=%u Proj=%u UIDraw=%u Present\n",
			s_frame, s_beginScene, s_endScene, s_setRT, s_setView, s_setProj, s_uiDraw );

		++s_frame;
		ResetCounters();
		if( s_frame >= kMaxFrames )
		{
			s_done = true;
			PSU_LOG( "FrameTrace: done (%u frames) — disable Present breadcrumbs\n", kMaxFrames );
		}
	}
}
