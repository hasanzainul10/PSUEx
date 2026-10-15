#include "VRGamepad.h"

#include "../Misc/Log.h"

#include <Windows.h>
#include <Xinput.h>

namespace PSUVR
{
	namespace
	{
		typedef DWORD( WINAPI *XInputGetStateFn )( DWORD, XINPUT_STATE * );

		XInputGetStateFn s_getState = nullptr;
		bool s_triedLoad = false;
		DWORD s_lastUser = 0;

		bool EnsureXInput()
		{
			if( s_triedLoad )
			{
				return s_getState != nullptr;
			}
			s_triedLoad = true;
			// Same DLL order as the ImGui Win32 backend.
			const char *names[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "xinput1_2.dll", "xinput1_1.dll" };
			for( const char *name : names )
			{
				if( HMODULE module = LoadLibraryA( name ) )
				{
					s_getState = reinterpret_cast< XInputGetStateFn >( GetProcAddress( module, "XInputGetState" ) );
					if( s_getState )
					{
						PSU_LOG( "VRGamepad: using %s\n", name );
						return true;
					}
				}
			}
			PSU_LOG( "VRGamepad: no XInput DLL found\n" );
			return false;
		}
	}

	namespace
	{
		// -1..1 past the standard right-thumb deadzone, rescaled so motion starts at 0.
		float AxisPastDeadzone( short value )
		{
			const float raw = static_cast< float >( value );
			const float deadzone = static_cast< float >( XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE );
			const float magnitude = raw < 0.0f ? -raw : raw;
			if( magnitude <= deadzone )
			{
				return 0.0f;
			}
			const float scaled = ( magnitude - deadzone ) / ( 32767.0f - deadzone );
			return ( raw < 0.0f ? -1.0f : 1.0f ) * ( scaled > 1.0f ? 1.0f : scaled );
		}
	}

	namespace VRGamepad
	{
		bool Buttons( unsigned short &buttons )
		{
			buttons = 0;
			if( !EnsureXInput() )
			{
				return false;
			}
			for( DWORD i = 0; i < XUSER_MAX_COUNT; ++i )
			{
				const DWORD user = ( s_lastUser + i ) % XUSER_MAX_COUNT;
				XINPUT_STATE state{};
				if( s_getState( user, &state ) == ERROR_SUCCESS )
				{
					s_lastUser = user;
					buttons = state.Gamepad.wButtons;
					return true;
				}
			}
			return false;
		}

		bool RightStick( float &x, float &y )
		{
			x = 0.0f;
			y = 0.0f;
			if( !EnsureXInput() )
			{
				return false;
			}
			// Try the last pad that answered first, then the rest.
			for( DWORD i = 0; i < XUSER_MAX_COUNT; ++i )
			{
				const DWORD user = ( s_lastUser + i ) % XUSER_MAX_COUNT;
				XINPUT_STATE state{};
				if( s_getState( user, &state ) != ERROR_SUCCESS )
				{
					continue;
				}
				s_lastUser = user;
				x = AxisPastDeadzone( state.Gamepad.sThumbRX );
				y = AxisPastDeadzone( state.Gamepad.sThumbRY );
				return true;
			}
			return false;
		}

		bool RightStickX( float &x )
		{
			float y = 0.0f;
			return RightStick( x, y );
		}
	}
}
