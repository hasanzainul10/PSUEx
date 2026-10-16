/*
* Recursive skeleton walk behind every skinned pose build (0x0074D1D0 and the per-frame
* animation roots): for one bone and its children it writes skinning matrices (inverse
* bind x bone, model space) into the output buffer by palette slot. Inputs live in
* globals: 0x00A85BC0 output (a model's +0x64 buffer), 0x00A85BA0 bone records (0x90 bytes
* each, skeleton +0x30). When the outermost call returns the whole pose is written, and
* VR hands edit the local player's arm matrices then.
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUVR/VRPlayerDraw.h"

namespace sub_74CFD0
{
	typedef void( __cdecl *function_t )( int bone );
	function_t pOriginal = reinterpret_cast< function_t >( 0x0074CFD0 );

	constexpr uintptr_t kPoseOutput = 0x00A85BC0;
	constexpr uintptr_t kPoseRecords = 0x00A85BA0;

	unsigned s_depth = 0;

	void __cdecl Hook_74CFD0( int bone )
	{
		++s_depth;
		pOriginal( bone );
		if( --s_depth == 0 )
		{
			PSUVR::VRPlayerDraw::OnPoseBuilt( *reinterpret_cast< float ** >( kPoseOutput ),
				*reinterpret_cast< const uint8_t ** >( kPoseRecords ) );
		}
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )pOriginal, reinterpret_cast< PVOID >( Hook_74CFD0 ) );
	}
}
