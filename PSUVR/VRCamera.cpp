#include "VRCamera.h"

#include "../Misc/Log.h"

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

		Quat ToQuat( const float o[ 4 ] ) { return { o[ 0 ], o[ 1 ], o[ 2 ], o[ 3 ] }; }
		Vec3 ToVec( const float p[ 3 ] ) { return { p[ 0 ], p[ 1 ], p[ 2 ] }; }

		bool s_haveReference = false;
		Vec3 s_refPosition{};
		float s_refYaw = 0.0f;
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
			const Vec3 forward = Rotate( ToQuat( views[ 0 ].orientation ), { 0.0f, 0.0f, -1.0f } );
			s_refYaw = std::atan2( -forward.x, -forward.z );
			s_haveReference = true;
			PSU_LOG( "VRCamera: recentered at (%.3f, %.3f, %.3f) yaw=%.1f deg\n",
				s_refPosition.x, s_refPosition.y, s_refPosition.z, s_refYaw * 57.29578f );
		}

		bool HasReference()
		{
			return s_haveReference;
		}

		void BuildEye( const EyePose &eye, float unitsPerMeter, const D3DMATRIX &gameProj,
			D3DMATRIX &view, D3DMATRIX &proj )
		{
			const Quat yawInverse = YawQuat( -s_refYaw );
			const Vec3 offset = ToVec( eye.position );
			Vec3 p = Rotate( yawInverse, { offset.x - s_refPosition.x, offset.y - s_refPosition.y, offset.z - s_refPosition.z } );
			p = { p.x * unitsPerMeter, p.y * unitsPerMeter, p.z * unitsPerMeter };
			const Quat q = Multiply( yawInverse, ToQuat( eye.orientation ) );

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
