#include "VRSyntheticPad.h"

#include "VRBindings.h"
#include "VRControllers.h"
#include "VRMotionLayout.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"
#include "../PSUStructs/PSUCombat.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include <cmath>
#include <cstring>
#include <mutex>

namespace PSUVR
{
	namespace
	{
		// Logical PSU pad buttons, in PsuIlluminus.ini [gamepad1] order.
		enum PadButton : int
		{
			kPadA, kPadB, kPadX, kPadY, kPadL1, kPadR1, kPadL2, kPadR2, kPadL3, kPadR3, kPadSelect, kPadStart,
			kPadUp, kPadRight, kPadDown, kPadLeft,
			kPadButtonCount
		};
		const char *const kIniKeys[ kPadButtonCount ] = {
			"BUTTON_A", "BUTTON_B", "BUTTON_X", "BUTTON_Y", "BUTTON_L1", "BUTTON_R1", "BUTTON_L2", "BUTTON_R2",
			"BUTTON_L3", "BUTTON_R3", "BUTTON_SELECT", "BUTTON_START", "BUTTON_U", "BUTTON_R", "BUTTON_D", "BUTTON_L" };

		constexpr uint32_t Bit( int button ) { return 1u << button; }

		struct PadState
		{
			uint32_t buttons = 0;
			float lx = 0.0f, ly = 0.0f;  // -1..1, +y = stick pushed up
			float rx = 0.0f, ry = 0.0f;
		};

		// [gamepad1]: DirectInput button index per logical button (-1 = none; the D-pad
		// uses the POV hat when its entries are 0), and the right stick's axes.
		struct PadMap
		{
			int button[ kPadButtonCount ];
			int rightX = 3;  // lRx
			int rightY = 4;  // lRy
		};

		std::mutex s_mutex;
		PadState s_out;           // read by MergeJoystickState (game input thread)
		bool s_active = false;
		DWORD s_updateTick = 0;
		PadMap s_map{};
		bool s_mapLoaded = false;
		FILETIME s_iniTime{};
		DWORD s_iniCheckTick = 0;
		unsigned s_actions = 0;
		bool s_loggedMerge = false;

		LARGE_INTEGER s_lastTime{};

		// Grip handling: a press waits before it is sent, so both-grip chords don't also
		// press L1 / R1 in the game. Consumed: the grip was part of another combination
		// (left grip + stick click) and sends nothing until released.
		enum class Grip { Idle, Pending, Held, Modifier, Consumed };
		Grip s_grip[ 2 ] = { Grip::Idle, Grip::Idle };
		float s_gripTime[ 2 ]{};
		float s_pulse[ 2 ]{};     // short L1 / R1 press left after a quick tap
		bool s_triggerDown[ 2 ]{};
		bool s_comboWindow = false;       // last frame's PSUCombat::ComboWindowOpen
		int s_comboStep = 0;
		bool s_combatValid = false;
		PSUCombat::State s_combat;

		// Swing to attack: a fast right-hand swing presses X (once per swing; re-arms when
		// the hand slows down). Swings during the attack animation wait for the window.
		bool s_swingArmed = true;
		int s_swingFrames = 0;
		float s_swingPending = 0.0f;  // seconds a buffered swing may still wait
		float s_attackPulse = 0.0f;   // seconds X stays pressed
		constexpr float kSwingRearmSpeed = 0.8f;
		constexpr float kSwingBufferSeconds = 0.25f;
		constexpr float kAttackPulseSeconds = 0.08f;
		bool s_dpadMode = false;          // left stick sends the D-pad
		bool s_leftClickConsumed = false; // the stick click toggled the D-pad: no SELECT

		constexpr float kGripDelay = 0.15f;
		constexpr float kTapPulse = 0.06f;
		constexpr float kDpadThreshold = 0.5f;

		void IniPath( char *out, size_t size )
		{
			out[ 0 ] = '\0';
			char base[ MAX_PATH ]{};
			if( GetEnvironmentVariableA( "LOCALAPPDATA", base, MAX_PATH ) == 0 )
			{
				return;
			}
			sprintf_s( out, size, "%s\\SEGA\\PHANTASY STAR UNIVERSE Illuminus\\PsuIlluminus.ini", base );
		}

