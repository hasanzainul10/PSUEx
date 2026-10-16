#include "VRHands.h"

#include "VRCamera.h"
#include "VRControllers.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"
#include "../PSUMemory/Memory.hpp"

#include <Windows.h>
#include <d3dx9.h>

#include <cmath>
#include <cstdio>

namespace PSUVR
{
	namespace
	{
		// PSU human skeleton (28 bones; bone records 0x90 bytes, parent at +6). Controller
		// hand 0 drives the character's left arm.
		constexpr int kBoneCount = 28;
		constexpr size_t kRecordSize = 0x90;
		constexpr size_t kRecordParent = 0x06;
		struct Arm
		{
			int shoulder, upper, fore, hand;
		};
		constexpr Arm kArms[ 2 ] = { { 14, 15, 16, 17 }, { 19, 20, 21, 22 } };

		// The wrist sits this far behind the controller's grip point (grip +Z points back).
		constexpr float kWristBackMeters = 0.06f;

		// Model vtable +0x18: bone matrix in game world, multiplied into the matrix passed in
		// (comes back transposed: translation at [3], [7], [11]).
		typedef char( __thiscall *GetBoneMatrixFn )( void *model, float *matrix, int bone, int, int, int );

		DWORD s_lastLog = 0;
		bool s_loggedWorld = false;

		// Model -> world, measured after the player's last draw. The game's bone query
		// re-runs the pose build (and so Apply), so it never runs inside Apply.
		D3DXMATRIX s_world;
		DWORD s_worldTick = 0;
		bool s_haveWorld = false;
		bool s_inWorldQuery = false;
		constexpr DWORD kWorldStaleMs = 500;

		// Why Apply returned early (logged every 3 s while diagnosing).
		enum Skip
		{
			kSkipSetting, kSkipFirstPerson, kSkipControllers, kSkipSkeleton, kSkipLayout, kSkipWorld,
			kSkipInverse, kSkipBody, kSkipTracking, kSolved, kSkipCount
		};
		const char *const kSkipNames[ kSkipCount ] = { "setting", "firstPerson", "controllers", "skeleton", "layout",
			"world", "inverse", "body", "tracking", "solved" };
		unsigned s_skips[ kSkipCount ]{};
		DWORD s_lastSkipLog = 0;

		void Note( Skip why )
		{
			++s_skips[ why ];
			if( GetTickCount() - s_lastSkipLog >= 3000 )
			{
				s_lastSkipLog = GetTickCount();
				char line[ 256 ];
				int len = 0;
				for( int i = 0; i < kSkipCount && len < 200; ++i )
				{
					len += sprintf_s( line + len, sizeof( line ) - len, " %s=%u", kSkipNames[ i ], s_skips[ i ] );
				}
				PSU_LOG( "VRHands: player poses:%s\n", line );
			}
		}

		bool Readable( const void *p, size_t bytes )
		{
			return p && PSUMemory::IsValidPointer( p ) &&
				PSUMemory::IsValidPointer( static_cast< const uint8_t * >( p ) + bytes - 1 );
		}

		D3DXVECTOR3 Position( const float *out, int bone )
		{
			const float *m = out + bone * 16;
			return { m[ 12 ], m[ 13 ], m[ 14 ] };
		}

		int Parent( const uint8_t *records, int bone )
		{
			return *reinterpret_cast< const short * >( records + bone * kRecordSize + kRecordParent );
		}

		bool Below( const uint8_t *records, int bone, int root )
		{
			for( int guard = 0; bone >= 0 && guard < kBoneCount; ++guard )
			{
				if( bone == root )
				{
					return true;
				}
				bone = Parent( records, bone );
			}
			return false;
		}

		D3DXVECTOR3 RotateByQuat( const float q[ 4 ], const D3DXVECTOR3 &v )
		{
			// v + 2w(u x v) + 2(u x (u x v)), u = q.xyz
			const D3DXVECTOR3 u( q[ 0 ], q[ 1 ], q[ 2 ] );
			D3DXVECTOR3 t;
			D3DXVec3Cross( &t, &u, &v );
			t *= 2.0f;
			D3DXVECTOR3 c;
			D3DXVec3Cross( &c, &u, &t );
			return v + t * q[ 3 ] + c;
		}

