/*
* Builds a model's per-frame bone matrices (model space, indexed by bone): for each bone
* record (skeleton +0x30, 0x90 bytes, parent at +6) it composes the parent's matrix with
* the bone's local translation / rotation / scale (localPose, 0x30 bytes per bone).
* Called from the model pose update 0x004FFB30 with out = model +0x64; the loaded player
* character (TDivModel) takes this path. VR hands edit the local player's arm bones here.
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUVR/VRPlayerDraw.h"

namespace sub_74B760
{
	typedef void( __cdecl *function_t )( float *out, void *skeleton, void *localPose, void *root );
	function_t pOriginal = reinterpret_cast< function_t >( 0x0074B760 );

	void __cdecl Hook_74B760( float *out, void *skeleton, void *localPose, void *root )
	{
		pOriginal( out, skeleton, localPose, root );
		PSUVR::VRPlayerDraw::OnBonePose( out, static_cast< const uint8_t * >( skeleton ) );
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )pOriginal, reinterpret_cast< PVOID >( Hook_74B760 ) );
	}
}
