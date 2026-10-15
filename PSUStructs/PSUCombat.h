#pragma once

#include "../PSUMemory/Memory.hpp"

#include <cstdint>

// Read-only view of the local player's melee combo state (offsets from probe runs,
// 2026-10-15; see docs/ADDRESS_MAP.md "Local player struct fields").
namespace PSUCombat
{
	// What a hand holds, from the weapon class (+0x1F0 right, +0x1F4 left). Seen so far:
	// 0x04 twin knuckles, 0x06 spear, 0x12 saber, 0x22 handgun. The high digit looks like
	// 0 = two-handed, 1 = right-hand one-handed, 2 = left-hand one-handed (guns).
	enum class WeaponKind { None, Melee1H, Melee2H, Twin, Gun1H, Unknown };

	inline WeaponKind ClassifyWeapon( uint32_t weaponClass )
	{
		if( weaponClass == 0xFFFFFFFFu )
		{
			return WeaponKind::None;
		}
		switch( weaponClass )
		{
			case 0x04: return WeaponKind::Twin;
			case 0x06: return WeaponKind::Melee2H;
			case 0x12: return WeaponKind::Melee1H;
			case 0x22: return WeaponKind::Gun1H;
			default: break;
		}
		switch( weaponClass >> 4 )
		{
			case 0: return WeaponKind::Melee2H;  // rifles / rods also land here until mapped
			case 1: return WeaponKind::Melee1H;
			case 2: return WeaponKind::Gun1H;
			default: return WeaponKind::Unknown;
		}
	}

	inline const char *WeaponKindName( WeaponKind kind )
	{
		switch( kind )
		{
			case WeaponKind::None: return "none";
			case WeaponKind::Melee1H: return "melee 1H";
			case WeaponKind::Melee2H: return "melee 2H";
			case WeaponKind::Twin: return "twin";
			case WeaponKind::Gun1H: return "gun 1H";
			default: return "unknown";
		}
	}

	struct State
	{
		uint32_t attackState = 0;  // +0x710
		int comboStep = 0;         // 1..3 while attacking, 0 idle (+0x1E4 low digit)
		bool photonArt = false;    // the chain is a Photon Art (+0x1E4 0x2n)
		bool earlyPress = false;   // a press landed before the window (+0x7B4)
		bool hitObject = false;    // the swing's hit object exists (+0x00C / +0x010)
		uint32_t weaponClass = 0;  // +0x1F0 right hand (0xFFFFFFFF = none)
		uint32_t leftClass = 0;    // +0x1F4 left hand
		uint32_t activeClass = 0;  // +0x1DC >> 8: the weapon being used (switches to the left one on R1)
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
		// 0x11..0x13 normal attack chain, 0x21..0x23 Photon Art chain.
		const uint32_t chain = step >> 4;
		const uint32_t hit = step & 0xF;
		out.comboStep = ( chain == 1 || chain == 2 ) && hit >= 1 && hit <= 3 ? static_cast< int >( hit ) : 0;
		out.photonArt = chain == 2 && out.comboStep > 0;
		out.earlyPress = ( dword( 0x7B4 ) & 0x01000000 ) != 0;
		out.hitObject = dword( 0x010 ) != 0;
		out.weaponClass = dword( 0x1F0 );
		out.leftClass = dword( 0x1F4 );
		out.activeClass = ( dword( 0x1DC ) >> 8 ) & 0xFF;  // upper half can be 0xFFFF
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
