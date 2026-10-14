#include "StereoRenderer.h"

#include "D3D11Bridge.h"
#include "VRCamera.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

namespace PSUVR
{
	// Game units per meter: the follow camera sits 40-70 units behind the player
	// and focuses 15 units up, which reads as ~4-7 m and ~1.5 m.
	constexpr float kUnitsPerMeter = 10.0f;

	static bool HasStencil( D3DFORMAT format )
	{
		return format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4 || format == D3DFMT_D15S1 || format == D3DFMT_D24FS8;
	}

	StereoRenderer &StereoRenderer::Get()
	{
		static StereoRenderer instance;
		return instance;
	}

	void StereoRenderer::RenderEyes( IDirect3DDevice9 *device, Pass pass )
	{
		const auto &vr = Config::Get().vr;
		if( !device || !pass || !vr.m_enabled || !vr.m_stereo )
		{
			return;
		}

		EyePose views[ 2 ];
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge || !System::Get().NextViews( views ) )
		{
			return;
		}
		if( !VRCamera::HasReference() )
		{
			VRCamera::Recenter( views );
		}

		// Eye targets match the game's 3D target so screen-space draws inside the
		// pass (shadow composite, pre-transformed quads) still cover the image.
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

		if( bridge->EnsureEyeTargets( device, targetDesc.Width, targetDesc.Height, depthFormat ) )
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

				device->SetRenderTarget( 0, bridge->EyeSurface( eye ) );
				device->SetDepthStencilSurface( bridge->EyeDepth() );
				D3DVIEWPORT9 eyeViewport{ 0, 0, targetDesc.Width, targetDesc.Height, 0.0f, 1.0f };
				device->SetViewport( &eyeViewport );
				device->Clear( 0, nullptr, clearFlags, D3DCOLOR_XRGB( 0, 0, 0 ), 1.0f, 0 );
				device->SetTransform( D3DTS_VIEW, &eyeView );
				device->SetTransform( D3DTS_PROJECTION, &eyeProj );
				pass();
				m_poses[ eye ] = views[ eye ];
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
				PSU_LOG( "Stereo: first eye pair %ux%u, %.1f units/m, fovL=[%.2f %.2f %.2f %.2f]\n",
					targetDesc.Width, targetDesc.Height, unitsPerMeter,
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