		void LoadMap()
		{
			char path[ MAX_PATH * 2 ]{};
			IniPath( path, sizeof( path ) );
			WIN32_FILE_ATTRIBUTE_DATA attributes{};
			const bool exists = path[ 0 ] && GetFileAttributesExA( path, GetFileExInfoStandard, &attributes );
			if( s_mapLoaded && exists && CompareFileTime( &attributes.ftLastWriteTime, &s_iniTime ) == 0 )
			{
				return;
			}
			s_iniTime = exists ? attributes.ftLastWriteTime : FILETIME{};
			s_mapLoaded = true;

			// The game numbers buttons from 1 (0 = unassigned / POV for the D-pad).
			static const int kDefaults[ kPadButtonCount ] = { 2, 1, 3, 4, 5, 6, 11, 12, 7, 10, 0, 8, 0, 0, 0, 0 };
			PadMap map{};
			char line[ 256 ];
			int length = 0;
			length += sprintf_s( line + length, sizeof( line ) - length, "VRSyntheticPad: [gamepad1]%s", exists ? "" : " (ini not found, defaults)" );
			for( int i = 0; i < kPadButtonCount; ++i )
			{
				const int value = exists ? static_cast< int >( GetPrivateProfileIntA( "gamepad1", kIniKeys[ i ], kDefaults[ i ], path ) ) : kDefaults[ i ];
				map.button[ i ] = value > 0 && value <= 128 ? value - 1 : -1;
				if( length < static_cast< int >( sizeof( line ) ) - 16 )
				{
					length += sprintf_s( line + length, sizeof( line ) - length, " %s=%d", kIniKeys[ i ] + 7, value );
				}
			}
			const int stickX = exists ? static_cast< int >( GetPrivateProfileIntA( "gamepad1", "STICK2_INPUT_X", 4, path ) ) : 4;
			const int stickY = exists ? static_cast< int >( GetPrivateProfileIntA( "gamepad1", "STICK2_INPUT_Y", 5, path ) ) : 5;
			// Axes numbered from 1: lX, lY, lZ, lRx, lRy, lRz, slider 0, slider 1.
			map.rightX = stickX >= 1 && stickX <= 8 ? stickX - 1 : 3;
			map.rightY = stickY >= 1 && stickY <= 8 ? stickY - 1 : 4;
			PSU_LOG( "%s STICK2=%d,%d\n", line, stickX, stickY );

			std::lock_guard lock( s_mutex );
			s_map = map;
		}

		float Deadzone( float value, float deadzone )
		{
			const float magnitude = std::fabs( value );
			if( magnitude <= deadzone )
			{
				return 0.0f;
			}
			const float scaled = ( magnitude - deadzone ) / ( 1.0f - deadzone );
			return ( value < 0.0f ? -1.0f : 1.0f ) * ( scaled > 1.0f ? 1.0f : scaled );
		}

		float FrameSeconds()
		{
			LARGE_INTEGER now{}, frequency{};
			QueryPerformanceCounter( &now );
			QueryPerformanceFrequency( &frequency );
			float dt = 0.0f;
			if( s_lastTime.QuadPart && frequency.QuadPart )
			{
				dt = static_cast< float >( now.QuadPart - s_lastTime.QuadPart ) / static_cast< float >( frequency.QuadPart );
			}
			s_lastTime = now;
			return dt < 0.0f ? 0.0f : ( dt > 0.1f ? 0.1f : dt );
		}

		void Buzz( int hand )
		{
			VRControllers::Haptic( hand, 0.5f, 0.04f );
		}

		// Combo window edges from the player's attack state: a tick in the right hand when
		// the next hit can be pressed (stronger before the last hit).
		void ComboFeedback()
		{
			PSUCombat::State combat;
			const bool valid = PSUCombat::Read( combat );
			const bool open = valid && PSUCombat::ComboWindowOpen( combat );
			if( open && !s_comboWindow && Config::Get().vr.m_comboHaptic && VRControllers::Active( 1 ) )
			{
				VRControllers::Haptic( 1, combat.comboStep >= 2 ? 0.8f : 0.5f, 0.03f );
			}
#ifdef _DEBUG
			if( open != s_comboWindow || ( valid ? combat.comboStep : 0 ) != s_comboStep )
			{
				PSU_LOG( "VRCombat: step %d window %s (state 0x%08X%s)\n", valid ? combat.comboStep : 0,
					open ? "OPEN" : "closed", valid ? combat.attackState : 0u, valid && combat.earlyPress ? ", early press" : "" );
			}
#endif
			s_comboWindow = open;
			s_comboStep = valid ? combat.comboStep : 0;
			s_combatValid = valid;
			s_combat = combat;
		}

