/*
* Applies a fire action: when the action's +0x3B type is 1 it copies the shot's aim angles
* (16-bit game angles, 0x10000 = 360 degrees; pitch from +0x34, yaw from +0x36) into
* 0x00B90E78 / 0x00B90E74, which the bullet direction code 0x004CB3A0 reads, then fires.
* Debug only: logs each aim shot next to the game camera and the controller aim, to
* calibrate the VR free aim reticle.
*/

#pragma once

#include "../PSUEx.h"
#include "../Misc/Log.h"
#include "../PSUVR/VRCamera.h"
#include "../PSUVR/VRSyntheticPad.h"

namespace sub_4F9F50
{
	typedef void( __thiscall *function_t )( void *action, int arg );
	function_t pOriginal = reinterpret_cast< function_t >( 0x004F9F50 );

	constexpr uintptr_t kShotPitch = 0x00B90E78;
	constexpr uintptr_t kShotYaw = 0x00B90E74;
	constexpr float kGameAngleToDegrees = 360.0f / 65536.0f;

	void __fastcall Hook_4F9F50( void *action, void * /*edx*/, int arg )
	{
		pOriginal( action, arg );
		__try
		{
			const uint8_t *a = static_cast< const uint8_t * >( action );
			const short pitch = *reinterpret_cast< const short * >( kShotPitch );
			const short yaw = *reinterpret_cast< const short * >( kShotYaw );
			float camYaw = 0.0f, camPitch = 0.0f, wantYaw = 0.0f, wantPitch = 0.0f;
			const bool haveCam = PSUVR::VRCamera::RenderCameraYawPitch( camYaw, camPitch );
			const bool haveWant = PSUVR::VRSyntheticPad::LastFreeAim( wantYaw, wantPitch );
			PSU_LOG( "VRShot: type %u shot yaw %.2f pitch %.2f deg (raw %d %d) | camera yaw %.2f pitch %.2f%s | controller "
				"yaw %.2f pitch %.2f%s | camera state %d | action +30 %08lX +34 %04X +36 %04X +38 %02X %02X +3B %02X\n",
				a[ 0x3B ], yaw * kGameAngleToDegrees, pitch * kGameAngleToDegrees, yaw, pitch, camYaw * 57.29578f,
				camPitch * 57.29578f, haveCam ? "" : " (none)", wantYaw * 57.29578f, wantPitch * 57.29578f,
				haveWant ? "" : " (not aiming)", PSUVR::VRCamera::CameraState(), *reinterpret_cast< const DWORD * >( a + 0x30 ),
				*reinterpret_cast< const uint16_t * >( a + 0x34 ), *reinterpret_cast< const uint16_t * >( a + 0x36 ), a[ 0x38 ],
				a[ 0x39 ], a[ 0x3B ] );
		}
		__except( EXCEPTION_EXECUTE_HANDLER )
		{
		}
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )pOriginal, reinterpret_cast< PVOID >( Hook_4F9F50 ) );
	}
}
