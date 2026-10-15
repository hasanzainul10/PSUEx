#pragma once

#include "../PSUMemory/Memory.hpp"

#include <cstdint>

// Read-only view of the local player's melee combo state (offsets from probe runs,
// 2026-10-15; see docs/ADDRESS_MAP.md "Local player struct fields").
namespace PSUCombat
{
	struct State
	{
		uint32_t attackState = 0;  // +0x710
		int comboStep = 0;         // 1..3 while attacking, 0 idle (+0x1E4 - 0x10)
		bool earlyPress = false;   // a press landed before the window (+0x7B4)
		bool hitObject = false;    // the swing's hit object exists (+0x00C / +0x010)
		uint32_t weaponClass = 0;  // +0x1F0 (raw; meaning per value still to map)
		uint16_t facing = 0;       // +0x124, 0x10000 = 360 degrees
	};

	// False when there is no local player (login, area change).
	inline bool Read( State &out )
	{
		out = {};
		const uintptr_t table = 0x009CA8AC;
		if( !PSUMemory::IsValidPointer( reinterpret_cast< const void * >( table ) ) )
		{
			return false;
		}
		const uintptr_t player = *reinterpret_cast< const uintptr_t * >( table );
		if( !player || !PSUMemory::IsValidPointer( reinterpret_cast< const void * >( player ) ) ||
			!PSUMemory::IsValidPointer( reinterpret_cast< const void * >( player + 0x800 ) ) )
		{
			return false;
		}
		const auto dword = [ player ]( uintptr_t offset ) { return *reinterpret_cast< const uint32_t * >( player + offset ); };
		out.attackState = dword( 0x710 );
		const uint32_t step = dword( 0x1E4 );
		out.comboStep = step >= 0x11 && step <= 0x13 ? static_cast< int >( step - 0x10 ) : 0;
		out.earlyPress = ( dword( 0x7B4 ) & 0x01000000 ) != 0;
		out.hitObject = dword( 0x010 ) != 0;
		out.weaponClass = dword( 0x1F0 );
		out.facing = static_cast< uint16_t >( dword( 0x124 ) & 0xFFFF );
		return true;
	}

	// Mid-combo and the next press would be accepted (+0x710 == 0x0E; idle is 0x0080030E).
	inline bool ComboWindowOpen( const State &state )
	{
		return state.comboStep > 0 && ( state.attackState & 0xFFFF ) == 0x000E;
	}

	// Swinging: presses now are early (+0x710 == 0x02).
	inline bool Swinging( const State &state )
	{
		return state.comboStep > 0 && ( state.attackState & 0xFFFF ) == 0x0002;
	}
}