		// Returns whether X is pressed this frame.
		bool SwingAttack( float dt, bool modifier )
		{
			const auto &vr = Config::Get().vr;
			if( s_attackPulse > 0.0f )
			{
				s_attackPulse -= dt;
				return true;
			}
			const ControllerState &right = VRControllers::State( 1 );
			if( !vr.m_swingAttack || !right.active || !right.velocityValid )
			{
				s_swingArmed = true;
				s_swingFrames = 0;
				s_swingPending = 0.0f;
				return false;
			}
			const float *v = right.linearVelocity;
			const float speed = std::sqrt( v[ 0 ] * v[ 0 ] + v[ 1 ] * v[ 1 ] + v[ 2 ] * v[ 2 ] );
			// Presses are early while the swing animation plays (state 0x02).
			const bool early = s_combatValid && PSUCombat::Swinging( s_combat );

			if( speed < kSwingRearmSpeed )
			{
				s_swingArmed = true;
				s_swingFrames = 0;
			}
			else if( s_swingArmed && !modifier && speed > vr.m_swingSpeed && ++s_swingFrames >= 2 )
			{
				s_swingArmed = false;
				VRControllers::Haptic( 1, 0.25f, 0.02f );
				if( early && vr.m_comboBuffer )
				{
					s_swingPending = kSwingBufferSeconds;
				}
				else
				{
					s_attackPulse = kAttackPulseSeconds;
				}
#ifdef _DEBUG
				PSU_LOG( "VRSwing: %.1f m/s, step %d, %s\n", speed, s_combatValid ? s_combat.comboStep : 0,
					s_swingPending > 0.0f ? "buffered" : ( early ? "early (sent)" : "sent" ) );
#endif
			}

			if( s_swingPending > 0.0f )
			{
				if( !early )
				{
					s_swingPending = 0.0f;
					s_attackPulse = kAttackPulseSeconds;
#ifdef _DEBUG
					PSU_LOG( "VRSwing: buffered swing sent (window %s)\n", s_comboWindow ? "open" : "idle" );
#endif
				}
				else if( ( s_swingPending -= dt ) <= 0.0f )
				{
					s_swingPending = 0.0f;
#ifdef _DEBUG
					PSU_LOG( "VRSwing: buffered swing dropped (window didn't open in time)\n" );
#endif
				}
			}
			return s_attackPulse > 0.0f;
		}

		using VRMotionLayout::Control;
		using VRMotionLayout::Output;

		// Digital state of one layout control.
		bool ControlDown( const ControllerState &c, int hand, Control control )
		{
			switch( control )
			{
				case Control::Trigger: return s_triggerDown[ hand ];
				case Control::StickClick: return ( c.buttons & kControllerStick ) != 0;
				case Control::Primary: return ( c.buttons & kControllerPrimary ) != 0;
				case Control::Secondary: return ( c.buttons & kControllerSecondary ) != 0;
				default: return false;
			}
		}

		uint32_t ControlBit( Control control )
		{
			switch( control )
			{
				case Control::StickClick: return kControllerStick;
				case Control::Primary: return kControllerPrimary;
				case Control::Secondary: return kControllerSecondary;
				default: return 0;
			}
		}

		int PadButtonFor( Output output )
		{
			switch( output )
			{
				case Output::A: return kPadA;
				case Output::B: return kPadB;
				case Output::X: return kPadX;
				case Output::Y: return kPadY;
				case Output::L1: return kPadL1;
				case Output::R1: return kPadR1;
				case Output::L2: return kPadL2;
				case Output::R2: return kPadR2;
				case Output::L3: return kPadL3;
				case Output::R3: return kPadR3;
				case Output::Select: return kPadSelect;
				case Output::Start: return kPadStart;
				default: return -1;
			}
		}

