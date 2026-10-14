/*
* DirectInput init stub + UI draw (0x793C40) for FrameTrace / stereo HUD replay.
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUOverlay/FrameTrace.h"
#include "../PSUOverlay/PSUOverlay.h"
#include "../PSUVR/StereoRenderer.h"
#include "../PSUVR/VRSystem.h"
#include "../PSUVR/D3D11Bridge.h"
#include "../PSUExConfig.h"
#include "../Misc/Log.h"
#include <intrin.h>

namespace sub_774F90
{
	typedef HRESULT( __cdecl *functionType )();
	functionType pOriginalFunction = reinterpret_cast< functionType >( 0x00774F90 );

	HRESULT Hook_774F90()
	{
		printf( "Hooked 00774F90\n" );
		return pOriginalFunction();
	}

	// Game UI draw — thiscall with four stack args after this.
	typedef int( __thiscall *sub_793C40_t )( void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType );
	sub_793C40_t Original_sub_793C40 = reinterpret_cast< sub_793C40_t >( 0x00793C40 );

	static bool s_replayingHud = false;
	static bool s_loggedHud = false;

	static void ReplayUiOntoEyes( void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType )
	{
		auto &stereo = PSUVR::StereoRenderer::Get();
		if( !stereo.HasFrame() || stereo.EyeWidth() < 1 || stereo.EyeHeight() < 1 )
		{
			return;
		}
		PSUVR::D3D11Bridge *bridge = PSUVR::System::Get().StereoBridge();
		IDirect3DDevice9 *device = PSUOverlay::Device();
		if( !bridge || !device )
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

		s_replayingHud = true;
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		for( int eye = 0; eye < 2; ++eye )
		{
			IDirect3DSurface9 *eyeSurf = bridge->EyeSurface( eye );
			if( !eyeSurf )
			{
				continue;
			}
			PSUVR::StereoRenderer::BeginHudOverlay( eyeSurf, stereo.EyeWidth(), stereo.EyeHeight(),
				stereo.GameWidth(), stereo.GameHeight() );
			device->SetRenderTarget( 0, eyeSurf );
			device->SetDepthStencilSurface( nullptr );
			D3DVIEWPORT9 eyeVp{};
			PSUVR::StereoRenderer::GetEyeViewport( eyeVp );
			device->SetViewport( &eyeVp );
			RECT eyeScissor{};
			PSUVR::StereoRenderer::GetEyeScissor( eyeScissor );
			device->SetScissorRect( &eyeScissor );
			Original_sub_793C40( thisPtr, a4, vertexCount, vertexPtr, drawType );
			PSUVR::StereoRenderer::EndHudOverlay();
		}
		s_replayingHud = false;

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

		if( !s_loggedHud )
		{
			s_loggedHud = true;
			PSU_LOG( "VR: HUD replay onto stereo eyes active (0x793C40)\n" );
		}
	}

	int __fastcall Hook_sub_793C40( void *thisPtr, void * /*dummy*/, int a4, int vertexCount, void *vertexPtr, int drawType )
	{
		FrameTrace::OnUiDraw( _ReturnAddress() );
		const int result = Original_sub_793C40( thisPtr, a4, vertexCount, vertexPtr, drawType );
		if( !s_replayingHud && Config::Get().vr.m_enabled && Config::Get().vr.m_stereo )
		{
			ReplayUiOntoEyes( thisPtr, a4, vertexCount, vertexPtr, drawType );
		}
		return result;
	}

	void Hook()
	{
		//DetourAttach( &( PVOID & )pOriginalFunction, reinterpret_cast< PVOID >( Hook_774F90 ) );
		DetourAttach( &( PVOID & )Original_sub_793C40, reinterpret_cast< PVOID >( Hook_sub_793C40 ) );
	}
}
