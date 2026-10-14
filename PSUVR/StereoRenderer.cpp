#include "StereoRenderer.h"

#include "D3D11Bridge.h"
#include "VRCamera.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

#include <unordered_set>

namespace PSUVR
{
	// Game units per meter: the follow camera sits 40-70 units behind the player
	// and focuses 15 units up, which reads as ~4-7 m and ~1.5 m.
	constexpr float kUnitsPerMeter = 10.0f;

	namespace
	{
		bool s_inEyePass = false;
		std::unordered_set< IDirect3DBaseTexture9 * > s_writtenTextures;
		unsigned s_screenQuadLogCount = 0;
		unsigned s_stretchLogCount = 0;
		constexpr unsigned kMaxScreenQuadLogs = 24;
		constexpr unsigned kMaxStretchLogs = 16;

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

	void StereoRenderer::BeginEyePass( IDirect3DSurface9 *eyeTarget )
	{
		s_writtenTextures.clear();
		MarkSurfaceTexture( eyeTarget );
		s_inEyePass = true;
	}

	void StereoRenderer::EndEyePass()
	{
		s_inEyePass = false;
		s_writtenTextures.clear();
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

	void StereoRenderer::RenderEyes( IDirect3DDevice9 *device, Pass pass )
	{
		const auto &vr = Config::Get().vr;
		if( !device || !pass || !vr.m_enabled || !vr.m_stereo )
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

				BeginEyePass( eyeSurface );
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