		int ActionFor( Output output )
		{
			switch( output )
			{
				case Output::Recenter: return static_cast< int >( VRAction::Recenter );
				case Output::ToggleCamera: return static_cast< int >( VRAction::ToggleCameraMode );
				case Output::ToggleSettings: return static_cast< int >( VRAction::ToggleSettings );
				case Output::TogglePause: return static_cast< int >( VRAction::TogglePause );
				default: return -1;
			}
		}

		// One grip's state machine. Returns whether L1 / R1 is pressed this frame.
		bool UpdateGrip( int hand, float dt, bool otherGripDown )
		{
			const bool down = ( VRControllers::State( hand ).buttons & kControllerGrip ) != 0;
			Grip &grip = s_grip[ hand ];
			Grip &other = s_grip[ hand ^ 1 ];
			if( s_pulse[ hand ] > 0.0f )
			{
				s_pulse[ hand ] -= dt;
			}
			switch( grip )
			{
				case Grip::Idle:
					if( down )
					{
						grip = Grip::Pending;
						s_gripTime[ hand ] = 0.0f;
						// The other grip went down just before: a VR chord.
						if( other == Grip::Pending )
						{
							grip = Grip::Modifier;
							other = Grip::Modifier;
							Buzz( 0 );
							Buzz( 1 );
						}
					}
					break;
				case Grip::Pending:
					s_gripTime[ hand ] += dt;
					if( !down )
					{
						grip = Grip::Idle;
						s_pulse[ hand ] = kTapPulse;
					}
					else if( s_gripTime[ hand ] >= kGripDelay )
					{
						grip = Grip::Held;
					}
					break;
				case Grip::Held:
				case Grip::Consumed:
					if( !down )
					{
						grip = Grip::Idle;
					}
					break;
				case Grip::Modifier:
					// The chord lasts until both grips are released.
					if( !down && !otherGripDown )
					{
						grip = Grip::Idle;
						other = Grip::Idle;
					}
					break;
			}
			return grip == Grip::Held || s_pulse[ hand ] > 0.0f;
		}

		uint32_t DpadButtons( float x, float y )
		{
			uint32_t buttons = 0;
			if( x * x + y * y < kDpadThreshold * kDpadThreshold )
			{
				return 0;
			}
			// 8-way: a direction counts within 67.5 degrees of its axis.
			const float angle = std::atan2( y, x );  // 0 = right, +pi/2 = up
			constexpr float kHalfSector = 67.5f * ( 3.14159265f / 180.0f );
			const auto within = [ & ]( float target )
			{
				float d = angle - target;
				while( d > 3.14159265f ) d -= 6.2831853f;
				while( d < -3.14159265f ) d += 6.2831853f;
				return std::fabs( d ) < kHalfSector;
			};
			if( within( 0.0f ) ) buttons |= Bit( kPadRight );
			if( within( 1.5707963f ) ) buttons |= Bit( kPadUp );
			if( within( 3.14159265f ) ) buttons |= Bit( kPadLeft );
			if( within( -1.5707963f ) ) buttons |= Bit( kPadDown );
			return buttons;
		}

		LONG AxisValue( float value, LONG minimum, LONG maximum )
		{
			const float center = ( static_cast< float >( minimum ) + static_cast< float >( maximum ) ) * 0.5f;
			const float half = ( static_cast< float >( maximum ) - static_cast< float >( minimum ) ) * 0.5f;
			return static_cast< LONG >( std::lround( center + value * half ) );
		}

		// Larger deflection from the centre wins (so a physical pad still works alongside).
		void MergeAxis( LONG &axis, float value, LONG minimum, LONG maximum )
		{
			if( value == 0.0f || maximum <= minimum )
			{
				return;
			}
			const LONG ours = AxisValue( value, minimum, maximum );
			const double center = ( static_cast< double >( minimum ) + static_cast< double >( maximum ) ) * 0.5;
			if( std::fabs( ours - center ) > std::fabs( axis - center ) )
			{
				axis = ours;
			}
		}