		// Row-vector matrix rotating unit a onto unit b about pivot (v' = (v - p) R + p).
		D3DXMATRIX RotationAbout( const D3DXVECTOR3 &a, const D3DXVECTOR3 &b, const D3DXVECTOR3 &pivot )
		{
			D3DXVECTOR3 k;
			D3DXVec3Cross( &k, &a, &b );
			const float c = D3DXVec3Dot( &a, &b );
			// Column form R = I + K + K^2 / (1 + c), K = skew(a x b); valid away from c = -1.
			float r[ 3 ][ 3 ] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
			if( c > -0.999f )
			{
				const float K[ 3 ][ 3 ] = { { 0, -k.z, k.y }, { k.z, 0, -k.x }, { -k.y, k.x, 0 } };
				const float f = 1.0f / ( 1.0f + c );
				for( int i = 0; i < 3; ++i )
				{
					for( int j = 0; j < 3; ++j )
					{
						float k2 = 0.0f;
						for( int n = 0; n < 3; ++n )
						{
							k2 += K[ i ][ n ] * K[ n ][ j ];
						}
						r[ i ][ j ] += K[ i ][ j ] + k2 * f;
					}
				}
			}
			// Row-vector form is the transpose.
			D3DXMATRIX m;
			D3DXMatrixIdentity( &m );
			for( int i = 0; i < 3; ++i )
			{
				for( int j = 0; j < 3; ++j )
				{
					m( i, j ) = r[ j ][ i ];
				}
			}
			D3DXVECTOR3 moved;
			D3DXVec3TransformNormal( &moved, &pivot, &m );
			m( 3, 0 ) = pivot.x - moved.x;
			m( 3, 1 ) = pivot.y - moved.y;
			m( 3, 2 ) = pivot.z - moved.z;
			return m;
		}

		void ApplyToSubtree( float *out, const uint8_t *records, int root, const D3DXMATRIX &delta )
		{
			for( int b = 0; b < kBoneCount; ++b )
			{
				if( Below( records, b, root ) )
				{
					D3DXMATRIX *m = reinterpret_cast< D3DXMATRIX * >( out + b * 16 );
					D3DXMatrixMultiply( m, m, &delta );
				}
			}
		}

		D3DXVECTOR3 Normalized( const D3DXVECTOR3 &v, const D3DXVECTOR3 &fallback )
		{
			const float len = D3DXVec3Length( &v );
			return len > 1.0e-5f ? v / len : fallback;
		}

		// Model -> game world, from the game's own bone query: world(root) = out(root) * W.
		bool ModelToWorld( void *model, const float *out, D3DXMATRIX &world )
		{
			void *const *vt = *static_cast< void *const *const * >( model );
			const auto getBone = reinterpret_cast< GetBoneMatrixFn >( vt[ 6 ] );
			float g[ 32 ]{};
			g[ 0 ] = g[ 5 ] = g[ 10 ] = g[ 15 ] = 1.0f;
			if( !getBone( model, g, 0, 0, 0, 1 ) )
			{
				return false;
			}
			D3DXMATRIX rootWorld;
			for( int i = 0; i < 4; ++i )
			{
				for( int j = 0; j < 4; ++j )
				{
					rootWorld( i, j ) = g[ j * 4 + i ];
				}
			}
			D3DXMATRIX rootInverse;
			if( !D3DXMatrixInverse( &rootInverse, nullptr, reinterpret_cast< const D3DXMATRIX * >( out ) ) )
			{
				return false;
			}
			D3DXMatrixMultiply( &world, &rootInverse, &rootWorld );
			return std::isfinite( world._41 ) && std::isfinite( world._42 ) && std::isfinite( world._43 );
		}

