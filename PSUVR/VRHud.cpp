#include "VRHud.h"

#include "D3D11Bridge.h"
#include "StereoRenderer.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

#include <algorithm>
#include <cstring>

namespace PSUVR
{
	namespace
	{
		unsigned FvfStride( DWORD fvf )
		{
			unsigned size = 0;
			switch( fvf & D3DFVF_POSITION_MASK )
			{
				case D3DFVF_XYZ: size += 12; break;
				case D3DFVF_XYZRHW: size += 16; break;
				case D3DFVF_XYZB1: size += 16; break;
				case D3DFVF_XYZB2: size += 20; break;
				case D3DFVF_XYZB3: size += 24; break;
				case D3DFVF_XYZB4: size += 28; break;
				case D3DFVF_XYZB5: size += 32; break;
				default: size += 16; break;
			}
			if( fvf & D3DFVF_NORMAL ) size += 12;
			if( fvf & D3DFVF_PSIZE ) size += 4;
			if( fvf & D3DFVF_DIFFUSE ) size += 4;
			if( fvf & D3DFVF_SPECULAR ) size += 4;
			const unsigned texCount = ( fvf & D3DFVF_TEXCOUNT_MASK ) >> D3DFVF_TEXCOUNT_SHIFT;
			size += texCount * 8;
			return size ? size : 32;
		}
	}

	VRHud &VRHud::Get()
	{
		static VRHud instance;
		return instance;
	}

