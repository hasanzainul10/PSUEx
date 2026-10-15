#include "VRControllers.h"

#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

#include <cmath>

namespace PSUVR
{
	namespace
	{
		IVRRuntime *s_runtime = nullptr;
		ControllerState s_state[ 2 ]{};
		uint32_t s_pressed[ 2 ]{};
		uint32_t s_released[ 2 ]{};
		bool s_loggedActive[ 2 ]{};
		unsigned s_buttonLogs = 0;

		const char *const kHandNames[ 2 ] = { "left", "right" };

		struct V3 { float x, y, z; };

		V3 Sub( const V3 &a, const V3 &b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }

		// v rotated by q (x,y,z,w).
		V3 Rotate( const float q[ 4 ], const V3 &v )
		{
			const V3 u{ q[ 0 ], q[ 1 ], q[ 2 ] };
			const float w = q[ 3 ];
			const V3 t{ 2.0f * ( u.y * v.z - u.z * v.y ), 2.0f * ( u.z * v.x - u.x * v.z ), 2.0f * ( u.x * v.y - u.y * v.x ) };
			return { v.x + w * t.x + ( u.y * t.z - u.z * t.y ),
				v.y + w * t.y + ( u.z * t.x - u.x * t.z ),
				v.z + w * t.z + ( u.x * t.y - u.y * t.x ) };
		}

		V3 RotateInverse( const float q[ 4 ], const V3 &v )
		{
			const float c[ 4 ] = { -q[ 0 ], -q[ 1 ], -q[ 2 ], q[ 3 ] };
			return Rotate( c, v );
		}

		// LOCAL point -> eye RT pixels. False when behind or too close to the eye.
		struct Projector
		{
			V3 eyePos;
			const float *eyeRot;
			float tanL, tanR, tanU, tanD;
			float width, height;

			bool Project( const V3 &p, float &sx, float &sy, float &depth ) const
			{
				const V3 d = RotateInverse( eyeRot, Sub( p, eyePos ) );
				if( d.z > -0.05f )
				{
					return false;
				}
				depth = -d.z;
				const float tx = d.x / depth;
				const float ty = d.y / depth;
				sx = ( tx - tanL ) / ( tanR - tanL ) * width;
				sy = ( tanU - ty ) / ( tanU - tanD ) * height;
				return true;
			}

			// Pixels per meter at depth.
			float Scale( float depth ) const
			{
				return width / ( ( tanR - tanL ) * depth );
			}
		};

		struct Vertex { float x, y, z, rhw; DWORD color; };

