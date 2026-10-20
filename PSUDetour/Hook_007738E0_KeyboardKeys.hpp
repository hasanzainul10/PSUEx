/*
* Keyboard poll: the game's per-frame list of held keys (game key codes, from GetKeyboardState
* through the VK table 0x00870280; up to 14). 0x007738E0 copies it into 0x00962D64 (count
* 0x00962D74); the pressed / released bit sets follow from it. VR speech to text adds the chat
* key and Enter here so the game's own chat box opens and sends (see PSUVR/VRSpeech.h).
*/

#pragma once

#include "../PSUEx.h"
#include "../PSUVR/VRSpeech.h"

namespace sub_7738E0
{
	typedef bool( __stdcall *function_t )( const uint8_t *keys, unsigned count );
	function_t pOriginal = reinterpret_cast< function_t >( 0x007738E0 );

	bool __stdcall Hook_7738E0( const uint8_t *keys, unsigned count )
	{
		uint8_t merged[ 16 ]{};
		if( count > 14 )
		{
			count = 14;
		}
		if( keys && count )
		{
			memcpy( merged, keys, count );
		}
		count = PSUVR::VRSpeech::InjectKeys( merged, count );
		return pOriginal( merged, count );
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )pOriginal, reinterpret_cast< PVOID >( Hook_7738E0 ) );
	}
}
