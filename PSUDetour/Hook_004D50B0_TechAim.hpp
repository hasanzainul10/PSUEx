/*
* Technique owner check (TItemTechBase, technique in EAX): every technique's update calls it
* each frame. A technique stores where it flies from at +0xAC and its two angles at +0xE0 /
* +0xE4 (straight techniques: TItemTechStraight update 0x004EE2B0). Debug only: logs each
* technique once, the first time it is seen, next to the game camera and the controller aim,
* to find out how techniques aim (they don't read the gun's free aim angles).
*/

#pragma once

#include "../PSUEx.h"
#include "../Misc/Log.h"
#include "../PSUStructs/PSUCombat.h"
#include "../PSUStructs/PSUPlayerData.h"
#include "../PSUVR/VRCamera.h"
#include "../PSUVR/VRSyntheticPad.h"

namespace sub_4D50B0
{
	PVOID pOriginal = reinterpret_cast< PVOID >( 0x004D50B0 );

	constexpr unsigned kSeen = 64;
	const void *s_seen[ kSeen ]{};
	unsigned s_seenNext = 0;
	unsigned s_logs = 0;
	constexpr unsigned kMaxLogs = 300;

	// RTTI class name from the object's vtable (vtable[-1] = complete object locator).
	const char *ClassName( const void *object )
	{
		const uint8_t *const *vt = *static_cast< const uint8_t *const *const * >( object );
		const uint8_t *col = reinterpret_cast< const uint8_t * const * >( vt )[ -1 ];
		const uint8_t *typeDescriptor = *reinterpret_cast< const uint8_t * const * >( col + 0x0C );
		return reinterpret_cast< const char * >( typeDescriptor + 8 );
	}

	void __cdecl NoteTech( const void *tech )
	{
		if( s_logs >= kMaxLogs || !tech )
		{
			return;
		}
		for( const void *seen : s_seen )
		{
			if( seen == tech )
			{
				return;
			}
		}
		s_seen[ s_seenNext ] = tech;
		s_seenNext = ( s_seenNext + 1 ) % kSeen;
		__try
		{
			const uint8_t *t = static_cast< const uint8_t * >( tech );
			const float *pos = reinterpret_cast< const float * >( t + 0xAC );
			const float a0 = *reinterpret_cast< const float * >( t + 0xE0 );
			const float a1 = *reinterpret_cast< const float * >( t + 0xE4 );
			const DWORD *raw = reinterpret_cast< const DWORD * >( t + 0xD8 );
			float camYaw = 0.0f, camPitch = 0.0f, wantYaw = 0.0f, wantPitch = 0.0f;
			const bool haveCam = PSUVR::VRCamera::RenderCameraYawPitch( camYaw, camPitch );
			const bool haveWant = PSUVR::VRSyntheticPad::LastFreeAim( wantYaw, wantPitch );
			PSUCombat::State combat{};
			const bool haveCombat = PSUCombat::Read( combat );
			const PSUPlayer &player = PSUPlayer::Get();
			++s_logs;
			PSU_LOG( "VRTech: %s at (%.1f %.1f %.1f) +E0 %.3f (%.1f deg) +E4 %.3f (%.1f deg) | player (%.1f %.1f %.1f) facing %.1f deg | "
				"camera yaw %.1f pitch %.1f%s | controller yaw %.1f pitch %.1f%s | camera state %d | +D8 %08lX %08lX %08lX %08lX %08lX %08lX\n",
				ClassName( tech ), pos[ 0 ], pos[ 1 ], pos[ 2 ], a0, a0 * 57.29578f, a1, a1 * 57.29578f, player.position.x,
				player.position.y, player.position.z, haveCombat ? combat.facing * ( 360.0f / 65536.0f ) : -1.0f, camYaw * 57.29578f,
				camPitch * 57.29578f, haveCam ? "" : " (none)", wantYaw * 57.29578f, wantPitch * 57.29578f,
				haveWant ? "" : " (not aiming)", PSUVR::VRCamera::CameraState(), raw[ 0 ], raw[ 1 ], raw[ 2 ], raw[ 3 ], raw[ 4 ],
				raw[ 5 ] );
		}
		__except( EXCEPTION_EXECUTE_HANDLER )
		{
		}
	}

	// The technique arrives in EAX; ECX / EDX are kept too.
	__declspec( naked ) void Hook_4D50B0()
	{
		__asm
		{
			push ecx
			push edx
			push eax
			push eax
			call NoteTech
			add esp, 4
			pop eax
			pop edx
			pop ecx
			jmp dword ptr [ pOriginal ]
		}
	}

	void Hook()
	{
		DetourAttach( &pOriginal, reinterpret_cast< PVOID >( Hook_4D50B0 ) );
	}
}
