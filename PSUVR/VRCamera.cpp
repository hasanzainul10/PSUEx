#include "VRCamera.h"

#include "StereoRenderer.h"
#include "VRGamepad.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <Windows.h>

#include <cmath>

namespace PSUVR
{
	namespace
	{
		struct Quat
		{
			float x, y, z, w;
		};

		struct Vec3
		{
			float x, y, z;
		};

		constexpr float kPi = 3.14159265f;

		Quat Multiply( const Quat &a, const Quat &b )
		{
			return {
				a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
				a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
				a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
		}

		Vec3 Rotate( const Quat &q, const Vec3 &v )
		{
			const Quat p{ v.x, v.y, v.z, 0.0f };
			const Quat conj{ -q.x, -q.y, -q.z, q.w };
			const Quat r = Multiply( Multiply( q, p ), conj );
			return { r.x, r.y, r.z };
		}

		Quat YawQuat( float yaw )
		{
			return { 0.0f, std::sin( yaw * 0.5f ), 0.0f, std::cos( yaw * 0.5f ) };
		}

		// Rotation about +X; positive pitches -Z (forward) toward +Y (up).
		Quat PitchQuat( float pitch )
		{
			return { std::sin( pitch * 0.5f ), 0.0f, 0.0f, std::cos( pitch * 0.5f ) };
		}

		Quat ToQuat( const float o[ 4 ] ) { return { o[ 0 ], o[ 1 ], o[ 2 ], o[ 3 ] }; }
		Vec3 ToVec( const float p[ 3 ] ) { return { p[ 0 ], p[ 1 ], p[ 2 ] }; }
		Vec3 Add( const Vec3 &a, const Vec3 &b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		Vec3 Scale( const Vec3 &a, float s ) { return { a.x * s, a.y * s, a.z * s }; }

		float WrapAngle( float a )
		{
			while( a > kPi ) a -= 2.0f * kPi;
			while( a < -kPi ) a += 2.0f * kPi;
			return a;
		}

		// Yaw of an OpenXR orientation around +Y (0 = looking down -Z).
		float PoseYaw( const float orientation[ 4 ] )
		{
			const Vec3 forward = Rotate( ToQuat( orientation ), { 0.0f, 0.0f, -1.0f } );
			return std::atan2( -forward.x, -forward.z );
		}

		bool s_haveReference = false;
		Vec3 s_refPosition{};
		float s_refYaw = 0.0f;
		// First person: tracking-space point the head is leashed to (follows the head
		// once it leans further than fpLeanLimit / fpCrouchLimit).
		Vec3 s_fpAnchor{};

		// Body-locked HUD panel: tracking-space yaw it faces, eased toward the head
		// only after the head has looked away by more than vr.hudFollowAngle.
		float s_hudYaw = 0.0f;
		bool s_hudFollowing = false;
		LARGE_INTEGER s_hudLastTime{};

		// First person (game world: forward = ( sin, 0, cos ), right = ( -cos, 0, sin ),
		// turning left increases the yaw). The game camera is steered behind the head;
		// the eyes are built from wherever the game actually renders from.
		bool s_fpPrimed = false;
		bool s_fpActive = false;
		bool s_turnArmed = true;
		bool s_turnFromStick = false;
		bool s_loggedTurnSource = false;
		float s_bodyYaw = 0.0f;
		float s_lastWrittenYaw = 0.0f;
		float s_camDistance = 0.0f;  // game units from steered camera to head
		float s_steerHmdYaw = 0.0f;  // HMD yaw (relative to the reference) the camera was steered to
		Vec3 s_headWorld{};          // game world head position at the last steer
		LARGE_INTEGER s_lastSteerTime{};
		// The play camera task can stop updating; first person must not keep stale state.
		DWORD s_lastSteerTick = 0;
		constexpr DWORD kSteerStaleMs = 250;

		// Render camera object fields (position +0xFC, target +0x108), noted by the camera
		// hook, and the values captured right before the eye passes.
		const float *s_renderCamPos = nullptr;
		const float *s_renderCamTarget = nullptr;
		bool s_haveRenderCam = false;
		Vec3 s_renderPos{};
		Vec3 s_renderTarget{};

		unsigned s_turnLogs = 0;

		// Steered camera distance behind the head. Further back widens what the
		// game's frustum culling keeps around the head.
		constexpr float kSteerDistanceUnits = 25.0f;
		// Fallback (no XInput pad): per-tick game camera yaw change that counts as a
		// right-stick turn. High enough to ignore the follow camera's own drift.
		constexpr float kCameraDeltaDeadband = 1.5f * ( kPi / 180.0f );
		// Snap turn on the stick: fire past kSnapPress, re-arm under kSnapRelease.
		constexpr float kSnapPress = 0.6f;
		constexpr float kSnapRelease = 0.3f;

		// Captured render camera yaw (game convention) and pitch (positive = looking up).
		bool RenderCameraAngles( float &yaw, float &pitch )
		{
			if( !s_haveRenderCam )
			{
				return false;
			}
			const float dx = s_renderTarget.x - s_renderPos.x;
			const float dy = s_renderTarget.y - s_renderPos.y;
			const float dz = s_renderTarget.z - s_renderPos.z;
			const float horizontal = std::sqrt( dx * dx + dz * dz );
			if( horizontal < 1.0e-4f && std::fabs( dy ) < 1.0e-4f )
			{
				return false;
			}
			yaw = std::atan2( dx, dz );
			pitch = std::atan2( dy, horizontal );
			return true;
		}

		// View/projection for an eye at p with orientation q, both in game camera space
		// (right-handed, +Y up, looking down -Z, game units).
		void BuildViewProj( const EyePose &eye, const D3DMATRIX &gameProj, const Vec3 &p, const Quat &q,
			D3DMATRIX &view, D3DMATRIX &proj )
		{
			// Column-vector rotation of the eye in camera space. D3D row vectors use
			// v_eye = ( v_cam - p ) * R, i.e. VIEW = T(-p) * R.
			const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
			const float r[ 3 ][ 3 ] = {
				{ 1.0f - 2.0f * ( yy + zz ), 2.0f * ( xy - wz ), 2.0f * ( xz + wy ) },
				{ 2.0f * ( xy + wz ), 1.0f - 2.0f * ( xx + zz ), 2.0f * ( yz - wx ) },
				{ 2.0f * ( xz - wy ), 2.0f * ( yz + wx ), 1.0f - 2.0f * ( xx + yy ) } };

			view = {};
			view._11 = r[ 0 ][ 0 ]; view._12 = r[ 0 ][ 1 ]; view._13 = r[ 0 ][ 2 ];
			view._21 = r[ 1 ][ 0 ]; view._22 = r[ 1 ][ 1 ]; view._23 = r[ 1 ][ 2 ];
			view._31 = r[ 2 ][ 0 ]; view._32 = r[ 2 ][ 1 ]; view._33 = r[ 2 ][ 2 ];
			view._41 = -( p.x * r[ 0 ][ 0 ] + p.y * r[ 1 ][ 0 ] + p.z * r[ 2 ][ 0 ] );
			view._42 = -( p.x * r[ 0 ][ 1 ] + p.y * r[ 1 ][ 1 ] + p.z * r[ 2 ][ 1 ] );
			view._43 = -( p.x * r[ 0 ][ 2 ] + p.y * r[ 1 ][ 2 ] + p.z * r[ 2 ][ 2 ] );
			view._44 = 1.0f;

			// Right-handed off-center projection (camera looks down -Z, w = -z).
			const float left = std::tan( eye.fovAngleLeft );
			const float right = std::tan( eye.fovAngleRight );
			const float up = std::tan( eye.fovAngleUp );
			const float down = std::tan( eye.fovAngleDown );
			proj = gameProj;
			proj._11 = 2.0f / ( right - left );
			proj._12 = 0.0f;
			proj._21 = 0.0f;
			proj._22 = 2.0f / ( up - down );
			proj._31 = ( right + left ) / ( right - left );
			proj._32 = ( up + down ) / ( up - down );
		}

		void LogTurn( float delta )
		{
			if( s_turnLogs < 12 )
			{
				++s_turnLogs;
				PSU_LOG( "VRCamera FP: turn %.2f deg -> body=%.1f deg (%s, %s)\n",
					delta * ( 180.0f / kPi ), s_bodyYaw * ( 180.0f / kPi ),
					Config::Get().vr.m_snapTurn ? "snap" : "smooth", s_turnFromStick ? "XInput" : "camera-delta" );
			}
		}

		// Right stick X (-1..1): stick right turns right, i.e. lowers the game yaw.
		void ApplyStickTurn( float stickX, float dt )
		{
			const auto &vr = Config::Get().vr;
			const float magnitude = std::fabs( stickX );
			if( vr.m_snapTurn )
			{
				if( magnitude < kSnapRelease )
				{
					s_turnArmed = true;
					return;
				}
				if( magnitude < kSnapPress || !s_turnArmed )
				{
					return;
				}
				s_turnArmed = false;
				const float step = vr.m_snapTurnDegrees * ( kPi / 180.0f );
				const float delta = stickX > 0.0f ? -step : step;
				s_bodyYaw = WrapAngle( s_bodyYaw + delta );
				LogTurn( delta );
				return;
			}
			if( magnitude <= 0.0f )
			{
				return;
			}
			const float delta = -stickX * vr.m_fpTurnSpeed * ( kPi / 180.0f ) * dt;
			s_bodyYaw = WrapAngle( s_bodyYaw + delta );
			LogTurn( delta );
		}

		// Fallback without XInput: the game's own right-stick orbit, read back as a yaw change.
		void ApplyCameraDeltaTurn( float delta )
		{
			const auto &vr = Config::Get().vr;
			if( std::fabs( delta ) < kCameraDeltaDeadband )
			{
				s_turnArmed = true;
				return;
			}
			if( vr.m_snapTurn )
			{
				if( !s_turnArmed )
				{
					return;
				}
				s_turnArmed = false;
				const float step = vr.m_snapTurnDegrees * ( kPi / 180.0f );
				delta = delta > 0.0f ? step : -step;
			}
			s_bodyYaw = WrapAngle( s_bodyYaw + delta );
			LogTurn( delta );
		}

		float SteerDeltaSeconds()
		{
			LARGE_INTEGER now{};
			LARGE_INTEGER frequency{};
			QueryPerformanceCounter( &now );
			QueryPerformanceFrequency( &frequency );
			float dt = 0.0f;
			if( s_lastSteerTime.QuadPart != 0 && frequency.QuadPart > 0 )
			{
				dt = static_cast< float >( now.QuadPart - s_lastSteerTime.QuadPart ) /
					static_cast< float >( frequency.QuadPart );
			}
			s_lastSteerTime = now;
			return dt < 0.0f ? 0.0f : ( dt > 0.1f ? 0.1f : dt );
		}
	}

	namespace VRCamera
	{
		void Recenter( const EyePose views[ 2 ] )
		{
			s_refPosition = {
				( views[ 0 ].position[ 0 ] + views[ 1 ].position[ 0 ] ) * 0.5f,
				( views[ 0 ].position[ 1 ] + views[ 1 ].position[ 1 ] ) * 0.5f,
				( views[ 0 ].position[ 2 ] + views[ 1 ].position[ 2 ] ) * 0.5f };
			// Yaw only, so the game camera's horizon stays level with the real one.
			s_refYaw = PoseYaw( views[ 0 ].orientation );
			s_haveReference = true;
			s_fpAnchor = s_refPosition;
			s_hudYaw = s_refYaw;
			s_hudFollowing = false;
			// First person re-derives body yaw from the camera on its next steer.
			s_fpPrimed = false;
			PSU_LOG( "VRCamera: recentered at (%.3f, %.3f, %.3f) yaw=%.1f deg\n",
				s_refPosition.x, s_refPosition.y, s_refPosition.z, s_refYaw * 57.29578f );
		}

		bool HasReference()
		{
			return s_haveReference;
		}

		void NoteRenderCamera( const void *cameraPosition, const void *cameraTarget )
		{
			s_renderCamPos = static_cast< const float * >( cameraPosition );
			s_renderCamTarget = static_cast< const float * >( cameraTarget );
		}

		void CaptureRenderCamera()
		{
			s_haveRenderCam = false;
			if( !s_renderCamPos || !s_renderCamTarget )
			{
				return;
			}
			__try
			{
				s_renderPos = { s_renderCamPos[ 0 ], s_renderCamPos[ 1 ], s_renderCamPos[ 2 ] };
				s_renderTarget = { s_renderCamTarget[ 0 ], s_renderCamTarget[ 1 ], s_renderCamTarget[ 2 ] };
				s_haveRenderCam = true;
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				s_haveRenderCam = false;
			}
		}

		void BuildEye( const EyePose &eye, float unitsPerMeter, const D3DMATRIX &gameProj,
			D3DMATRIX &view, D3DMATRIX &proj )
		{
			// Third person: HMD on top of the game's orbit camera. The camera's own pitch
			// is removed so the horizon stays level with the real one.
			float camYaw = 0.0f, camPitch = 0.0f;
			const bool level = RenderCameraAngles( camYaw, camPitch );
			const Quat unpitch = level ? PitchQuat( -camPitch ) : Quat{ 0.0f, 0.0f, 0.0f, 1.0f };
			const Quat bodyInverse = YawQuat( -s_refYaw );
			const Vec3 offset = ToVec( eye.position );
			const Vec3 local = Rotate( bodyInverse,
				{ offset.x - s_refPosition.x, offset.y - s_refPosition.y, offset.z - s_refPosition.z } );
			const Vec3 p = Rotate( unpitch, Scale( local, unitsPerMeter ) );
			const Quat q = Multiply( unpitch, Multiply( bodyInverse, ToQuat( eye.orientation ) ) );
			BuildViewProj( eye, gameProj, p, q, view, proj );
		}

		bool FirstPersonMode()
		{
			// Stereo scenes only; elsewhere the game camera stays on the flat virtual screen.
			return Config::Get().vr.m_cameraMode == "firstPerson" && StereoRenderer::SceneAllowsStereo();
		}

		bool SteerFirstPerson( float cameraPosition[ 3 ], float focusPosition[ 3 ],
			const float playerPosition[ 3 ], int cameraState )
		{
			EyePose views[ 2 ];
			EyePose viewSpace[ 2 ];
			if( !FirstPersonMode() || !s_haveReference || !System::Get().NextViews( views, viewSpace ) )
			{
				s_fpActive = false;
				return false;
			}
			const auto &vr = Config::Get().vr;
			const float dt = SteerDeltaSeconds();
			// States 1 (follow) and 3 (refocus) are normal play. Others (5 = NPC talk) are
			// game-driven: no head steering and no turning, so nothing moves by itself.
			const bool playState = cameraState == 1 || cameraState == 3;

			// Yaw the game left the camera at after its own update.
			const float dx = focusPosition[ 0 ] - cameraPosition[ 0 ];
			const float dz = focusPosition[ 2 ] - cameraPosition[ 2 ];
			const bool haveGameYaw = ( dx * dx + dz * dz ) > 1.0e-6f;
			const float gameYaw = haveGameYaw ? std::atan2( dx, dz ) : s_lastWrittenYaw;
			float stickX = 0.0f;
			s_turnFromStick = VRGamepad::RightStickX( stickX );
			if( !s_loggedTurnSource )
			{
				s_loggedTurnSource = true;
				PSU_LOG( "VRCamera FP: turn source = %s\n", s_turnFromStick ? "XInput" : "camera-delta" );
			}
			if( !s_fpPrimed )
			{
				// Body forward = where the camera points now (after recenter: where you look).
				s_bodyYaw = gameYaw;
				s_fpPrimed = true;
				s_turnArmed = true;
				PSU_LOG( "VRCamera FP: primed body=%.1f deg, player=(%.2f, %.2f, %.2f)\n",
					s_bodyYaw * ( 180.0f / kPi ), playerPosition[ 0 ], playerPosition[ 1 ], playerPosition[ 2 ] );
			}
			else if( playState && s_turnFromStick )
			{
				ApplyStickTurn( stickX, dt );
			}
			else if( playState && haveGameYaw )
			{
				ApplyCameraDeltaTurn( WrapAngle( gameYaw - s_lastWrittenYaw ) );
			}

			// "head": forward = where you look (stick turns add on top). "body": FPS-style.
			const bool headMove = playState && vr.m_fpMoveDirection != "body";
			const float hmdYaw = headMove ? WrapAngle( PoseYaw( views[ 0 ].orientation ) - s_refYaw ) : 0.0f;
			const float yaw = WrapAngle( s_bodyYaw + hmdYaw );
			const float fx = std::sin( yaw );
			const float fz = std::cos( yaw );
			const float head[ 3 ] = {
				playerPosition[ 0 ],
				playerPosition[ 1 ] + vr.m_fpEyeHeight * kGameUnitsPerMeter,
				playerPosition[ 2 ] };
			focusPosition[ 0 ] = head[ 0 ];
			focusPosition[ 1 ] = head[ 1 ];
			focusPosition[ 2 ] = head[ 2 ];
			cameraPosition[ 0 ] = head[ 0 ] - fx * kSteerDistanceUnits;
			cameraPosition[ 1 ] = head[ 1 ];
			cameraPosition[ 2 ] = head[ 2 ] - fz * kSteerDistanceUnits;

			s_headWorld = { head[ 0 ], head[ 1 ], head[ 2 ] };
			s_lastWrittenYaw = yaw;
			s_lastSteerTick = GetTickCount();
			s_steerHmdYaw = hmdYaw;
			s_camDistance = kSteerDistanceUnits;
			s_fpActive = true;
			return true;
		}

		bool FirstPersonActive()
		{
			return s_fpActive && FirstPersonMode() && GetTickCount() - s_lastSteerTick < kSteerStaleMs;
		}

		void ClearFirstPerson()
		{
			s_fpActive = false;
		}

		bool TurnFromStick()
		{
			return s_turnFromStick;
		}

		float FirstPersonCameraDistance()
		{
			return s_camDistance;
		}

		float BodyYaw()
		{
			return s_bodyYaw;
		}

		void UpdateFirstPersonAnchor( const EyePose views[ 2 ] )
		{
			const auto &vr = Config::Get().vr;
			const Vec3 head{
				( views[ 0 ].position[ 0 ] + views[ 1 ].position[ 0 ] ) * 0.5f,
				( views[ 0 ].position[ 1 ] + views[ 1 ].position[ 1 ] ) * 0.5f,
				( views[ 0 ].position[ 2 ] + views[ 1 ].position[ 2 ] ) * 0.5f };
			// Horizontal leash: lean up to fpLeanLimit, then the anchor follows the head.
			const float lean = vr.m_fpLeanLimit < 0.0f ? 0.0f : vr.m_fpLeanLimit;
			const float dx = head.x - s_fpAnchor.x;
			const float dz = head.z - s_fpAnchor.z;
			const float horizontal = std::sqrt( dx * dx + dz * dz );
			if( horizontal > lean && horizontal > 1.0e-6f )
			{
				const float pull = ( horizontal - lean ) / horizontal;
				s_fpAnchor.x += dx * pull;
				s_fpAnchor.z += dz * pull;
			}
			// Vertical leash (ducking, standing up).
			const float crouch = vr.m_fpCrouchLimit < 0.0f ? 0.0f : vr.m_fpCrouchLimit;
			const float dy = head.y - s_fpAnchor.y;
			if( dy > crouch )
			{
				s_fpAnchor.y += dy - crouch;
			}
			else if( dy < -crouch )
			{
				s_fpAnchor.y += dy + crouch;
			}
		}

		void BuildEyeFirstPerson( const EyePose &eye, float unitsPerMeter, const D3DMATRIX &gameProj,
			D3DMATRIX &view, D3DMATRIX &proj )
		{
			// Body frame: VR yaw s_refYaw = game yaw s_bodyYaw, level, origin at the head.
			const Quat bodyInverse = YawQuat( -s_refYaw );
			const Quat bodyEye = Multiply( bodyInverse, ToQuat( eye.orientation ) );
			const Vec3 eyePos = ToVec( eye.position );
			Vec3 offset = Scale( Rotate( bodyInverse,
				{ eyePos.x - s_fpAnchor.x, eyePos.y - s_fpAnchor.y, eyePos.z - s_fpAnchor.z } ), unitsPerMeter );
			// The face offset follows where the head looks, so the eyes stay in front of the face.
			Vec3 look = Rotate( bodyEye, { 0.0f, 0.0f, -1.0f } );
			const float flat = std::sqrt( look.x * look.x + look.z * look.z );
			look = flat > 1.0e-4f ? Vec3{ look.x / flat, 0.0f, look.z / flat } : Vec3{ 0.0f, 0.0f, -1.0f };
			offset = Add( offset, Scale( look, Config::Get().vr.m_fpEyeForward * kGameUnitsPerMeter ) );

			// Camera the game actually renders from (our steer, or the NPC talk camera).
			// Place the eye at the head in that camera's space, so any game re-aim or
			// smoothing is cancelled: the world only moves by stick input and the head.
			float camYaw = 0.0f, camPitch = 0.0f;
			Vec3 headCam{ 0.0f, 0.0f, -s_camDistance };
			float yawDelta = s_steerHmdYaw;
			if( RenderCameraAngles( camYaw, camPitch ) )
			{
				const float dhx = s_headWorld.x - s_renderPos.x;
				const float dhy = s_headWorld.y - s_renderPos.y;
				const float dhz = s_headWorld.z - s_renderPos.z;
				const float sy = std::sin( camYaw ), cy = std::cos( camYaw );
				const float sp = std::sin( camPitch ), cp = std::cos( camPitch );
				const float ahead = dhx * sy + dhz * cy;   // along the camera's ground forward
				const float right = -dhx * cy + dhz * sy;  // game right = ( -cos, 0, sin )
				headCam = { right, -ahead * sp + dhy * cp, -( ahead * cp + dhy * sp ) };
				yawDelta = WrapAngle( camYaw - s_bodyYaw );
			}
			else
			{
				camPitch = 0.0f;
			}
			// Body -> camera: undo the camera's yaw relative to the body, then its pitch.
			const Quat bodyToCam = Multiply( PitchQuat( -camPitch ), YawQuat( -yawDelta ) );
			const Vec3 p = Add( headCam, Rotate( bodyToCam, offset ) );
			const Quat q = Multiply( bodyToCam, bodyEye );
			BuildViewProj( eye, gameProj, p, q, view, proj );
		}

		void HudPanelPose( const EyePose views[ 2 ], float distanceMeters, float position[ 3 ], float orientation[ 4 ] )
		{
			const auto &vr = Config::Get().vr;
			LARGE_INTEGER now{};
			LARGE_INTEGER frequency{};
			QueryPerformanceCounter( &now );
			QueryPerformanceFrequency( &frequency );
			float dt = 0.0f;
			if( s_hudLastTime.QuadPart != 0 && frequency.QuadPart > 0 )
			{
				dt = static_cast< float >( now.QuadPart - s_hudLastTime.QuadPart ) /
					static_cast< float >( frequency.QuadPart );
			}
			s_hudLastTime = now;
			dt = dt < 0.0f ? 0.0f : ( dt > 0.1f ? 0.1f : dt );

			// Lazy follow: stay put while the head looks around, glide back once it has
			// looked away further than hudFollowAngle, stop when nearly centred.
			const float headYaw = PoseYaw( views[ 0 ].orientation );
			const float delta = WrapAngle( headYaw - s_hudYaw );
			const float followAngle = vr.m_hudFollowAngle * ( kPi / 180.0f );
			constexpr float kSettle = 5.0f * ( kPi / 180.0f );
			constexpr float kFollowRate = 4.0f;  // 1/s, exponential ease
			if( std::fabs( delta ) > followAngle )
			{
				s_hudFollowing = true;
			}
			if( s_hudFollowing )
			{
				const float step = 1.0f - std::exp( -kFollowRate * dt );
				s_hudYaw = WrapAngle( s_hudYaw + delta * step );
				if( std::fabs( WrapAngle( headYaw - s_hudYaw ) ) < kSettle )
				{
					s_hudFollowing = false;
				}
			}

			// First person anchors at the body leash point so leaning doesn't move it.
			const Vec3 anchor = FirstPersonActive() ? s_fpAnchor : s_refPosition;
			const float drop = vr.m_hudDrop;
			const Quat yaw = YawQuat( s_hudYaw );
			const Vec3 ahead = Rotate( yaw, { 0.0f, -drop, -distanceMeters } );
			position[ 0 ] = anchor.x + ahead.x;
			position[ 1 ] = anchor.y + ahead.y;
			position[ 2 ] = anchor.z + ahead.z;
			// Tilt the top back slightly so the panel faces the eyes from below eye level
			// (negative rotation about +X turns the quad's +Z normal upward).
			const float tilt = std::atan2( drop, distanceMeters );
			const Quat q = Multiply( yaw, PitchQuat( -tilt ) );
			orientation[ 0 ] = q.x;
			orientation[ 1 ] = q.y;
			orientation[ 2 ] = q.z;
			orientation[ 3 ] = q.w;
		}

		void PanelPose( float distanceMeters, float dropMeters, float position[ 3 ], float orientation[ 4 ] )
		{
			const Quat yaw = YawQuat( s_refYaw );
			const Vec3 ahead = Rotate( yaw, { 0.0f, 0.0f, -distanceMeters } );
			position[ 0 ] = s_refPosition.x + ahead.x;
			position[ 1 ] = s_refPosition.y + ahead.y - dropMeters;
			position[ 2 ] = s_refPosition.z + ahead.z;
			orientation[ 0 ] = yaw.x;
			orientation[ 1 ] = yaw.y;
			orientation[ 2 ] = yaw.z;
			orientation[ 3 ] = yaw.w;
		}
	}
}
