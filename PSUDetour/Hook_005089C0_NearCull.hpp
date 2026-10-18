/*
* Render object visibility test (called from the render object draw 0x005526D0): an object
* closer than a threshold (+0x10 vs 0x00993368) runs its own check (vtable +0x0C) and may be
* hidden. Free aim puts the game camera in the player's head, which hides the player and
* weapons; VR hands keep them drawn while the local player is drawn in free aim.
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUVR/VRPlayerDraw.h"

namespace sub_5089C0
{
	typedef unsigned( __fastcall *function_t )( void *self );
	function_t pOriginal = reinterpret_cast< function_t >( 0x005089C0 );

	// Nonzero = hidden.
	unsigned __fastcall Hook_5089C0( void *self )
	{
		if( PSUVR::VRPlayerDraw::IgnoreNearHide() )
		{
			return 0;
		}
		return pOriginal( self );
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )pOriginal, reinterpret_cast< PVOID >( Hook_5089C0 ) );
	}
}