		LONG &AxisRef( DIJOYSTATE2 &state, int index )
		{
			switch( index )
			{
				case 0: return state.lX;
				case 1: return state.lY;
				case 2: return state.lZ;
				case 3: return state.lRx;
				case 4: return state.lRy;
				case 5: return state.lRz;
				case 6: return state.rglSlider[ 0 ];
				default: return state.rglSlider[ 1 ];
			}
		}
	}

	namespace VRSyntheticPad
	{
		void Update()
		{
			const float dt = FrameSeconds();
			// Also with the gamepad: the buzz needs only an active right controller.
			ComboFeedback();
			const bool motion = Config::Get().vr.m_inputMode == "motion";
			if( !motion || !VRControllers::AnyActive() )
			{
				s_grip[ 0 ] = s_grip[ 1 ] = Grip::Idle;
				s_pulse[ 0 ] = s_pulse[ 1 ] = 0.0f;
				s_dpadMode = false;
				s_leftClickConsumed = false;
				std::lock_guard lock( s_mutex );
				s_active = false;
				s_out = {};
				return;
			}
			if( !s_mapLoaded || GetTickCount() - s_iniCheckTick > 3000 )
			{
				s_iniCheckTick = GetTickCount();
				LoadMap();
			}

			const ControllerState &left = VRControllers::State( 0 );
			const ControllerState &right = VRControllers::State( 1 );
			const bool leftGripDown = ( left.buttons & kControllerGrip ) != 0;
			const bool rightGripDown = ( right.buttons & kControllerGrip ) != 0;
			const ControllerState *hands[ 2 ] = { &left, &right };
			// Triggers with a little hysteresis.
			for( int hand = 0; hand < 2; ++hand )
			{
				const float value = hands[ hand ]->trigger;
				s_triggerDown[ hand ] = s_triggerDown[ hand ] ? value > 0.4f : value > 0.6f;
			}

			// Left grip + left stick click toggles the D-pad; that grip and click send nothing.
			if( !s_leftClickConsumed && ( VRControllers::Pressed( 0 ) & kControllerStick ) && leftGripDown &&
				s_grip[ 0 ] != Grip::Modifier && s_grip[ 1 ] != Grip::Modifier )
			{
				s_dpadMode = !s_dpadMode;
				s_leftClickConsumed = true;
				s_grip[ 0 ] = Grip::Consumed;
				s_pulse[ 0 ] = 0.0f;
				VRBindings::ShowToast( s_dpadMode ? "D-pad on" : "D-pad off" );
				Buzz( 0 );
			}
			if( s_leftClickConsumed && !( left.buttons & kControllerStick ) )
			{
				s_leftClickConsumed = false;
			}

			const bool l1 = UpdateGrip( 0, dt, rightGripDown );
			const bool r1 = UpdateGrip( 1, dt, leftGripDown );
			const bool modifier = s_grip[ 0 ] == Grip::Modifier;

			PadState pad;
			if( SwingAttack( dt, modifier ) ) pad.buttons |= Bit( kPadX );
			if( l1 && !modifier ) pad.buttons |= Bit( kPadL1 );
			if( r1 && !modifier ) pad.buttons |= Bit( kPadR1 );
			for( const VRMotionLayout::Entry &entry : VRMotionLayout::kLayout )
			{
				if( entry.control == Control::Grip || entry.control == Control::Stick )
				{
					continue;
				}
				const ControllerState &c = *hands[ entry.hand ];
				if( entry.hand == 0 && entry.control == Control::StickClick && s_leftClickConsumed )
				{
					continue;
				}
				const Output output = modifier ? entry.chord : entry.plain;
				const int button = PadButtonFor( output );
				if( button >= 0 )
				{
					if( ControlDown( c, entry.hand, entry.control ) )
					{
						pad.buttons |= Bit( button );
					}
					continue;
				}
				// VR actions fire once, on the press.
				const int action = ActionFor( output );
				if( action >= 0 && ( VRControllers::Pressed( entry.hand ) & ControlBit( entry.control ) ) )
				{
					s_actions |= 1u << action;
					Buzz( 0 );
					Buzz( 1 );
				}
			}
			if( s_dpadMode )
			{
				pad.buttons |= DpadButtons( left.stick[ 0 ], left.stick[ 1 ] );
			}
			else
			{
				pad.lx = Deadzone( left.stick[ 0 ], 0.15f );
				pad.ly = Deadzone( left.stick[ 1 ], 0.15f );
			}
			pad.rx = Deadzone( right.stick[ 0 ], 0.15f );
			pad.ry = Deadzone( right.stick[ 1 ], 0.15f );

			std::lock_guard lock( s_mutex );
			s_out = pad;
			s_active = true;
			s_updateTick = GetTickCount();
		}

