/*
* Bullet direction (TItemBulletBase): sets a new bullet's yaw / pitch (16-bit game angles and
* radians) from the facing (with spread and a small auto-pitch) or, for firing type 0x1B,
* the shot aim globals, then the velocity at bullet +0x50; the start (muzzle) is at +0x44.
* Called by every gun bullet's setup (TItemBulletNormal 0x004E3B90). Debug only: logs each
* bullet next to the game camera and the controller aim, to calibrate the VR free aim reticle.
*/

#pragma once

#include "../PSUEx.h"
#include "../Misc/Log.h"
#include "../PSUStructs/PSUCombat.h"
#include "../PSUVR/VRCamera.h"
#include "../PSUVR/VRSyntheticPad.h"

namespace sub_4CB3A0
{
	typedef void( __stdcall *function_t )( void *bullet, int spread, int *pitch, int *yaw, float *pitchRad, float *yawRad,
		void *extra );
	function_t pOriginal = reinterpret_cast< function_t >( 0x004CB3A0 );

	unsigned s_logs = 0;
	constexpr unsigned kMaxLogs = 300;

	void __stdcall Hook_4CB3A0( void *bullet, int spread, int *pitch, int *yaw, float *pitchRad, float *yawRad, void *extra )
	{
		pOriginal( bullet, spread, pitch, yaw, pitchRad, yawRad, extra );
		if( s_logs >= kMaxLogs )
		{
			return;
		}
		__try
		{
			const uint8_t *b = static_cast< const uint8_t * >( bullet );
			const uint8_t *weapon = *reinterpret_cast< const uint8_t * const * >( b + 0x20 );
			const float *start = reinterpret_cast< const float * >( b + 0x44 );
			const float *velocity = reinterpret_cast< const float * >( b + 0x50 );
			float camYaw = 0.0f, camPitch = 0.0f, wantYaw = 0.0f, wantPitch = 0.0f;
			const bool haveCam = PSUVR::VRCamera::RenderCameraYawPitch( camYaw, camPitch );
			const bool haveWant = PSUVR::VRSyntheticPad::LastFreeAim( wantYaw, wantPitch );
			PSUCombat::State combat{};
			const bool haveCombat = PSUCombat::Read( combat );
			constexpr float kAngle = 360.0f / 65536.0f;
			++s_logs;
			PSU_LOG( "VRBullet: type %02X spread %d | yaw %.2f pitch %.2f deg (raw %d %d; rad %.4f %.4f) | start (%.1f %.1f %.1f) "
				"velocity (%.2f %.2f %.2f) | facing %.2f aim pitch %.2f | camera yaw %.2f pitch %.2f%s | controller yaw %.2f pitch %.2f%s | camera state %d\n",
				weapon ? weapon[ 0x35 ] : 0xFF, spread, *yaw * kAngle, *pitch * kAngle, *yaw, *pitch, *yawRad, *pitchRad, start[ 0 ],
				start[ 1 ], start[ 2 ], velocity[ 0 ], velocity[ 1 ], velocity[ 2 ], haveCombat ? combat.facing * kAngle : -1.0f,
				haveCombat ? -combat.aimPitch * kAngle : 0.0f,
				camYaw * 57.29578f, camPitch * 57.29578f, haveCam ? "" : " (none)", wantYaw * 57.29578f, wantPitch * 57.29578f,
				haveWant ? "" : " (not aiming)", PSUVR::VRCamera::CameraState() );
		}
		__except( EXCEPTION_EXECUTE_HANDLER )
		{
		}
	}

	void Hook()
	{
		DetourAttach( &( PVOID & )pOriginal, reinterpret_cast< PVOID >( Hook_4CB3A0 ) );
	}
}
