#include "VRControllerSim.h"

#include "VRBindings.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <Windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace PSUVR
{
	namespace
	{
		constexpr float kPi = 3.14159265f;

		struct V3
		{
			float x, y, z;
		};

		V3 Add( const V3 &a, const V3 &b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		V3 Sub( const V3 &a, const V3 &b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		V3 Scale( const V3 &a, float s ) { return { a.x * s, a.y * s, a.z * s }; }
		float Length( const V3 &a ) { return std::sqrt( a.x * a.x + a.y * a.y + a.z * a.z ); }

		// Hand poses relative to the head, in its yaw-only frame (OpenXR: +X right, +Y up,
		// -Z forward), meters; pitch tips the controller up (+) or down (-). The left hand is
		// the mirror of the right.
		struct Preset
		{
			const char *name;
			V3 right;
			float pitch;
		};
		const Preset kPresets[] = {
			{ "rest", { 0.20f, -0.65f, -0.05f }, -60.0f },
			{ "forward", { 0.18f, -0.30f, -0.45f }, 0.0f },
			{ "gun", { 0.10f, -0.15f, -0.50f }, 0.0f },     // in view: free aim engages
			{ "raised", { 0.20f, 0.25f, -0.15f }, 60.0f },
			{ "calib", { 0.12f, -0.30f, -0.55f }, 0.0f },   // fists forward, palms facing
		};
		constexpr int kPresetCount = static_cast< int >( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

		// Presets move slower than the swing threshold (vr.swingSpeed); a swing moves faster.
		constexpr float kEaseSpeed = 1.0f;       // m/s
		constexpr float kSwingSeconds = 0.15f;
		const V3 kSwingFrom{ 0.0f, 0.30f, 0.05f };
		const V3 kSwingTo{ 0.0f, -0.30f, -0.25f };

		enum Button { kTrigger, kGrip, kStickClick, kPrimary, kSecondary, kButtonCount };
		const uint32_t kButtonBits[ kButtonCount ] = { kControllerTrigger, kControllerGrip, kControllerStick, kControllerPrimary,
			kControllerSecondary };
		const char *const kButtonNames[ kButtonCount ] = { "trigger", "grip", "stick", "primary", "secondary" };

		struct Hand
		{
			V3 offset{};            // current, head frame
			V3 target{};
			float pitch = 0.0f;     // degrees
			float targetPitch = 0.0f;
			bool held[ kButtonCount ]{};       // hold commands
			bool keyHeld[ kButtonCount ]{};    // hotkeys, this frame
			float pressLeft[ kButtonCount ]{}; // timed presses, seconds left
			float stick[ 2 ]{};
			float stickLeft = 0.0f;
			float keyStick[ 2 ]{};
			float swingTime = -1.0f;           // < 0: not swinging
			V3 world{};
			V3 prevWorld{};
			bool havePrev = false;
			V3 velocity{};
		};

		Hand s_hands[ 2 ];
		bool s_initialised = false;
		bool s_wasActive = false;
		LARGE_INTEGER s_lastTime{};
		int s_keyHands = 3;  // hotkeys act on: 1 left, 2 right, 3 both

		// Head (eye midpoint and yaw) from the next views; kept when they aren't available.
		V3 s_head{ 0.0f, 0.0f, 0.0f };
		float s_headYaw = 0.0f;

		// Script runner.
		std::vector< std::string > s_script;
		size_t s_scriptLine = 0;
		float s_scriptWait = 0.0f;
		bool s_scriptRunning = false;
		std::filesystem::file_time_type s_scriptTime{};
		bool s_haveScriptTime = false;
		DWORD s_lastScriptCheck = 0;

		// Chord command: both grips, then a control on one or both hands.
		float s_chordLeft = 0.0f;
		float s_chordPressAt = 0.0f;
		int s_chordHands = 0;
		int s_chordButton = -1;
		bool s_chordPressed = false;

		unsigned s_hapticLogs = 0;
		DWORD s_hapticLogTick = 0;

		void ApplyPreset( int hand, const Preset &preset )
		{
			const float side = hand == 0 ? -1.0f : 1.0f;
			s_hands[ hand ].target = { preset.right.x * side, preset.right.y, preset.right.z };
			s_hands[ hand ].targetPitch = preset.pitch;
		}

		void InitHands()
		{
			for( int hand = 0; hand < 2; ++hand )
			{
				s_hands[ hand ] = Hand{};
				ApplyPreset( hand, kPresets[ 1 ] );
				s_hands[ hand ].offset = s_hands[ hand ].target;
				s_hands[ hand ].pitch = s_hands[ hand ].targetPitch;
			}
			s_initialised = true;
		}

		const Preset *FindPreset( const std::string &name )
		{
			for( const Preset &p : kPresets )
			{
				if( name == p.name )
				{
					return &p;
				}
			}
			return nullptr;
		}

		int FindButton( const std::string &name )
		{
			for( int b = 0; b < kButtonCount; ++b )
			{
				if( name == kButtonNames[ b ] )
				{
					return b;
				}
			}
			return -1;
		}

		// "left" 1, "right" 2, "both" 3, else 0.
		int ParseHands( const std::string &name )
		{
			return name == "left" ? 1 : ( name == "right" ? 2 : ( name == "both" ? 3 : 0 ) );
		}

		template< typename Fn >
		void ForHands( int mask, Fn &&fn )
		{
			for( int hand = 0; hand < 2; ++hand )
			{
				if( mask & ( 1 << hand ) )
				{
					fn( hand );
				}
			}
		}

		void StartSwing( int hand )
		{
			s_hands[ hand ].swingTime = 0.0f;
		}

		void SetActive( bool on )
		{
			Config::Get().vr.m_emulateControllers = on;
			VRBindings::ShowToast( on ? "Emulated controllers on" : "Emulated controllers off" );
			PSU_LOG( "VRSim: emulated controllers %s\n", on ? "on" : "off" );
		}

		// ---- Hotkeys (Ctrl + numpad; sticks on Ctrl + T/G/F/H, which the game doesn't bind).

		bool KeyDown( int vk )
		{
			return ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
		}

		bool KeyEdge( int vk )
		{
			static bool s_prev[ 256 ]{};
			const bool down = KeyDown( vk );
			const bool edge = down && !s_prev[ vk & 0xFF ];
			s_prev[ vk & 0xFF ] = down;
			return edge;
		}

		void Hotkeys()
		{
			const bool ctrl = KeyDown( VK_CONTROL );
			// Edges are tracked even without Ctrl, so releasing Ctrl first doesn't fire later.
			const bool toggle = KeyEdge( VK_F9 );
			if( ctrl && toggle )
			{
				SetActive( !Config::Get().vr.m_emulateControllers );
			}
			if( !Config::Get().vr.m_emulateControllers )
			{
				return;
			}
			const bool selectLeft = KeyEdge( VK_NUMPAD7 );
			const bool selectBoth = KeyEdge( VK_NUMPAD8 );
			const bool selectRight = KeyEdge( VK_NUMPAD9 );
			const bool swing = KeyEdge( VK_NUMPAD6 );
			bool presets[ kPresetCount ]{};
			for( int i = 0; i < kPresetCount; ++i )
			{
				presets[ i ] = KeyEdge( VK_NUMPAD1 + i );
			}
			for( Hand &h : s_hands )
			{
				std::memset( h.keyHeld, 0, sizeof( h.keyHeld ) );
				h.keyStick[ 0 ] = h.keyStick[ 1 ] = 0.0f;
			}
			if( !ctrl )
			{
				return;
			}
			if( selectLeft || selectBoth || selectRight )
			{
				s_keyHands = selectLeft ? 1 : ( selectRight ? 2 : 3 );
				VRBindings::ShowToast( s_keyHands == 1 ? "Emulator keys: left hand" : ( s_keyHands == 2 ? "Emulator keys: right hand"
					: "Emulator keys: both hands" ) );
			}
			for( int i = 0; i < kPresetCount; ++i )
			{
				if( presets[ i ] )
				{
					ForHands( s_keyHands, [ & ]( int hand ) { ApplyPreset( hand, kPresets[ i ] ); } );
					PSU_LOG( "VRSim: key pose %s\n", kPresets[ i ].name );
				}
			}
			if( swing )
			{
				ForHands( s_keyHands, StartSwing );
				PSU_LOG( "VRSim: key swing\n" );
			}
			const struct
			{
				int vk;
				int button;
			} held[] = { { VK_NUMPAD0, kTrigger }, { VK_DECIMAL, kGrip }, { VK_DIVIDE, kPrimary }, { VK_ADD, kSecondary },
				{ VK_SUBTRACT, kStickClick } };
			for( const auto &k : held )
			{
				if( KeyDown( k.vk ) )
				{
					ForHands( s_keyHands, [ & ]( int hand ) { s_hands[ hand ].keyHeld[ k.button ] = true; } );
				}
			}
			// Num*: both grips (the VR chord modifier).
			if( KeyDown( VK_MULTIPLY ) )
			{
				s_hands[ 0 ].keyHeld[ kGrip ] = s_hands[ 1 ].keyHeld[ kGrip ] = true;
			}
			const float sx = ( KeyDown( 'H' ) ? 1.0f : 0.0f ) - ( KeyDown( 'F' ) ? 1.0f : 0.0f );
			const float sy = ( KeyDown( 'T' ) ? 1.0f : 0.0f ) - ( KeyDown( 'G' ) ? 1.0f : 0.0f );
			ForHands( s_keyHands, [ & ]( int hand ) {
				s_hands[ hand ].keyStick[ 0 ] = sx;
				s_hands[ hand ].keyStick[ 1 ] = sy;
			} );
		}

		// ---- Script.

		std::filesystem::path ScriptPath()
		{
			std::filesystem::path path( Config::Get().vr.m_simScript );
			if( path.is_relative() )
			{
				wchar_t exe[ MAX_PATH ]{};
				GetModuleFileNameW( nullptr, exe, MAX_PATH );
				path = std::filesystem::path( exe ).parent_path() / path;
			}
			return path;
		}

		void CheckScript()
		{
			if( GetTickCount() - s_lastScriptCheck < 1000 )
			{
				return;
			}
			s_lastScriptCheck = GetTickCount();
			std::error_code error;
			const std::filesystem::path path = ScriptPath();
			const auto time = std::filesystem::last_write_time( path, error );
			if( error || ( s_haveScriptTime && time == s_scriptTime ) )
			{
				return;
			}
			const bool first = !s_haveScriptTime;
			s_scriptTime = time;
			s_haveScriptTime = true;
			// A script already there at startup is not run; only a new or changed one.
			if( first && !Config::Get().vr.m_emulateControllers )
			{
				return;
			}
			std::ifstream file( path );
			if( !file )
			{
				return;
			}
			s_script.clear();
			std::string line;
			while( std::getline( file, line ) )
			{
				s_script.push_back( line );
			}
			s_scriptLine = 0;
			s_scriptWait = 0.0f;
			s_scriptRunning = true;
			PSU_LOG( "VRSim: script start (%u lines)\n", static_cast< unsigned >( s_script.size() ) );
		}

		std::vector< std::string > Split( const std::string &line )
		{
			std::vector< std::string > words;
			size_t i = 0;
			while( i < line.size() )
			{
				while( i < line.size() && ( line[ i ] == ' ' || line[ i ] == '\t' || line[ i ] == '\r' ) )
				{
					++i;
				}
				const size_t start = i;
				while( i < line.size() && line[ i ] != ' ' && line[ i ] != '\t' && line[ i ] != '\r' )
				{
					++i;
				}
				if( i > start )
				{
					words.push_back( line.substr( start, i - start ) );
				}
			}
			return words;
		}

		float Number( const std::vector< std::string > &w, size_t i, float fallback )
		{
			return i < w.size() ? static_cast< float >( std::atof( w[ i ].c_str() ) ) : fallback;
		}

		// One command; false = unknown / bad (logged and skipped).
		bool RunCommand( const std::string &line )
		{
			const std::vector< std::string > w = Split( line );
			if( w.empty() || w[ 0 ][ 0 ] == '#' )
			{
				return true;
			}
			PSU_LOG( "VRSim: > %s\n", line.c_str() );
			const std::string &cmd = w[ 0 ];
			if( cmd == "wait" )
			{
				s_scriptWait = Number( w, 1, 0.0f );
				return true;
			}
			if( cmd == "log" )
			{
				return true;  // the "> ..." line above is the marker
			}
			if( cmd == "emulate" && w.size() >= 2 )
			{
				SetActive( w[ 1 ] == "on" );
				return true;
			}
			if( cmd == "chord" && w.size() >= 3 )
			{
				const int hands = ParseHands( w[ 1 ] );
				const int button = FindButton( w[ 2 ] );
				if( !hands || button < 0 )
				{
					return false;
				}
				s_chordHands = hands;
				s_chordButton = button;
				s_chordLeft = 0.8f;
				s_chordPressAt = 0.25f;
				s_chordPressed = false;
				return true;
			}
			if( w.size() < 2 )
			{
				return false;
			}
			const int hands = ParseHands( w[ 1 ] );
			if( !hands )
			{
				return false;
			}
			if( cmd == "pose" && w.size() >= 3 )
			{
				const Preset *preset = FindPreset( w[ 2 ] );
				if( !preset )
				{
					return false;
				}
				ForHands( hands, [ & ]( int hand ) { ApplyPreset( hand, *preset ); } );
				return true;
			}
			if( cmd == "swing" )
			{
				ForHands( hands, StartSwing );
				return true;
			}
			if( cmd == "press" && w.size() >= 3 )
			{
				const int button = FindButton( w[ 2 ] );
				if( button < 0 )
				{
					return false;
				}
				const float seconds = Number( w, 3, 0.15f );
				ForHands( hands, [ & ]( int hand ) { s_hands[ hand ].pressLeft[ button ] = seconds; } );
				return true;
			}
			if( cmd == "hold" && w.size() >= 4 )
			{
				const int button = FindButton( w[ 2 ] );
				if( button < 0 )
				{
					return false;
				}
				const bool on = w[ 3 ] == "on";
				ForHands( hands, [ & ]( int hand ) { s_hands[ hand ].held[ button ] = on; } );
				return true;
			}
			if( cmd == "stick" && w.size() >= 4 )
			{
				const float x = Number( w, 2, 0.0f );
				const float y = Number( w, 3, 0.0f );
				const float seconds = Number( w, 4, 0.5f );
				ForHands( hands, [ & ]( int hand ) {
					s_hands[ hand ].stick[ 0 ] = x;
					s_hands[ hand ].stick[ 1 ] = y;
					s_hands[ hand ].stickLeft = seconds;
				} );
				return true;
			}
			return false;
		}

		void StepScript( float dt )
		{
			if( !s_scriptRunning )
			{
				return;
			}
			if( s_scriptWait > 0.0f )
			{
				s_scriptWait -= dt;
				return;
			}
			// Run commands until a wait (or the end).
			while( s_scriptLine < s_script.size() && s_scriptWait <= 0.0f )
			{
				const std::string &line = s_script[ s_scriptLine++ ];
				if( !RunCommand( line ) )
				{
					PSU_LOG( "VRSim: bad command, skipped: %s\n", line.c_str() );
				}
			}
			if( s_scriptLine >= s_script.size() && s_scriptWait <= 0.0f )
			{
				s_scriptRunning = false;
				PSU_LOG( "VRSim: script done\n" );
			}
		}

		// ---- Motion.

		void ReadHead()
		{
			EyePose views[ 2 ];
			EyePose viewSpace[ 2 ];
			if( !System::Get().NextViews( views, viewSpace ) )
			{
				return;
			}
			s_head = { ( views[ 0 ].position[ 0 ] + views[ 1 ].position[ 0 ] ) * 0.5f,
				( views[ 0 ].position[ 1 ] + views[ 1 ].position[ 1 ] ) * 0.5f,
				( views[ 0 ].position[ 2 ] + views[ 1 ].position[ 2 ] ) * 0.5f };
			// Yaw of the head's forward (q * (0, 0, -1)), OpenXR: yaw 0 looks down -Z.
			const float *q = views[ 0 ].orientation;
			const float fx = -( 2.0f * ( q[ 0 ] * q[ 2 ] + q[ 3 ] * q[ 1 ] ) );
			const float fz = -( 1.0f - 2.0f * ( q[ 0 ] * q[ 0 ] + q[ 1 ] * q[ 1 ] ) );
			if( fx * fx + fz * fz > 1.0e-6f )
			{
				s_headYaw = std::atan2( -fx, -fz );
			}
		}

		// Head frame -> tracking space (yaw only).
		V3 ToTracking( const V3 &local )
		{
			const float s = std::sin( s_headYaw ), c = std::cos( s_headYaw );
			return Add( s_head, { c * local.x + s * local.z, local.y, -s * local.x + c * local.z } );
		}

		void Move( Hand &h, float dt )
		{
			if( h.swingTime >= 0.0f )
			{
				h.swingTime += dt;
				const float t = h.swingTime / kSwingSeconds;
				if( t >= 1.0f )
				{
					h.swingTime = -1.0f;
					h.offset = Add( h.target, kSwingTo );  // eases back from the end of the swing
				}
				else
				{
					const V3 from = Add( h.target, kSwingFrom );
					const V3 to = Add( h.target, kSwingTo );
					h.offset = Add( from, Scale( Sub( to, from ), t ) );
				}
			}
			else
			{
				const V3 d = Sub( h.target, h.offset );
				const float len = Length( d );
				const float step = kEaseSpeed * dt;
				h.offset = len <= step || len < 1.0e-5f ? h.target : Add( h.offset, Scale( d, step / len ) );
			}
			const float pitchStep = 240.0f * dt;
			const float dp = h.targetPitch - h.pitch;
			h.pitch = std::fabs( dp ) <= pitchStep ? h.targetPitch : h.pitch + ( dp > 0.0f ? pitchStep : -pitchStep );

			h.world = ToTracking( h.offset );
			if( h.havePrev && dt > 1.0e-4f )
			{
				h.velocity = Scale( Sub( h.world, h.prevWorld ), 1.0f / dt );
			}
			h.prevWorld = h.world;
			h.havePrev = true;

			for( float &left : h.pressLeft )
			{
				left = left > 0.0f ? left - dt : 0.0f;
			}
			h.stickLeft = h.stickLeft > 0.0f ? h.stickLeft - dt : 0.0f;
		}

		void StepChord( float dt )
		{
			if( s_chordLeft <= 0.0f )
			{
				return;
			}
			s_chordLeft -= dt;
			s_chordPressAt -= dt;
			if( !s_chordPressed && s_chordPressAt <= 0.0f )
			{
				s_chordPressed = true;
				ForHands( s_chordHands, [ & ]( int hand ) { s_hands[ hand ].pressLeft[ s_chordButton ] = 0.3f; } );
			}
		}

		// Yaw (head) then pitch (local X), as an x, y, z, w quaternion.
		void Orientation( float pitchDegrees, float out[ 4 ] )
		{
			const float hy = s_headYaw * 0.5f;
			const float hp = pitchDegrees * ( kPi / 180.0f ) * 0.5f;
			const float yw = std::cos( hy ), yy = std::sin( hy );  // about +Y
			const float pw = std::cos( hp ), px = std::sin( hp );  // about +X
			// q = yaw * pitch
			out[ 0 ] = yw * px;
			out[ 1 ] = yy * pw;
			out[ 2 ] = -yy * px;
			out[ 3 ] = yw * pw;
		}
	}

	namespace VRControllerSim
	{
		void Update()
		{
			if( !s_initialised )
			{
				InitHands();
			}
			LARGE_INTEGER now, frequency;
			QueryPerformanceCounter( &now );
			QueryPerformanceFrequency( &frequency );
			float dt = s_lastTime.QuadPart ? static_cast< float >( now.QuadPart - s_lastTime.QuadPart ) /
				static_cast< float >( frequency.QuadPart ) : 0.0f;
			dt = dt < 0.0f ? 0.0f : ( dt > 0.1f ? 0.1f : dt );
			s_lastTime = now;

			Hotkeys();
			CheckScript();
			const bool active = Config::Get().vr.m_emulateControllers;
			if( active != s_wasActive )
			{
				s_wasActive = active;
				for( Hand &h : s_hands )
				{
					h.havePrev = false;
					h.velocity = {};
				}
			}
			if( !active && !s_scriptRunning )
			{
				return;
			}
			StepScript( dt );
			StepChord( dt );
			ReadHead();
			for( Hand &h : s_hands )
			{
				Move( h, dt );
			}
		}

		bool Active()
		{
			return Config::Get().vr.m_emulateControllers;
		}

		void Fill( int hand, ControllerState &state )
		{
			const Hand &h = s_hands[ hand & 1 ];
			state = {};
			state.active = true;
			state.gripValid = true;
			state.aimValid = true;
			state.velocityValid = h.havePrev;
			state.gripPosition[ 0 ] = state.aimPosition[ 0 ] = h.world.x;
			state.gripPosition[ 1 ] = state.aimPosition[ 1 ] = h.world.y;
			state.gripPosition[ 2 ] = state.aimPosition[ 2 ] = h.world.z;
			Orientation( h.pitch, state.gripOrientation );
			std::memcpy( state.aimOrientation, state.gripOrientation, sizeof( state.aimOrientation ) );
			state.linearVelocity[ 0 ] = h.velocity.x;
			state.linearVelocity[ 1 ] = h.velocity.y;
			state.linearVelocity[ 2 ] = h.velocity.z;
			for( int b = 0; b < kButtonCount; ++b )
			{
				if( h.held[ b ] || h.keyHeld[ b ] || h.pressLeft[ b ] > 0.0f )
				{
					state.buttons |= kButtonBits[ b ];
				}
			}
			// The chord holds both grips for its length.
			if( s_chordLeft > 0.0f )
			{
				state.buttons |= kControllerGrip;
			}
			state.trigger = ( state.buttons & kControllerTrigger ) ? 1.0f : 0.0f;
			state.grip = ( state.buttons & kControllerGrip ) ? 1.0f : 0.0f;
			const bool scripted = h.stickLeft > 0.0f;
			state.stick[ 0 ] = scripted ? h.stick[ 0 ] : h.keyStick[ 0 ];
			state.stick[ 1 ] = scripted ? h.stick[ 1 ] : h.keyStick[ 1 ];
		}

		void NoteHaptic( int hand, float amplitude, float seconds )
		{
			if( GetTickCount() - s_hapticLogTick >= 1000 )
			{
				s_hapticLogTick = GetTickCount();
				s_hapticLogs = 0;
			}
			if( s_hapticLogs++ < 10 )
			{
				PSU_LOG( "VRSim: haptic %s %.2f for %.2f s\n", hand == 0 ? "left" : "right", amplitude, seconds );
			}
		}
	}
}