		bool Active()
		{
			std::lock_guard lock( s_mutex );
			// Stale when VR frames stop (runtime paused or lost).
			return s_active && GetTickCount() - s_updateTick < 250;
		}

		unsigned TakeActions()
		{
			const unsigned actions = s_actions;
			s_actions = 0;
			return actions;
		}

		bool MergeJoystickState( void *statePtr, unsigned size, const long axisMin[ 8 ], const long axisMax[ 8 ], bool replace )
		{
			if( !statePtr || size != sizeof( DIJOYSTATE2 ) || !Active() )
			{
				return false;
			}
			PadState pad;
			PadMap map;
			{
				std::lock_guard lock( s_mutex );
				pad = s_out;
				map = s_map;
			}
			DIJOYSTATE2 &state = *static_cast< DIJOYSTATE2 * >( statePtr );
			if( replace )
			{
				// The device is a VR controller seen through DirectInput: its own buttons
				// and axes would be a second, differently mapped copy of the same input.
				std::memset( &state, 0, sizeof( state ) );
				for( int i = 0; i < 8; ++i )
				{
					AxisRef( state, i ) = AxisValue( 0.0f, axisMin[ i ], axisMax[ i ] );
				}
				for( DWORD &pov : state.rgdwPOV )
				{
					pov = 0xFFFFFFFFu;
				}
			}

			// DirectInput Y grows downward.
			MergeAxis( state.lX, pad.lx, axisMin[ 0 ], axisMax[ 0 ] );
			MergeAxis( state.lY, -pad.ly, axisMin[ 1 ], axisMax[ 1 ] );
			MergeAxis( AxisRef( state, map.rightX ), pad.rx, axisMin[ map.rightX ], axisMax[ map.rightX ] );
			MergeAxis( AxisRef( state, map.rightY ), -pad.ry, axisMin[ map.rightY ], axisMax[ map.rightY ] );

			for( int i = 0; i < kPadButtonCount; ++i )
			{
				if( ( pad.buttons & Bit( i ) ) && map.button[ i ] >= 0 && map.button[ i ] < 128 )
				{
					state.rgbButtons[ map.button[ i ] ] = 0x80;
				}
			}
			// D-pad on the POV hat unless [gamepad1] gives it buttons.
			const bool up = ( pad.buttons & Bit( kPadUp ) ) && map.button[ kPadUp ] < 0;
			const bool down = ( pad.buttons & Bit( kPadDown ) ) && map.button[ kPadDown ] < 0;
			const bool leftDir = ( pad.buttons & Bit( kPadLeft ) ) && map.button[ kPadLeft ] < 0;
			const bool rightDir = ( pad.buttons & Bit( kPadRight ) ) && map.button[ kPadRight ] < 0;
			if( ( up || down || leftDir || rightDir ) && LOWORD( state.rgdwPOV[ 0 ] ) == 0xFFFF )
			{
				const int dx = ( rightDir ? 1 : 0 ) - ( leftDir ? 1 : 0 );
				const int dy = ( up ? 1 : 0 ) - ( down ? 1 : 0 );
				// Hundredths of a degree clockwise from north.
				static const DWORD kPov[ 3 ][ 3 ] = {
					{ 22500, 18000, 13500 },   // dy = -1: down-left, down, down-right
					{ 27000, 0xFFFFFFFFu, 9000 },
					{ 31500, 0, 4500 } };      // dy = +1: up-left, up, up-right
				state.rgdwPOV[ 0 ] = kPov[ dy + 1 ][ dx + 1 ];
			}

			if( !s_loggedMerge )
			{
				s_loggedMerge = true;
				PSU_LOG( "VRSyntheticPad: merging motion controllers into the game's pad (replace=%d, range X %ld..%ld)\n",
					replace ? 1 : 0, axisMin[ 0 ], axisMax[ 0 ] );
			}
			return true;
		}
	}
}
