/*
* This hook is responsible for grabbing the D3D9 interface and initializing the overlay.
* We grab it just as the game creates it.
*/

#pragma once

#include <intrin.h>
#include "../PSUEx.h"
#include "../Misc/Log.h"
#include "../PSUVR/VRSystem.h"

namespace sub_776590
{
	typedef char( __cdecl *FunctionInitializeDirect3D )( int a1 );
    FunctionInitializeDirect3D m_pOriginalInitializeDirect3D = reinterpret_cast< FunctionInitializeDirect3D >( 0x00776590 );

	char __cdecl InitializeDirect3D( int a1 )
	{
		PSU_LOG( "PSU_InitializeDirect3D @ 0x00776590 hit (a1=%p)\n", ( void * )( uintptr_t )a1 );

		char result = m_pOriginalInitializeDirect3D( a1 );

		IDirect3D9 *g_pD3D = *( IDirect3D9 ** )0x00A888C0;

		if( g_pD3D )
		{
			PSU_LOG( "g_pD3D @ 0x00A888C0 = %p\n", g_pD3D );
			PSUOverlay::Get().Initialize( g_pD3D );
			PSUVR::System::Get().OnDirect3DCreated( g_pD3D );
		}
		else
		{
			PSU_LOG( "Failed to retrieve g_pD3D.\n" );
		}

		return result;
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )m_pOriginalInitializeDirect3D, reinterpret_cast< PVOID >( InitializeDirect3D ) );
	}
}