		// Saves the fixed-function state an overlay draw changes and restores it after.
		class OverlayState
		{
		public:
			explicit OverlayState( IDirect3DDevice9 *device ) : m_device( device )
			{
				device->GetFVF( &m_fvf );
				device->GetTexture( 0, &m_texture );
				device->GetVertexShader( &m_vs );
				device->GetPixelShader( &m_ps );
				device->GetStreamSource( 0, &m_vb, &m_vbOffset, &m_vbStride );
				for( int i = 0; i < kStates; ++i )
				{
					device->GetRenderState( kRenderStates[ i ], &m_render[ i ] );
				}
				for( int i = 0; i < kStages; ++i )
				{
					device->GetTextureStageState( 0, kStageStates[ i ], &m_stage[ i ] );
				}

				device->SetVertexShader( nullptr );
				device->SetPixelShader( nullptr );
				device->SetTexture( 0, nullptr );
				device->SetFVF( D3DFVF_XYZRHW | D3DFVF_DIFFUSE );
				device->SetRenderState( D3DRS_ALPHABLENDENABLE, TRUE );
				device->SetRenderState( D3DRS_SRCBLEND, D3DBLEND_SRCALPHA );
				device->SetRenderState( D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA );
				device->SetRenderState( D3DRS_SEPARATEALPHABLENDENABLE, FALSE );
				device->SetRenderState( D3DRS_ZENABLE, FALSE );
				device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
				device->SetRenderState( D3DRS_CULLMODE, D3DCULL_NONE );
				device->SetRenderState( D3DRS_ALPHATESTENABLE, FALSE );
				device->SetRenderState( D3DRS_FOGENABLE, FALSE );
				device->SetRenderState( D3DRS_CLIPPLANEENABLE, 0 );
				device->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1 );
				device->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE );
				device->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1 );
				device->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE );
			}

			~OverlayState()
			{
				for( int i = 0; i < kStages; ++i )
				{
					m_device->SetTextureStageState( 0, kStageStates[ i ], m_stage[ i ] );
				}
				for( int i = 0; i < kStates; ++i )
				{
					m_device->SetRenderState( kRenderStates[ i ], m_render[ i ] );
				}
				m_device->SetFVF( m_fvf );
				m_device->SetStreamSource( 0, m_vb, m_vbOffset, m_vbStride );
				m_device->SetTexture( 0, m_texture );
				m_device->SetVertexShader( m_vs );
				m_device->SetPixelShader( m_ps );
				if( m_texture ) m_texture->Release();
				if( m_vs ) m_vs->Release();
				if( m_ps ) m_ps->Release();
				if( m_vb ) m_vb->Release();
			}

		private:
			static constexpr int kStates = 11;
			static constexpr D3DRENDERSTATETYPE kRenderStates[ kStates ] = {
				D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_SEPARATEALPHABLENDENABLE,
				D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_CULLMODE, D3DRS_ALPHATESTENABLE, D3DRS_FOGENABLE,
				D3DRS_CLIPPLANEENABLE, D3DRS_SCISSORTESTENABLE };
			static constexpr int kStages = 4;
			static constexpr D3DTEXTURESTAGESTATETYPE kStageStates[ kStages ] = {
				D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1 };

			IDirect3DDevice9 *m_device;
			DWORD m_fvf = 0;
			IDirect3DBaseTexture9 *m_texture = nullptr;
			IDirect3DVertexShader9 *m_vs = nullptr;
			IDirect3DPixelShader9 *m_ps = nullptr;
			IDirect3DVertexBuffer9 *m_vb = nullptr;
			UINT m_vbOffset = 0, m_vbStride = 0;
			DWORD m_render[ kStates ]{};
			DWORD m_stage[ kStages ]{};
		};

		// Filled circle (triangle fan).
		void DrawDisc( IDirect3DDevice9 *device, float cx, float cy, float radius, DWORD color )
		{
			constexpr int kSegments = 20;
			Vertex fan[ kSegments + 2 ];
			fan[ 0 ] = { cx, cy, 0.0f, 1.0f, color };
			for( int i = 0; i <= kSegments; ++i )
			{
				const float a = 6.2831853f * static_cast< float >( i ) / kSegments;
				fan[ i + 1 ] = { cx + std::cos( a ) * radius, cy + std::sin( a ) * radius, 0.0f, 1.0f, color };
			}
			device->DrawPrimitiveUP( D3DPT_TRIANGLEFAN, kSegments, fan, sizeof( Vertex ) );
		}

		// Thick screen-space line between two LOCAL points (clipped near the eye by sampling).
		void DrawRay( IDirect3DDevice9 *device, const Projector &proj, const V3 &from, const V3 &to,
			float thicknessPx, DWORD colorNear, DWORD colorFar )
		{
			constexpr int kSteps = 16;
			Vertex strip[ ( kSteps + 1 ) * 2 ];
			int count = 0;
			float prevX = 0.0f, prevY = 0.0f;
			bool havePrev = false;
			for( int i = 0; i <= kSteps; ++i )
			{
				const float t = static_cast< float >( i ) / kSteps;
				const V3 p{ from.x + ( to.x - from.x ) * t, from.y + ( to.y - from.y ) * t, from.z + ( to.z - from.z ) * t };
				float sx, sy, depth;
				if( !proj.Project( p, sx, sy, depth ) )
				{
					continue;
				}
				if( !havePrev )
				{
					prevX = sx;
					prevY = sy;
					havePrev = true;
					continue;
				}
				float dx = sx - prevX, dy = sy - prevY;
				const float length = std::sqrt( dx * dx + dy * dy );
				if( length < 0.5f )
				{
					continue;
				}
				dx /= length;
				dy /= length;
				const float half = thicknessPx * 0.5f;
				const DWORD color = t < 0.5f ? colorNear : colorFar;
				if( count == 0 )
				{
					strip[ count++ ] = { prevX - dy * half, prevY + dx * half, 0.0f, 1.0f, colorNear };
					strip[ count++ ] = { prevX + dy * half, prevY - dx * half, 0.0f, 1.0f, colorNear };
				}
				strip[ count++ ] = { sx - dy * half, sy + dx * half, 0.0f, 1.0f, color };
				strip[ count++ ] = { sx + dy * half, sy - dx * half, 0.0f, 1.0f, color };
				prevX = sx;
				prevY = sy;
			}
			if( count >= 4 )
			{
				device->DrawPrimitiveUP( D3DPT_TRIANGLESTRIP, count - 2, strip, sizeof( Vertex ) );
			}
		}
	}

	namespace VRControllers
	{
		void Update( IVRRuntime *runtime )
		{
			s_runtime = runtime;
			for( int hand = 0; hand < 2; ++hand )
			{
				const uint32_t before = s_state[ hand ].buttons;
				if( !runtime || !runtime->GetControllerState( hand, s_state[ hand ] ) )
				{
					s_state[ hand ] = {};
				}
				const uint32_t now = s_state[ hand ].buttons;
				s_pressed[ hand ] = now & ~before;
				s_released[ hand ] = before & ~now;

				if( s_state[ hand ].active != s_loggedActive[ hand ] )
				{
					s_loggedActive[ hand ] = s_state[ hand ].active;
					PSU_LOG( "VRControllers: %s controller %s\n", kHandNames[ hand ],
						s_state[ hand ].active ? "active" : "inactive" );
				}
				if( ( s_pressed[ hand ] || s_released[ hand ] ) && s_buttonLogs < 40 )
				{
					++s_buttonLogs;
					const ControllerState &c = s_state[ hand ];
					PSU_LOG( "VRControllers: %s buttons 0x%02X trigger %.2f grip %.2f stick (%.2f, %.2f) grip pos (%.2f, %.2f, %.2f)\n",
						kHandNames[ hand ], c.buttons, c.trigger, c.grip, c.stick[ 0 ], c.stick[ 1 ],
						c.gripPosition[ 0 ], c.gripPosition[ 1 ], c.gripPosition[ 2 ] );
				}
			}
		}

		const ControllerState &State( int hand )
		{
			return s_state[ hand & 1 ];
		}

		bool Active( int hand )
		{
			return s_state[ hand & 1 ].active;
		}

		bool AnyActive()
		{
			return s_state[ 0 ].active || s_state[ 1 ].active;
		}

		uint32_t Pressed( int hand )
		{
			return s_pressed[ hand & 1 ];
		}

		uint32_t Released( int hand )
		{
			return s_released[ hand & 1 ];
		}

		void Haptic( int hand, float amplitude, float seconds )
		{
			if( s_runtime )
			{
				s_runtime->Haptic( hand, amplitude, seconds );
			}
		}

		void DrawMarkers( IDirect3DDevice9 *device, const EyePose &eye, unsigned width, unsigned height )
		{
			if( !device || !Config::Get().vr.m_showControllers || !AnyActive() || width < 1 || height < 1 )
			{
				return;
			}
			Projector proj{};
			proj.eyePos = { eye.position[ 0 ], eye.position[ 1 ], eye.position[ 2 ] };
			proj.eyeRot = eye.orientation;
			proj.tanL = std::tan( eye.fovAngleLeft );
			proj.tanR = std::tan( eye.fovAngleRight );
			proj.tanU = std::tan( eye.fovAngleUp );
			proj.tanD = std::tan( eye.fovAngleDown );
			proj.width = static_cast< float >( width );
			proj.height = static_cast< float >( height );
			if( proj.tanR - proj.tanL < 1.0e-4f || proj.tanU - proj.tanD < 1.0e-4f )
			{
				return;
			}

			OverlayState state( device );
			for( int hand = 0; hand < 2; ++hand )
			{
				const ControllerState &c = s_state[ hand ];
				if( !c.active )
				{
					continue;
				}
				// Left blue, right orange; brighter while the trigger is held.
				const bool trigger = ( c.buttons & kControllerTrigger ) != 0;
				const BYTE alpha = trigger ? 230 : 160;
				const DWORD color = hand == 0 ? D3DCOLOR_ARGB( alpha, 80, 160, 255 ) : D3DCOLOR_ARGB( alpha, 255, 150, 60 );

				if( c.aimValid )
				{
					const V3 origin{ c.aimPosition[ 0 ], c.aimPosition[ 1 ], c.aimPosition[ 2 ] };
					const V3 dir = Rotate( c.aimOrientation, { 0.0f, 0.0f, -1.0f } );
					constexpr float kRayLength = 2.0f;
					const V3 end{ origin.x + dir.x * kRayLength, origin.y + dir.y * kRayLength, origin.z + dir.z * kRayLength };
					const DWORD farColor = ( color & 0x00FFFFFFu ) | 0x30000000u;
					DrawRay( device, proj, origin, end, trigger ? 4.0f : 2.5f, color, farColor );
				}
				if( c.gripValid )
				{
					float sx, sy, depth;
					const V3 grip{ c.gripPosition[ 0 ], c.gripPosition[ 1 ], c.gripPosition[ 2 ] };
					if( proj.Project( grip, sx, sy, depth ) )
					{
						// About a 3 cm ball, never smaller than a few pixels.
						float radius = 0.03f * proj.Scale( depth );
						radius = radius < 3.0f ? 3.0f : radius;
						DrawDisc( device, sx, sy, radius, color );
					}
				}
			}
		}
	}
}
