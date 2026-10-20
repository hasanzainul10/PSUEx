#include "VRControllerLabels.h"

#include "D3D11Bridge.h"
#include "OpenXRRuntime.h"
#include "VRControllers.h"
#include "VRMotionLayout.h"
#include "VRSyntheticPad.h"
#include "VRUiPointer.h"

#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <imgui.h>
#include "backends/imgui_impl_dx9.h"

#include <d3d9.h>

#include <cfloat>
#include <cmath>
#include <string>

namespace PSUVR
{
	namespace
	{
		using VRMotionLayout::Control;

		constexpr float kRadians = 3.14159265f / 180.0f;
		// Looking at a controller: within this angle of the view centre, head pitched down.
		constexpr float kLookEnter = 25.0f * kRadians;
		constexpr float kLookLeave = 32.0f * kRadians;
		constexpr float kPitchEnter = -25.0f * kRadians;
		constexpr float kPitchLeave = -18.0f * kRadians;
		constexpr float kLookSeconds = 0.25f;
		constexpr float kFadeSeconds = 0.15f;
		// Using anything but the grips while they are held hides the grip cards until let go.
		constexpr float kTriggerUsed = 0.3f;
		constexpr float kStickUsed = 0.35f;
		constexpr float kSwingSpeed = 1.0f;  // m/s of the gripping hand