	bool VRHud::EnsureTargets( IDirect3DDevice9 *device, unsigned w, unsigned h )
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge || !device )
		{
			return false;
		}
		if( !bridge->EnsureHudTargets( device, w, h ) )
		{
			return false;
		}
		m_w = w;
		m_h = h;
		return true;
	}

	void VRHud::BeginStereoFrame( IDirect3DDevice9 *device, unsigned gameW, unsigned gameH )
	{
		m_active = false;
		m_hasUser = false;
		m_hasWorld = false;
		if( !device || gameW < 1 || gameH < 1 || !Config::Get().vr.m_enabled || !Config::Get().vr.m_stereo )
		{
			return;
		}
		if( !EnsureTargets( device, gameW, gameH ) )
		{
			return;
		}
		D3D11Bridge *bridge = System::Get().StereoBridge();
		IDirect3DSurface9 *user = bridge->HudUserSurface();
		IDirect3DSurface9 *world = bridge->HudWorldSurface();
		if( !user || !world )
		{
			return;
		}

		IDirect3DSurface9 *savedRt = nullptr;
		IDirect3DSurface9 *savedDs = nullptr;
		device->GetRenderTarget( 0, &savedRt );
		device->GetDepthStencilSurface( &savedDs );

		device->SetDepthStencilSurface( nullptr );
		device->SetRenderTarget( 0, user );
		device->Clear( 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB( 0, 0, 0, 0 ), 1.0f, 0 );
		device->SetRenderTarget( 0, world );
		device->Clear( 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB( 0, 0, 0, 0 ), 1.0f, 0 );

		device->SetRenderTarget( 0, savedRt );
		device->SetDepthStencilSurface( savedDs );
		if( savedRt )
		{
			savedRt->Release();
		}
		if( savedDs )
		{
			savedDs->Release();
		}

		m_active = true;
		if( !m_logged )
		{
			m_logged = true;
			PSU_LOG( "VR: HUD quad capture active (%ux%u user+world)\n", gameW, gameH );
		}
	}

	ID3D11Texture2D *VRHud::UserTexture() const
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		return bridge ? bridge->HudUserTexture() : nullptr;
	}

	ID3D11Texture2D *VRHud::WorldTexture() const
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		return bridge ? bridge->HudWorldTexture() : nullptr;
	}

	bool VRHud::IsWorldUiWidget( const void *verts, int vertexCount, unsigned stride,
		unsigned gameW, unsigned gameH )
	{
		if( !verts || vertexCount < 3 || stride < 16 || gameW < 1 || gameH < 1 )
		{
			return false;
		}
		float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
		const auto *bytes = static_cast< const unsigned char * >( verts );
		const unsigned count = static_cast< unsigned >( vertexCount );
		for( unsigned i = 0; i < count; ++i )
		{
			const float *xy = reinterpret_cast< const float * >( bytes + i * stride );
			minX = ( std::min )( minX, xy[ 0 ] );
			maxX = ( std::max )( maxX, xy[ 0 ] );
			minY = ( std::min )( minY, xy[ 1 ] );
			maxY = ( std::max )( maxY, xy[ 1 ] );
		}
		const float gw = static_cast< float >( gameW );
		const float gh = static_cast< float >( gameH );
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		const float areaFrac = ( boxW * boxH ) / ( gw * gh );
		if( areaFrac >= 0.35f )
		{
			return true;
		}
		const float cx = ( minX + maxX ) * 0.5f;
		const float cy = ( minY + maxY ) * 0.5f;
		const bool centered = ( cx > gw * 0.25f && cx < gw * 0.75f && cy > gh * 0.25f && cy < gh * 0.75f );
		return centered && areaFrac >= 0.15f;
	}

	void VRHud::ReplayOnto( IDirect3DDevice9 *device, bool worldLayer,
		void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType,
		int( __thiscall *drawFn )( void *, int, int, void *, int ) )
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		IDirect3DSurface9 *target = worldLayer ? bridge->HudWorldSurface() : bridge->HudUserSurface();
		if( !target || !drawFn )
		{
			return;
		}

		IDirect3DSurface9 *savedRt = nullptr;
		IDirect3DSurface9 *savedDs = nullptr;
		D3DVIEWPORT9 savedVp{};
		RECT savedScissor{};
		DWORD zEnable = 0, zWrite = 0, scissorEnable = 0, fvf = 0;
		device->GetRenderTarget( 0, &savedRt );
		device->GetDepthStencilSurface( &savedDs );
		device->GetViewport( &savedVp );
		device->GetScissorRect( &savedScissor );
		device->GetRenderState( D3DRS_ZENABLE, &zEnable );
		device->GetRenderState( D3DRS_ZWRITEENABLE, &zWrite );
		device->GetRenderState( D3DRS_SCISSORTESTENABLE, &scissorEnable );
		device->GetFVF( &fvf );

		m_capturing = true;
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		device->SetRenderTarget( 0, target );
		device->SetDepthStencilSurface( nullptr );
		D3DVIEWPORT9 vp{ 0, 0, m_w, m_h, 0.0f, 1.0f };
		device->SetViewport( &vp );
		RECT scissor{ 0, 0, static_cast< LONG >( m_w ), static_cast< LONG >( m_h ) };
		device->SetScissorRect( &scissor );
		drawFn( thisPtr, a4, vertexCount, vertexPtr, drawType );
		m_capturing = false;

		device->SetRenderTarget( 0, savedRt );
		device->SetDepthStencilSurface( savedDs );
		device->SetViewport( &savedVp );
		device->SetScissorRect( &savedScissor );
		device->SetRenderState( D3DRS_SCISSORTESTENABLE, scissorEnable );
		device->SetRenderState( D3DRS_ZENABLE, zEnable );
		device->SetRenderState( D3DRS_ZWRITEENABLE, zWrite );
		device->SetFVF( fvf );
		if( savedRt )
		{
			savedRt->Release();
		}
		if( savedDs )
		{
			savedDs->Release();
		}

		if( worldLayer )
		{
			m_hasWorld = true;
		}
		else
		{
			m_hasUser = true;
		}
	}

	void VRHud::ReplayPassBOntoWorld( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride )
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		IDirect3DSurface9 *target = bridge ? bridge->HudWorldSurface() : nullptr;
		if( !target || !verts )
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

		m_capturing = true;
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		device->SetRenderTarget( 0, target );
		device->SetDepthStencilSurface( nullptr );
		D3DVIEWPORT9 vp{ 0, 0, m_w, m_h, 0.0f, 1.0f };
		device->SetViewport( &vp );
		RECT scissor{ 0, 0, static_cast< LONG >( m_w ), static_cast< LONG >( m_h ) };
		device->SetScissorRect( &scissor );
		device->DrawPrimitiveUP( static_cast< D3DPRIMITIVETYPE >( primitiveType ),
			primitiveCount, verts, stride );
		m_capturing = false;

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
		m_hasWorld = true;
	}

	void VRHud::CapturePassBDraw( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride )
	{
		if( m_capturing || !m_active || !device || !verts || StereoRenderer::InEyePass() )
		{
			return;
		}
		if( !StereoRenderer::Get().HasFrame() )
		{
			return;
		}
		ReplayPassBOntoWorld( device, primitiveType, primitiveCount, verts, stride );
	}

	void VRHud::CaptureUiDraw( IDirect3DDevice9 *device,
		void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType,
		int( __thiscall *drawFn )( void *, int, int, void *, int ) )
	{
		if( m_capturing || !m_active || !device || !drawFn || StereoRenderer::InEyePass() )
		{
			return;
		}
		if( !StereoRenderer::Get().HasFrame() )
		{
			return;
		}

		DWORD fvf = 0;
		device->GetFVF( &fvf );
		unsigned stride = FvfStride( fvf );
		const bool world = IsWorldUiWidget( vertexPtr, vertexCount, stride, m_w, m_h );
		ReplayOnto( device, world, thisPtr, a4, vertexCount, vertexPtr, drawType, drawFn );
	}
}
