/*
* Model part cull test (called from the part draw 0x0049A760; part in EAX): with culling on
* (0x00B9DB88 bit 0) a part is tested against the game camera's volume (0x00B911A4 or
* 0x00B911A8) and skipped when outside. Free aim puts the game camera in the player's head,
* so the body and arms are culled; VR hands keep them while the local player is drawn in
* free aim (VRPlayerDraw::IgnorePartCull). Nonzero AL = culled.
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUVR/VRPlayerDraw.h"

namespace sub_562070
{
	PVOID pOriginal = reinterpret_cast< PVOID >( 0x00562070 );

	bool __cdecl ForcingVisible()
	{
		return PSUVR::VRPlayerDraw::IgnorePartCull();
	}

	// EAX carries the part: keep it for the original.
	__declspec( naked ) void Hook_562070()
	{
		__asm
		{
			push eax
			call ForcingVisible
			test al, al
			pop eax
			jnz visible
			jmp dword ptr [ pOriginal ]
		visible:
			xor al, al
			ret
		}
	}

	void Hook()
	{
		DetourAttach( &pOriginal, reinterpret_cast< PVOID >( Hook_562070 ) );
	}
}