		struct V3 { float x, y, z; };
		V3 Add( const V3 &a, const V3 &b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		V3 Sub( const V3 &a, const V3 &b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		V3 Scale( const V3 &a, float s ) { return { a.x * s, a.y * s, a.z * s }; }
		float Dot( const V3 &a, const V3 &b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		V3 Cross( const V3 &a, const V3 &b ) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
		bool Normalize( V3 &v )
		{
			const float length = std::sqrt( Dot( v, v ) );
			if( length < 1.0e-5f )
			{
				return false;
			}
			v = Scale( v, 1.0f / length );
			return true;
		}

		V3 Rotate( const float q[ 4 ], const V3 &v )
		{
			const V3 u{ q[ 0 ], q[ 1 ], q[ 2 ] };
			const float w = q[ 3 ];
			const V3 t = Scale( Cross( u, v ), 2.0f );
			return Add( Add( v, Scale( t, w ) ), Cross( u, t ) );
		}

		// Rotation with these columns (right-handed basis) as a quaternion x y z w.
		void BasisToQuat( const V3 &x, const V3 &y, const V3 &z, float q[ 4 ] )
		{
			const float trace = x.x + y.y + z.z;
			if( trace > 0.0f )
			{
				const float s = std::sqrt( trace + 1.0f ) * 2.0f;
				q[ 3 ] = 0.25f * s;
				q[ 0 ] = ( y.z - z.y ) / s;
				q[ 1 ] = ( z.x - x.z ) / s;
				q[ 2 ] = ( x.y - y.x ) / s;
			}
			else if( x.x > y.y && x.x > z.z )
			{
				const float s = std::sqrt( 1.0f + x.x - y.y - z.z ) * 2.0f;
				q[ 3 ] = ( y.z - z.y ) / s;
				q[ 0 ] = 0.25f * s;
				q[ 1 ] = ( y.x + x.y ) / s;
				q[ 2 ] = ( z.x + x.z ) / s;
			}
			else if( y.y > z.z )
			{
				const float s = std::sqrt( 1.0f + y.y - x.x - z.z ) * 2.0f;
				q[ 3 ] = ( z.x - x.z ) / s;
				q[ 0 ] = ( y.x + x.y ) / s;
				q[ 1 ] = 0.25f * s;
				q[ 2 ] = ( z.y + y.z ) / s;
			}
			else
			{
				const float s = std::sqrt( 1.0f + z.z - x.x - y.y ) * 2.0f;
				q[ 3 ] = ( x.y - y.x ) / s;
				q[ 0 ] = ( z.x + x.z ) / s;
				q[ 1 ] = ( z.y + y.z ) / s;
				q[ 2 ] = 0.25f * s;
			}
		}

		ImFont *s_font = nullptr;
		LARGE_INTEGER s_lastTime{};

		bool s_looking[ 2 ]{};
		float s_lookTime[ 2 ]{};
		int s_gripMask = 0;
		float s_gripTime = 0.0f;
		bool s_gripUsed = false;      // another input since the grips went down
		bool s_gripShow = false;
		int s_contextMask = 0;        // the grips whose context the cards show
		float s_alpha[ 2 ]{};
		bool s_want[ 2 ]{};
		float s_position[ 2 ][ 3 ]{};
		float s_orientation[ 2 ][ 4 ]{ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f } };
		bool s_posed[ 2 ]{};
		bool s_drawn = false;         // Render filled the texture this frame
		bool s_loggedShow = false;

		ImDrawList *s_list = nullptr;

		float FrameSeconds()
		{
			LARGE_INTEGER now, frequency;
			QueryPerformanceCounter( &now );
			QueryPerformanceFrequency( &frequency );
			float dt = s_lastTime.QuadPart ? static_cast< float >( now.QuadPart - s_lastTime.QuadPart ) / static_cast< float >( frequency.QuadPart ) : 0.0f;
			s_lastTime = now;
			return dt > 0.1f ? 0.1f : dt;
		}

		bool OtherInputUsed()
		{
			for( int hand = 0; hand < 2; ++hand )
			{
				const ControllerState &c = VRControllers::State( hand );
				if( ( c.buttons & ~kControllerGrip ) != 0 || c.trigger > kTriggerUsed ||
					std::fabs( c.stick[ 0 ] ) > kStickUsed || std::fabs( c.stick[ 1 ] ) > kStickUsed )
				{
					return true;
				}
				if( ( s_gripMask & ( 1 << hand ) ) && c.velocityValid &&
					Dot( { c.linearVelocity[ 0 ], c.linearVelocity[ 1 ], c.linearVelocity[ 2 ] },
						{ c.linearVelocity[ 0 ], c.linearVelocity[ 1 ], c.linearVelocity[ 2 ] } ) > kSwingSpeed * kSwingSpeed )
				{
					return true;
				}
			}
			return false;
		}

		void HideAll()
		{
			s_want[ 0 ] = s_want[ 1 ] = false;
			s_looking[ 0 ] = s_looking[ 1 ] = false;
			s_lookTime[ 0 ] = s_lookTime[ 1 ] = 0.0f;
			s_gripShow = false;
		}

		const char *ContextText( int mask )
		{
			switch( mask )
			{
				case 1: return "left grip held";
				case 2: return "right grip held";
				case 3: return "both grips held";
				default: return nullptr;
			}
		}

		void DrawCard( ImDrawList &list, int hand, float alpha, const VRSyntheticPad::HandLabels &labels )
		{
			ImFont *font = s_font ? s_font : ImGui::GetIO().Fonts->Fonts[ 0 ];
			const float x0 = hand == 0 ? 0.0f : static_cast< float >( VRControllerLabels::kTextureWidth / 2 );
			const float width = static_cast< float >( VRControllerLabels::kTextureWidth / 2 );
			const float height = static_cast< float >( VRControllerLabels::kTextureHeight );
			const auto color = [ & ]( int r, int g, int b, float a ) { return IM_COL32( r, g, b, static_cast< int >( a * alpha * 255.0f ) ); };
			const ImU32 accent = hand == 0 ? color( 110, 180, 255, 1.0f ) : color( 255, 170, 90, 1.0f );

			list.AddRectFilled( ImVec2( x0 + 4.0f, 4.0f ), ImVec2( x0 + width - 4.0f, height - 4.0f ), color( 12, 16, 26, 0.85f ), 18.0f );
			list.AddRect( ImVec2( x0 + 4.0f, 4.0f ), ImVec2( x0 + width - 4.0f, height - 4.0f ), accent, 18.0f, 0, 4.0f );

			float y = 18.0f;
			const float pad = 20.0f;
			list.AddText( font, 36.0f, ImVec2( x0 + pad, y ), accent, hand == 0 ? "Left" : "Right" );
			if( const char *context = ContextText( s_contextMask ) )
			{
				list.AddText( font, 26.0f, ImVec2( x0 + pad + 100.0f, y + 8.0f ), color( 255, 230, 120, 1.0f ), context );
			}
			y += 50.0f;

			const std::string &profile = Config::Get().vr.m_motionProfile;
			const float nameWidth = 168.0f;
			const float textWidth = width - pad * 2.0f - nameWidth;
			const auto row = [ & ]( const char *name, const char *text )
			{
				if( !text || y > height - 30.0f )
				{
					return;
				}
				const ImVec2 textSize = font->CalcTextSizeA( 28.0f, FLT_MAX, textWidth, text );
				list.AddText( font, 22.0f, ImVec2( x0 + pad, y + 4.0f ), color( 160, 175, 200, 1.0f ), name );
				list.AddText( font, 28.0f, ImVec2( x0 + pad + nameWidth, y ), color( 255, 255, 255, 1.0f ), text, nullptr, textWidth );
				y += ( textSize.y > 28.0f ? textSize.y : 28.0f ) + 10.0f;
			};
			for( int control = 0; control < static_cast< int >( Control::Count ); ++control )
			{
				const char *text = labels.control[ control ];
				if( control == static_cast< int >( Control::Grip ) && ( s_contextMask & ( 1 << hand ) ) )
				{
					text = "(held)";
				}
				row( VRMotionLayout::ControlName( profile, hand, static_cast< Control >( control ) ), text );
			}
			row( "Swing", labels.swing );
		}
	}

	namespace VRControllerLabels
	{
		void SetFont( ImFont *font )
		{
			s_font = font;
		}

		void Update( const EyePose *views )
		{
			const float dt = FrameSeconds();
			const auto &vr = Config::Get().vr;
			const bool allowed = views && vr.m_inputMode == "motion" && VRControllers::AnyActive() && !VRUiPointer::Wanted() &&
				( vr.m_controllerLabels || vr.m_controllerLabelsGrip );
			if( !allowed )
			{
				HideAll();
				s_gripMask = 0;
			}
			else
			{
				const V3 head{ ( views[ 0 ].position[ 0 ] + views[ 1 ].position[ 0 ] ) * 0.5f,
					( views[ 0 ].position[ 1 ] + views[ 1 ].position[ 1 ] ) * 0.5f,
					( views[ 0 ].position[ 2 ] + views[ 1 ].position[ 2 ] ) * 0.5f };
				const V3 forward = Rotate( views[ 0 ].orientation, { 0.0f, 0.0f, -1.0f } );
				const float pitch = std::asin( forward.y < -1.0f ? -1.0f : ( forward.y > 1.0f ? 1.0f : forward.y ) );

				// Grips held still.
				int mask = 0;
				for( int hand = 0; hand < 2; ++hand )
				{
					if( VRControllers::State( hand ).buttons & kControllerGrip )
					{
						mask |= 1 << hand;
					}
				}
				if( mask != s_gripMask )
				{
					s_gripMask = mask;
					s_gripTime = 0.0f;
					s_gripUsed = false;
				}
				if( mask )
				{
					s_gripTime += dt;
					s_gripUsed = s_gripUsed || OtherInputUsed();
				}
				s_gripShow = vr.m_controllerLabelsGrip && mask && !s_gripUsed && s_gripTime >= vr.m_controllerLabelsHoldSeconds;
				s_contextMask = s_gripShow ? mask : 0;

				const float size = vr.m_controllerLabelsSize > 0.05f ? vr.m_controllerLabelsSize : 0.05f;
				for( int hand = 0; hand < 2; ++hand )
				{
					const ControllerState &c = VRControllers::State( hand );
					s_posed[ hand ] = c.active && c.gripValid;
					if( !s_posed[ hand ] )
					{
						s_looking[ hand ] = false;
						s_lookTime[ hand ] = 0.0f;
						s_want[ hand ] = false;
						continue;
					}
					const V3 grip{ c.gripPosition[ 0 ], c.gripPosition[ 1 ], c.gripPosition[ 2 ] };
					V3 toGrip = Sub( grip, head );
					bool look = false;
					if( vr.m_controllerLabels && Normalize( toGrip ) )
					{
						const float angle = std::acos( Dot( forward, toGrip ) > 1.0f ? 1.0f : Dot( forward, toGrip ) );
						look = s_looking[ hand ] ? ( angle < kLookLeave && pitch < kPitchLeave ) : ( angle < kLookEnter && pitch < kPitchEnter );
					}
					s_looking[ hand ] = look;
					s_lookTime[ hand ] = look ? s_lookTime[ hand ] + dt : 0.0f;
					s_want[ hand ] = s_gripShow || s_lookTime[ hand ] >= kLookSeconds;

					// Card above the controller, a little toward the head, facing the eyes.
					V3 toHead = Sub( head, grip );
					if( !Normalize( toHead ) )
					{
						toHead = { 0.0f, 0.0f, 1.0f };
					}
					const V3 centre = Add( Add( grip, { 0.0f, 0.07f + size * 0.5f, 0.0f } ), Scale( toHead, 0.03f ) );
					V3 z = Sub( head, centre );
					if( !Normalize( z ) )
					{
						z = { 0.0f, 0.0f, 1.0f };
					}
					V3 x = Cross( { 0.0f, 1.0f, 0.0f }, z );
					if( !Normalize( x ) )
					{
						x = { 1.0f, 0.0f, 0.0f };
					}
					const V3 y = Cross( z, x );
					s_position[ hand ][ 0 ] = centre.x;
					s_position[ hand ][ 1 ] = centre.y;
					s_position[ hand ][ 2 ] = centre.z;
					BasisToQuat( x, y, z, s_orientation[ hand ] );
				}
			}

			for( int hand = 0; hand < 2; ++hand )
			{
				const float step = kFadeSeconds > 0.0f ? dt / kFadeSeconds : 1.0f;
				s_alpha[ hand ] = s_want[ hand ] ? ( s_alpha[ hand ] + step > 1.0f ? 1.0f : s_alpha[ hand ] + step )
					: ( s_alpha[ hand ] - step < 0.0f ? 0.0f : s_alpha[ hand ] - step );
			}
			if( ( s_want[ 0 ] || s_want[ 1 ] ) && !s_loggedShow )
			{
				s_loggedShow = true;
				PSU_LOG( "VRLabels: first card shown (%s, context mask %d)\n", s_gripShow ? "grip hold" : "look down", s_contextMask );
			}
		}

		void Render( IDirect3DDevice9 *device, D3D11Bridge *bridge )
		{
			s_drawn = false;
			if( !device || !bridge || ( s_alpha[ 0 ] <= 0.0f && s_alpha[ 1 ] <= 0.0f ) )
			{
				return;
			}
			if( !bridge->EnsureLabelTarget( device, kTextureWidth, kTextureHeight ) )
			{
				return;
			}
			IDirect3DSurface9 *target = bridge->LabelSurface();
			if( !target )
			{
				return;
			}

			if( !s_list )
			{
				s_list = IM_NEW( ImDrawList )( ImGui::GetDrawListSharedData() );
			}
			ImDrawList &list = *s_list;
			list._ResetForNewFrame();
			list.PushTextureID( ImGui::GetIO().Fonts->TexID );
			list.PushClipRect( ImVec2( 0.0f, 0.0f ), ImVec2( static_cast< float >( kTextureWidth ), static_cast< float >( kTextureHeight ) ) );

			VRSyntheticPad::HandLabels labels[ 2 ];
			VRSyntheticPad::DescribeControls( s_contextMask, labels );
			for( int hand = 0; hand < 2; ++hand )
			{
				if( s_alpha[ hand ] > 0.0f )
				{
					DrawCard( list, hand, s_alpha[ hand ], labels[ hand ] );
				}
			}

			ImDrawData data;
			data.Valid = true;
			data.DisplayPos = ImVec2( 0.0f, 0.0f );
			data.DisplaySize = ImVec2( static_cast< float >( kTextureWidth ), static_cast< float >( kTextureHeight ) );
			data.FramebufferScale = ImVec2( 1.0f, 1.0f );
			data.AddDrawList( &list );

			IDirect3DSurface9 *savedRt = nullptr;
			IDirect3DSurface9 *savedDs = nullptr;
			D3DVIEWPORT9 savedVp{};
			device->GetRenderTarget( 0, &savedRt );
			device->GetDepthStencilSurface( &savedDs );
			device->GetViewport( &savedVp );
			device->SetRenderTarget( 0, target );
			device->SetDepthStencilSurface( nullptr );
			device->Clear( 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB( 0, 0, 0, 0 ), 1.0f, 0 );
			ImGui_ImplDX9_RenderDrawData( &data );
			device->SetRenderTarget( 0, savedRt );
			device->SetDepthStencilSurface( savedDs );
			device->SetViewport( &savedVp );
			if( savedRt )
			{
				savedRt->Release();
			}
			if( savedDs )
			{
				savedDs->Release();
			}
			s_drawn = true;
		}

		void Submit( OpenXRRuntime *runtime, D3D11Bridge *bridge )
		{
			if( !runtime || !bridge || !s_drawn || !bridge->LabelTexture() )
			{
				return;
			}
			s_drawn = false;
			bool visible[ 2 ];
			for( int hand = 0; hand < 2; ++hand )
			{
				visible[ hand ] = s_posed[ hand ] && s_alpha[ hand ] > 0.0f;
			}
			if( !visible[ 0 ] && !visible[ 1 ] )
			{
				return;
			}
			const float size = Config::Get().vr.m_controllerLabelsSize > 0.05f ? Config::Get().vr.m_controllerLabelsSize : 0.05f;
			runtime->SubmitHandLabels( bridge->LabelTexture(), s_position, s_orientation, size, visible );
		}
	}
}