		void SolveArm( float *out, const uint8_t *records, const Arm &arm, const D3DXVECTOR3 &target,
			const D3DXVECTOR3 &pole )
		{
			const D3DXVECTOR3 s = Position( out, arm.upper );
			const D3DXVECTOR3 e = Position( out, arm.fore );
			const D3DXVECTOR3 h = Position( out, arm.hand );
			const D3DXVECTOR3 se = e - s;
			const D3DXVECTOR3 eh = h - e;
			const float a = D3DXVec3Length( &se );
			const float b = D3DXVec3Length( &eh );
			if( a < 1.0e-3f || b < 1.0e-3f )
			{
				return;
			}
			D3DXVECTOR3 toTarget = target - s;
			float dist = D3DXVec3Length( &toTarget );
			const D3DXVECTOR3 dir = Normalized( toTarget, Normalized( h - s, { 0.0f, -1.0f, 0.0f } ) );
			const float minReach = std::fabs( a - b ) + 1.0e-3f;
			const float maxReach = ( a + b ) * 0.999f;
			dist = dist < minReach ? minReach : ( dist > maxReach ? maxReach : dist );

			// Elbow in the plane of the reach and the pole.
			D3DXVECTOR3 bend = pole - dir * D3DXVec3Dot( &pole, &dir );
			bend = Normalized( bend, Normalized( se - dir * D3DXVec3Dot( &se, &dir ), { 0.0f, -1.0f, 0.0f } ) );
			const float cosA = ( a * a + dist * dist - b * b ) / ( 2.0f * a * dist );
			const float clamped = cosA < -1.0f ? -1.0f : ( cosA > 1.0f ? 1.0f : cosA );
			const float sinA = std::sqrt( 1.0f - clamped * clamped );
			const D3DXVECTOR3 elbow = s + dir * ( a * clamped ) + bend * ( a * sinA );
			const D3DXVECTOR3 wrist = s + dir * dist;

			// Upper arm: shoulder -> elbow; then the forearm: elbow -> wrist.
			ApplyToSubtree( out, records, arm.upper,
				RotationAbout( Normalized( se, dir ), Normalized( elbow - s, dir ), s ) );
			const D3DXVECTOR3 e1 = Position( out, arm.fore );
			const D3DXVECTOR3 h1 = Position( out, arm.hand );
			ApplyToSubtree( out, records, arm.fore,
				RotationAbout( Normalized( h1 - e1, dir ), Normalized( wrist - e1, dir ), e1 ) );
		}
	}

	namespace VRHands
	{
		void AfterPlayerDraw( void *model )
		{
			if( !Config::Get().vr.m_handsFollow || !VRCamera::FirstPersonActive() || s_inWorldQuery )
			{
				return;
			}
			s_inWorldQuery = true;
			__try
			{
				const float *out = *reinterpret_cast< const float * const * >( static_cast< const uint8_t * >( model ) + 0x64 );
				D3DXMATRIX world;
				if( Readable( out, 64 ) && ModelToWorld( model, out, world ) )
				{
					s_world = world;
					s_worldTick = GetTickCount();
					s_haveWorld = true;
				}
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				s_haveWorld = false;
			}
			s_inWorldQuery = false;
		}

