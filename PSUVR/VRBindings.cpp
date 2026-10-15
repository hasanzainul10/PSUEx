#include "VRBindings.h"

#include "VRCamera.h"
#include "VRGamepad.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <Windows.h>
#include <Xinput.h>

namespace PSUVR
{
	namespace
	{
		bool s_keyWasDown[ kVRActionCount ]{};
		unsigned short s_padPrevious = 0;
		const char *s_toast = nullptr;
		DWORD s_toastTick = 0;
		bool s_settingsRequested = false;

		constexpr DWORD kToastMs = 1500;
		constexpr DWORD kToastFadeMs = 400;
	}

	namespace VRBindings
	{
		unsigned Poll( bool windowFocused )
		{
			const auto &bindings = Config::Get().vr.m_bindings;
			unsigned short buttons = 0;
			VRGamepad::Buttons( buttons );
			const bool backHeld = ( buttons & XINPUT_GAMEPAD_BACK ) != 0;
			const unsigned short newlyPressed = static_cast< unsigned short >( buttons & ~s_padPrevious );
			s_padPrevious = buttons;

			uint8_t mods = 0;
			if( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) mods |= kVRModCtrl;
			if( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) mods |= kVRModShift;
			if( GetAsyncKeyState( VK_MENU ) & 0x8000 ) mods |= kVRModAlt;

			unsigned actions = 0;
			for( int i = 0; i < kVRActionCount; ++i )
			{
				const VRBinding &binding = bindings[ i ];
				// Exact modifiers: Ctrl+F7 doesn't fire on plain F7 (a game shortcut) and back.
				const bool keyDown = windowFocused && binding.key > 0 && mods == binding.mods &&
					( GetAsyncKeyState( binding.key ) & 0x8000 ) != 0;
				const bool keyEdge = keyDown && !s_keyWasDown[ i ];
				s_keyWasDown[ i ] = keyDown;
				const bool padEdge = backHeld && binding.pad != 0 && ( newlyPressed & binding.pad ) != 0;
				if( keyEdge || padEdge )
				{
					actions |= 1u << i;
				}
			}
			return actions;
		}

		void ApplyToggles( unsigned actions )
		{
			auto &vr = Config::Get().vr;
			if( actions & Bit( VRAction::ToggleCameraMode ) )
			{
				const bool toFirst = vr.m_cameraMode != "firstPerson";
				vr.m_cameraMode = toFirst ? "firstPerson" : "thirdPerson";
				// Re-prime first person from the camera so the switch doesn't jump.
				VRCamera::ClearFirstPerson();
				ShowToast( toFirst ? "First person" : "Third person" );
			}
			if( actions & Bit( VRAction::TogglePause ) )
			{
				vr.m_paused = !vr.m_paused;
				ShowToast( vr.m_paused ? "VR paused" : "VR resumed" );
			}
			if( actions & Bit( VRAction::ToggleSnapTurn ) )
			{
				// Each camera mode has its own turn setting.
				if( vr.m_cameraMode == "firstPerson" )
				{
					vr.m_snapTurn = !vr.m_snapTurn;
					ShowToast( vr.m_snapTurn ? "First person: snap turn" : "First person: smooth turn" );
				}
				else
				{
					vr.m_tpSnapTurn = !vr.m_tpSnapTurn;
					ShowToast( vr.m_tpSnapTurn ? "Third person: snap turn" : "Third person: smooth turn" );
				}
			}
			if( actions & Bit( VRAction::ToggleHudFollow ) )
			{
				vr.m_hudFollow = !vr.m_hudFollow;
				ShowToast( vr.m_hudFollow ? "HUD: body-locked" : "HUD: head-locked" );
			}
			if( actions & Bit( VRAction::ToggleSettings ) )
			{
				s_settingsRequested = true;
			}
		}

		void ShowToast( const char *text )
		{
			s_toast = text;
			s_toastTick = GetTickCount();
			PSU_LOG( "VR: %s\n", text );
		}

		const char *CurrentToast( float &alpha )
		{
			alpha = 0.0f;
			if( !s_toast )
			{
				return nullptr;
			}
			const DWORD age = GetTickCount() - s_toastTick;
			if( age >= kToastMs )
			{
				s_toast = nullptr;
				return nullptr;
			}
			const DWORD fadeStart = kToastMs - kToastFadeMs;
			alpha = age < fadeStart ? 1.0f : 1.0f - static_cast< float >( age - fadeStart ) / kToastFadeMs;
			return s_toast;
		}

		bool ConsumeSettingsRequest()
		{
			const bool requested = s_settingsRequested;
			s_settingsRequested = false;
			return requested;
		}
	}
}
