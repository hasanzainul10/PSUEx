/*
* DirectInput init stub + UI draw (0x793C40) for FrameTrace / VR HUD capture.
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUOverlay/FrameTrace.h"
#include "../PSUOverlay/PSUOverlay.h"
#include "../PSUVR/VRHud.h"
#include "../PSUExConfig.h"
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

	int __fastcall Hook_sub_793C40( void *thisPtr, void * /*dummy*/, int a4, int vertexCount, void *vertexPtr, int drawType )
	{
		FrameTrace::OnUiDraw( _ReturnAddress() );
		auto &hud = PSUVR::VRHud::Get();
		// Pass B DP-UPs inside this call are captured; only replay the function if none fired.
		hud.BeginUiDraw();
		const int result = Original_sub_793C40( thisPtr, a4, vertexCount, vertexPtr, drawType );
		if( Config::Get().vr.m_enabled && Config::Get().vr.m_stereo && hud.UiPassBCaptures() == 0 )
		{
			hud.CaptureUiDraw( PSUOverlay::Device(),
				thisPtr, a4, vertexCount, vertexPtr, drawType, Original_sub_793C40 );
		}
		hud.EndUiDraw();
		return result;
	}

	void Hook()
	{
		//DetourAttach( &( PVOID & )pOriginalFunction, reinterpret_cast< PVOID >( Hook_774F90 ) );
		DetourAttach( &( PVOID & )Original_sub_793C40, reinterpret_cast< PVOID >( Hook_sub_793C40 ) );
	}
}