		void Apply( void *model, float *out, const uint8_t *skeleton )
		{
			// Also runs inside the world query's pose rebuild, so the second eye keeps the arms.
			if( !Config::Get().vr.m_handsFollow )
			{
				Note( kSkipSetting );
				return;
			}
			if( !VRCamera::FirstPersonActive() )
			{
				Note( kSkipFirstPerson );
				return;
			}
			const ControllerState *pads[ 2 ] = { &VRControllers::State( 0 ), &VRControllers::State( 1 ) };
			if( !( pads[ 0 ]->gripValid || pads[ 1 ]->gripValid ) )
			{
				Note( kSkipControllers );
				return;
			}
			__try
			{
				const int count = *reinterpret_cast< const int * >( skeleton + 0x28 );
				const uint8_t *records = *reinterpret_cast< const uint8_t * const * >( skeleton + 0x30 );
				if( count != kBoneCount || !Readable( records, kBoneCount * kRecordSize ) ||
					!Readable( out, kBoneCount * 64 ) )
				{
					Note( kSkipSkeleton );
					return;
				}
				for( const Arm &arm : kArms )
				{
					if( Parent( records, arm.upper ) != arm.shoulder || Parent( records, arm.fore ) != arm.upper ||
						Parent( records, arm.hand ) != arm.fore )
					{
						Note( kSkipLayout );
						return;
					}
				}
				if( !s_haveWorld || GetTickCount() - s_worldTick > kWorldStaleMs )
				{
					Note( kSkipWorld );
					return;
				}
				const D3DXMATRIX world = s_world;
				D3DXMATRIX toModel;
				if( !D3DXMatrixInverse( &toModel, nullptr, &world ) )
				{
					Note( kSkipInverse );
					return;
				}
				float forward[ 3 ], right[ 3 ];
				if( !VRCamera::BodyAxes( forward, right ) )
				{
					Note( kSkipBody );
					return;
				}
				if( !s_loggedWorld )
				{
					s_loggedWorld = true;
					const uint8_t *node = *reinterpret_cast< const uint8_t * const * >( static_cast< const uint8_t * >( model ) + 0x10 );
					const float *nm = Readable( node, 0x44 ) ? reinterpret_cast< const float * >( node + 4 ) : nullptr;
					PSU_LOG( "VRHands: model->world [%.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f | %.2f %.2f %.2f]\n",
						world._11, world._12, world._13, world._21, world._22, world._23, world._31, world._32, world._33,
						world._41, world._42, world._43 );
					if( nm )
					{
						PSU_LOG( "VRHands: node+4 [%.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f]\n",
							nm[ 0 ], nm[ 1 ], nm[ 2 ], nm[ 3 ], nm[ 4 ], nm[ 5 ], nm[ 6 ], nm[ 7 ], nm[ 8 ], nm[ 9 ], nm[ 10 ], nm[ 11 ],
							nm[ 12 ], nm[ 13 ], nm[ 14 ], nm[ 15 ] );
					}
				}

				const bool log = GetTickCount() - s_lastLog >= 3000;
				if( log )
				{
					s_lastLog = GetTickCount();
				}
				for( int hand = 0; hand < 2; ++hand )
				{
					const ControllerState &c = *pads[ hand ];
					if( !c.gripValid )
					{
						continue;
					}
					const D3DXVECTOR3 grip( c.gripPosition[ 0 ], c.gripPosition[ 1 ], c.gripPosition[ 2 ] );
					const D3DXVECTOR3 wristTracking = grip + RotateByQuat( c.gripOrientation, { 0.0f, 0.0f, kWristBackMeters } );
					const float tracking[ 3 ] = { wristTracking.x, wristTracking.y, wristTracking.z };
					float targetWorld[ 3 ];
					if( !VRCamera::TrackingToWorld( tracking, targetWorld ) )
					{
						Note( kSkipTracking );
						return;
					}
					// Elbows hang down, a little back and out to the side.
					const float side = hand == 0 ? -1.0f : 1.0f;
					const D3DXVECTOR3 poleWorld( -forward[ 0 ] * 0.5f + right[ 0 ] * side * 0.4f, -1.0f,
						-forward[ 2 ] * 0.5f + right[ 2 ] * side * 0.4f );
					D3DXVECTOR3 target, pole;
					const D3DXVECTOR3 tw( targetWorld[ 0 ], targetWorld[ 1 ], targetWorld[ 2 ] );
					D3DXVec3TransformCoord( &target, &tw, &toModel );
					D3DXVec3TransformNormal( &pole, &poleWorld, &toModel );
					pole = Normalized( pole, { 0.0f, -1.0f, 0.0f } );

					const Arm &arm = kArms[ hand ];
					if( log )
					{
						const D3DXVECTOR3 s = Position( out, arm.upper );
						const D3DXVECTOR3 h = Position( out, arm.hand );
						const D3DXVECTOR3 reach = target - s;
						PSU_LOG( "VRHands: %s target world (%.1f %.1f %.1f) model (%.2f %.2f %.2f) shoulder (%.2f %.2f %.2f) hand (%.2f %.2f %.2f) reach %.2f\n",
							hand == 0 ? "L" : "R", targetWorld[ 0 ], targetWorld[ 1 ], targetWorld[ 2 ], target.x, target.y, target.z,
							s.x, s.y, s.z, h.x, h.y, h.z, D3DXVec3Length( &reach ) );
					}
					SolveArm( out, records, arm, target, pole );
				}
				Note( kSolved );
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				PSU_LOG( "VRHands: exception\n" );
			}
		}
	}
}
