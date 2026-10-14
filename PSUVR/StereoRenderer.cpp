#include "StereoRenderer.h"

#include "D3D11Bridge.h"
#include "VRCamera.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace PSUVR
{
	// Game units per meter: the follow camera sits 40-70 units behind the player
	// and focuses 15 units up, which reads as ~4-7 m and ~1.5 m.
	constexpr float kUnitsPerMeter = 10.0f;

	namespace
	{
		bool s_inEyePass = false;
		bool s_inHudOverlay = false;
		IDirect3DSurface9 *s_eyeSurface = nullptr;
		unsigned s_eyeW = 0;
		unsigned s_eyeH = 0;
		unsigned s_gameW = 0;
		unsigned s_gameH = 0;
		unsigned s_stretchQuadLogCount = 0;
		alignas( 16 ) unsigned char s_stretchQuadBuf[ 64 * 64 ]{};
		std::unordered_set< IDirect3DBaseTexture9 * > s_writtenTextures;
		unsigned s_screenQuadLogCount = 0;
		unsigned s_stretchLogCount = 0;
		constexpr unsigned kMaxScreenQuadLogs = 24;
		constexpr unsigned kMaxStretchLogs = 16;
		constexpr unsigned kMaxStretchVerts = 64;
		constexpr unsigned kMaxStretchStride = 64;

		bool HasStencil( D3DFORMAT format )
		{
			return format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4 || format == D3DFMT_D15S1 || format == D3DFMT_D24FS8;
		}
	}

	StereoRenderer &StereoRenderer::Get()
	{
		static StereoRenderer instance;
		return instance;
	}

	bool StereoRenderer::InEyePass()
	{
		return s_inEyePass;
	}

	bool StereoRenderer::InHudOverlay()
	{
		return s_inHudOverlay;
	}

	void StereoRenderer::BeginHudOverlay( IDirect3DSurface9 *eyeTarget, unsigned eyeW, unsigned eyeH,
		unsigned gameW, unsigned gameH )
	{
		s_eyeSurface = eyeTarget;
		s_eyeW = eyeW;
		s_eyeH = eyeH;
		s_gameW = gameW;
		s_gameH = gameH;
		s_inHudOverlay = true;
	}

	void StereoRenderer::EndHudOverlay()
	{
		s_inHudOverlay = false;
		s_eyeSurface = nullptr;
		s_eyeW = 0;
		s_eyeH = 0;
		s_gameW = 0;
		s_gameH = 0;
	}

	void StereoRenderer::MarkSurfaceTexture( IDirect3DSurface9 *surface )
	{
		if( !surface )
		{
			return;
		}
		IDirect3DTexture9 *tex = nullptr;
		if( SUCCEEDED( surface->GetContainer( __uuidof( IDirect3DTexture9 ), reinterpret_cast< void ** >( &tex ) ) ) && tex )
		{
			s_writtenTextures.insert( tex );
			tex->Release();
		}
	}

	void StereoRenderer::BeginEyePass( IDirect3DSurface9 *eyeTarget, unsigned eyeW, unsigned eyeH,
		unsigned gameW, unsigned gameH )
	{
		s_writtenTextures.clear();
		s_eyeSurface = eyeTarget;
		s_eyeW = eyeW;
		s_eyeH = eyeH;
		s_gameW = gameW;
		s_gameH = gameH;
		MarkSurfaceTexture( eyeTarget );
		s_inEyePass = true;
	}

	void StereoRenderer::EndEyePass()
	{
		s_inEyePass = false;
		s_eyeSurface = nullptr;
		s_eyeW = 0;
		s_eyeH = 0;
		s_gameW = 0;
		s_gameH = 0;
		s_writtenTextures.clear();
	}

	bool StereoRenderer::EyeColorTargetIs( IDirect3DSurface9 *surface )
	{
		return ( s_inEyePass || s_inHudOverlay ) && surface && surface == s_eyeSurface && s_eyeW > 0 && s_eyeH > 0;
	}

	bool StereoRenderer::EyeColorTargetBound( IDirect3DDevice9 *device )
	{
		if( ( !s_inEyePass && !s_inHudOverlay ) || !device || !s_eyeSurface || s_eyeW == 0 || s_eyeH == 0 )
		{
			return false;
		}
		IDirect3DSurface9 *rt = nullptr;
		if( FAILED( device->GetRenderTarget( 0, &rt ) ) || !rt )
		{
			return false;
		}
		const bool match = ( rt == s_eyeSurface );
		rt->Release();
		return match;
	}

	void StereoRenderer::GetEyeViewport( D3DVIEWPORT9 &out )
	{
		out = { 0, 0, s_eyeW, s_eyeH, 0.0f, 1.0f };
	}

	void StereoRenderer::GetEyeScissor( RECT &out )
	{
		out.left = 0;
		out.top = 0;
		out.right = static_cast< LONG >( s_eyeW );
		out.bottom = static_cast< LONG >( s_eyeH );
	}

	void StereoRenderer::OnSetRenderTarget( IDirect3DSurface9 *target )
	{
		if( !s_inEyePass )
		{
			return;
		}
		MarkSurfaceTexture( target );
	}

	void StereoRenderer::OnStretchRect( IDirect3DSurface9 *source, IDirect3DSurface9 *dest, void *ret )
	{
		if( !source || !dest )
		{
			return;
		}

		if( s_inEyePass )
		{
			MarkSurfaceTexture( dest );
		}

		if( s_stretchLogCount >= kMaxStretchLogs )
		{
			return;
		}

		D3DSURFACE_DESC srcDesc{};
		D3DSURFACE_DESC dstDesc{};
		source->GetDesc( &srcDesc );
		dest->GetDesc( &dstDesc );
		const bool srcRT = ( srcDesc.Usage & D3DUSAGE_RENDERTARGET ) != 0;
		const bool dstRT = ( dstDesc.Usage & D3DUSAGE_RENDERTARGET ) != 0;
		if( !srcRT && !dstRT )
		{
			return;
		}

		++s_stretchLogCount;
		PSU_LOG( "StereoStretch: eye=%d src=%ux%u usage=0x%X dst=%ux%u usage=0x%X ret=%p\n",
			s_inEyePass ? 1 : 0,
			srcDesc.Width, srcDesc.Height, srcDesc.Usage,
			dstDesc.Width, dstDesc.Height, dstDesc.Usage,
			ret );
	}

	bool StereoRenderer::ShouldSkipScreenQuad( IDirect3DDevice9 *device, void *ret,
		unsigned primitiveCount, bool userPointer )
	{
		if( !s_inEyePass || !device || !Config::Get().vr.m_stereoSkipScreenQuads )
		{
			return false;
		}

		DWORD fvf = 0;
		if( FAILED( device->GetFVF( &fvf ) ) || ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
		{
			return false;
		}

		IDirect3DBaseTexture9 *stage0 = nullptr;
		if( FAILED( device->GetTexture( 0, &stage0 ) ) || !stage0 )
		{
			return false;
		}

		IDirect3DTexture9 *tex9 = nullptr;
		stage0->QueryInterface( __uuidof( IDirect3DTexture9 ), reinterpret_cast< void ** >( &tex9 ) );
		stage0->Release();
		if( !tex9 )
		{
			return false;
		}

		D3DSURFACE_DESC desc{};
		tex9->GetLevelDesc( 0, &desc );
		const bool isRT = ( desc.Usage & D3DUSAGE_RENDERTARGET ) != 0;
		const bool written = s_writtenTextures.find( tex9 ) != s_writtenTextures.end();
		const bool skip = isRT && !written;

		DWORD blendEnable = 0;
		device->GetRenderState( D3DRS_ALPHABLENDENABLE, &blendEnable );

		if( s_screenQuadLogCount < kMaxScreenQuadLogs )
		{
			++s_screenQuadLogCount;
			PSU_LOG( "StereoQuad: skip=%d up=%d prim=%u fvf=0x%X tex=%ux%u rt=%d written=%d blend=%u ret=%p\n",
				skip ? 1 : 0, userPointer ? 1 : 0, primitiveCount, fvf,
				desc.Width, desc.Height, isRT ? 1 : 0, written ? 1 : 0, blendEnable, ret );
		}

		tex9->Release();
		return skip;
	}

	const void *StereoRenderer::StretchScreenQuadIfNeeded( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride, void *ret )
	{
		if( !verts || stride < 16 || stride > kMaxStretchStride || !device )
		{
			return nullptr;
		}
		if( ( !s_inEyePass && !s_inHudOverlay ) || s_gameW < 1 || s_gameH < 1 || s_eyeW < 1 || s_eyeH < 1 )
		{
			return nullptr;
		}
		if( s_gameW == s_eyeW && s_gameH == s_eyeH )
		{
			return nullptr;
		}
		if( !EyeColorTargetBound( device ) )
		{
			return nullptr;
		}

		DWORD fvf = 0;
		if( FAILED( device->GetFVF( &fvf ) ) || ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
		{
			return nullptr;
		}

		unsigned vertCount = 0;
		switch( static_cast< D3DPRIMITIVETYPE >( primitiveType ) )
		{
			case D3DPT_TRIANGLELIST: vertCount = primitiveCount * 3; break;
			case D3DPT_TRIANGLESTRIP:
			case D3DPT_TRIANGLEFAN: vertCount = primitiveCount + 2; break;
			case D3DPT_LINELIST: vertCount = primitiveCount * 2; break;
			case D3DPT_LINESTRIP: vertCount = primitiveCount + 1; break;
			case D3DPT_POINTLIST: vertCount = primitiveCount; break;
			default: return nullptr;
		}
		if( vertCount < 3 || vertCount > kMaxStretchVerts )
		{
			return nullptr;
		}
		if( vertCount * stride > sizeof( s_stretchQuadBuf ) )
		{
			return nullptr;
		}

		float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
		const auto *bytes = static_cast< const unsigned char * >( verts );
		for( unsigned i = 0; i < vertCount; ++i )
		{
			const float *xy = reinterpret_cast< const float * >( bytes + i * stride );
			minX = ( std::min )( minX, xy[ 0 ] );
			maxX = ( std::max )( maxX, xy[ 0 ] );
			minY = ( std::min )( minY, xy[ 1 ] );
			maxY = ( std::max )( maxY, xy[ 1 ] );
		}
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		// Eye-pass tint: only near-fullscreen quads. HUD overlay: every XYZRHW widget.
		if( !s_inHudOverlay )
		{
			if( boxW < 0.8f * static_cast< float >( s_gameW ) || boxH < 0.8f * static_cast< float >( s_gameH ) )
			{
				return nullptr;
			}
		}

		const float sx = static_cast< float >( s_eyeW ) / static_cast< float >( s_gameW );
		const float sy = static_cast< float >( s_eyeH ) / static_cast< float >( s_gameH );
		std::memcpy( s_stretchQuadBuf, verts, vertCount * stride );
		for( unsigned i = 0; i < vertCount; ++i )
		{
			float *xy = reinterpret_cast< float * >( s_stretchQuadBuf + i * stride );
			xy[ 0 ] *= sx;
			xy[ 1 ] *= sy;
		}

		if( s_stretchQuadLogCount < 8 )
		{
			++s_stretchQuadLogCount;
			PSU_LOG( "StereoStretchQuad: hud=%d game=%ux%u eye=%ux%u verts=%u box=%.0fx%.0f ret=%p\n",
				s_inHudOverlay ? 1 : 0, s_gameW, s_gameH, s_eyeW, s_eyeH, vertCount, boxW, boxH, ret );
		}
		return s_stretchQuadBuf;
	}

	void StereoRenderer::MirrorPassBDrawToEyes( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride )
	{
		if( s_inHudOverlay || s_inEyePass || !device || !verts || !Get().HasFrame() )
		{
			return;
		}
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge )
		{
			return;
		}
		const unsigned eyeW = Get().EyeWidth();
		const unsigned eyeH = Get().EyeHeight();
		const unsigned gameW = Get().GameWidth();
		const unsigned gameH = Get().GameHeight();
		if( eyeW < 1 || eyeH < 1 )
		{
			return;
		}

		IDirect3DSurface9 *savedRt = nullptr;
		IDirect3DSurface9 *savedDs = nullptr;
		D3DVIEWPORT9 savedVp{};
		RECT savedScissor{};
		DWORD zEnable = 0, zWrite = 0, scissorEnable = 0;
		device->GetRenderTarget( 0, &savedRt );
		device->GetDepthStencilSurface( &savedDs );
		device->GetViewport( &savedVp );
		device->GetScissorRect( &savedScissor );
		device->GetRenderState( D3DRS_ZENABLE, &zEnable );
		device->GetRenderState( D3DRS_ZWRITEENABLE, &zWrite );
		device->GetRenderState( D3DRS_SCISSORTESTENABLE, &scissorEnable );

		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		for( int eye = 0; eye < 2; ++eye )
		{
			IDirect3DSurface9 *eyeSurf = bridge->EyeSurface( eye );
			if( !eyeSurf )
			{
				continue;
			}
			BeginHudOverlay( eyeSurf, eyeW, eyeH, gameW, gameH );
			device->SetRenderTarget( 0, eyeSurf );
			device->SetDepthStencilSurface( nullptr );
			D3DVIEWPORT9 eyeVp{};
			GetEyeViewport( eyeVp );
			device->SetViewport( &eyeVp );
			RECT eyeScissor{};
			GetEyeScissor( eyeScissor );
			device->SetScissorRect( &eyeScissor );
			device->DrawPrimitiveUP( static_cast< D3DPRIMITIVETYPE >( primitiveType ),
				primitiveCount, verts, stride );
			EndHudOverlay();
		}

		device->SetRenderTarget( 0, savedRt );
		device->SetDepthStencilSurface( savedDs );
		device->SetViewport( &savedVp );
		device->SetScissorRect( &savedScissor );
		device->SetRenderState( D3DRS_SCISSORTESTENABLE, scissorEnable );
		device->SetRenderState( D3DRS_ZENABLE, zEnable );
		device->SetRenderState( D3DRS_ZWRITEENABLE, zWrite );
		if( savedRt )
		{
			savedRt->Release();
		}
		if( savedDs )
		{
			savedDs->Release();
		}

		static bool s_logged = false;
		if( !s_logged )
		{
			s_logged = true;
			PSU_LOG( "VR: Pass B draws mirrored to stereo eyes (no extra Pass B)\n" );
		}
	}

	void StereoRenderer::RenderEyes( IDirect3DDevice9 *device, Pass pass )
	{
		const auto &vr = Config::Get().vr;
		if( !device || !pass || !vr.m_enabled || !vr.m_stereo )
		{
			return;
		}
		const unsigned sceneMode = *reinterpret_cast< unsigned * >( 0x00B90EA8 );
		if( vr.m_stereoMode != 0 && sceneMode != vr.m_stereoMode )
		{
			return;
		}

		EyePose views[ 2 ];
		EyePose viewSpace[ 2 ];
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge || !System::Get().NextViews( views, viewSpace ) )
		{
			return;
		}
		if( !VRCamera::HasReference() )
		{
			VRCamera::Recenter( views );
		}

		IDirect3DSurface9 *gameTarget = nullptr;
		IDirect3DSurface9 *gameDepth = nullptr;
		if( FAILED( device->GetRenderTarget( 0, &gameTarget ) ) || !gameTarget )
		{
			return;
		}
		device->GetDepthStencilSurface( &gameDepth );

		D3DSURFACE_DESC targetDesc{};
		gameTarget->GetDesc( &targetDesc );
		D3DFORMAT depthFormat = D3DFMT_D24S8;
		if( gameDepth )
		{
			D3DSURFACE_DESC depthDesc{};
			gameDepth->GetDesc( &depthDesc );
			depthFormat = depthDesc.Format;
		}

		// Prefer the OpenXR eye swapchain size so FOV maps 1:1 (stretching a 16:9
		// game RT into a near-square eye buffer warps stereo and looks like ghosts).
		UINT eyeW = targetDesc.Width;
		UINT eyeH = targetDesc.Height;
		uint32_t xrW = 0, xrH = 0;
		if( System::Get().EyeSwapchainSize( xrW, xrH ) )
		{
			eyeW = xrW;
			eyeH = xrH;
		}

		if( bridge->EnsureEyeTargets( device, eyeW, eyeH, depthFormat ) )
		{
			D3DVIEWPORT9 gameViewport{};
			D3DMATRIX gameView{};
			D3DMATRIX gameProj{};
			device->GetViewport( &gameViewport );
			device->GetTransform( D3DTS_VIEW, &gameView );
			device->GetTransform( D3DTS_PROJECTION, &gameProj );

			const float unitsPerMeter = kUnitsPerMeter * ( vr.m_worldScale > 0.01f ? vr.m_worldScale : 1.0f );
			const DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | ( HasStencil( depthFormat ) ? D3DCLEAR_STENCIL : 0 );

			for( int eye = 0; eye < 2; ++eye )
			{
				D3DMATRIX eyeView{};
				D3DMATRIX eyeProj{};
				VRCamera::BuildEye( views[ eye ], unitsPerMeter, gameProj, eyeView, eyeProj );

				IDirect3DSurface9 *eyeSurface = bridge->EyeSurface( eye );
				device->SetRenderTarget( 0, eyeSurface );
				device->SetDepthStencilSurface( bridge->EyeDepth() );
				D3DVIEWPORT9 eyeViewport{ 0, 0, eyeW, eyeH, 0.0f, 1.0f };
				device->SetViewport( &eyeViewport );
				device->Clear( 0, nullptr, clearFlags, D3DCOLOR_XRGB( 0, 0, 0 ), 1.0f, 0 );
				device->SetTransform( D3DTS_VIEW, &eyeView );
				device->SetTransform( D3DTS_PROJECTION, &eyeProj );

				BeginEyePass( eyeSurface, eyeW, eyeH, targetDesc.Width, targetDesc.Height );
				pass();
				EndEyePass();

				m_poses[ eye ] = views[ eye ];
				m_viewSpacePoses[ eye ] = viewSpace[ eye ];
			}

			device->SetRenderTarget( 0, gameTarget );
			device->SetDepthStencilSurface( gameDepth );
			device->SetViewport( &gameViewport );
			device->SetTransform( D3DTS_VIEW, &gameView );
			device->SetTransform( D3DTS_PROJECTION, &gameProj );
			m_lastEyeW = eyeW;
			m_lastEyeH = eyeH;
			m_lastGameW = targetDesc.Width;
			m_lastGameH = targetDesc.Height;
			m_hasFrame = true;

			if( !m_loggedFirst )
			{
				m_loggedFirst = true;
				PSU_LOG( "Stereo: first eye pair %ux%u (game RT %ux%u), %.1f units/m, skipScreenQuads=%d, fovL=[%.2f %.2f %.2f %.2f]\n",
					eyeW, eyeH, targetDesc.Width, targetDesc.Height, unitsPerMeter,
					vr.m_stereoSkipScreenQuads ? 1 : 0,
					views[ 0 ].fovAngleLeft, views[ 0 ].fovAngleRight, views[ 0 ].fovAngleUp, views[ 0 ].fovAngleDown );
			}
		}

		gameTarget->Release();
		if( gameDepth )
		{
			gameDepth->Release();
		}
	}
}
