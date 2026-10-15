#include "VRSyntheticPad.h"

#include "VRBindings.h"
#include "VRCamera.h"
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
		int s_lastAttackHand = 1;     // the combo buzz goes to the hand that attacked

		bool s_dpadMode = false;          // manual flip of the D-pad mode (left grip + stick click)
		bool s_ui = false;                // a menu has the input (UiContext)

		// The input manager's context (0x00B9CC80): 0x1007 while moving freely, 0x0405 while a
		// menu has the input (main menu, shops, mission counter, login screens).
		bool UiContext()
		{
			return ( PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B9CC80 ) & 0xFFFF ) == 0x0405;
		}

		// Left stick = D-pad: automatically in menus (vr.autoDpad), flipped by the manual toggle
		// until the context changes.
		bool DpadActive()
		{
			return ( Config::Get().vr.m_autoDpad && s_ui ) != s_dpadMode;
		}
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
			const int buzzHand = VRControllers::Active( s_lastAttackHand ) ? s_lastAttackHand : 1;
			if( open && !s_comboWindow && Config::Get().vr.m_comboHaptic && VRControllers::Active( buzzHand ) )
			{
				VRControllers::Haptic( buzzHand, combat.comboStep >= 2 ? 0.8f : 0.5f, 0.03f );
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

		// Motion combat (vr.swingAttack): gestures press the game's attack buttons, chosen by
		// what each hand holds (PSUCombat weapon classes). X = attack and Y = Photon Art with
		// the active weapon; R1 switches the active weapon between the right and left hand
		// (+0x1DC). Swing a melee hand to attack, swing with that hand's trigger held for a
		// Photon Art; pull the trigger of the hand holding a gun to shoot. A request first
		// switches to that hand's weapon if needed, waits for the combo window when made
		// during the swing animation (vr.comboBuffer), and on the first hit of a chain turns
		// the character toward where you look (vr.attackFaceLook).
		bool s_swingArmed[ 2 ] = { true, true };
		int s_swingFrames[ 2 ]{};
		int s_pendingButton = -1;     // requested attack button (X / Y), not sent yet
		float s_pendingTime = 0.0f;   // seconds the request may still wait
		int s_pendingHand = 1;
		uint32_t s_pendingWeapon = 0; // weapon class that must be active first (0 = any)
		bool s_switchSent = false;    // R1 sent for this request
		int s_pulseButton = -1;       // pad button being pressed
		float s_pulseTime = 0.0f;
		float s_faceHold = 0.0f;      // left stick still held toward the look direction after the turn
		bool s_turning = false;       // turning toward the look direction; the press waits
		float s_turnTime = 0.0f;
		constexpr float kSwingRearmSpeed = 0.8f;
		constexpr float kSwingBufferSeconds = 0.25f;
		constexpr float kSwitchWaitSeconds = 0.6f;
		constexpr float kAttackPulseSeconds = 0.08f;
		constexpr float kMinTurnSeconds = 0.05f;
		constexpr float kMaxTurnSeconds = 0.7f;
		constexpr float kFaceHoldAfterSeconds = 0.1f;
		constexpr float kFaceToleranceRadians = 20.0f * ( 3.14159265f / 180.0f );

		bool MeleeKind( PSUCombat::WeaponKind kind )
		{
			return kind == PSUCombat::WeaponKind::Melee1H || kind == PSUCombat::WeaponKind::Melee2H ||
				kind == PSUCombat::WeaponKind::Twin;
		}

		// Whether a hand's gestures are melee swings / gun triggers, from both hands' weapons.
		struct HandRoles
		{
			bool swing[ 2 ]{};
			bool shoot[ 2 ]{};
		};

		HandRoles Roles()
		{
			HandRoles roles;
			// No combat gestures while a menu has the input.
			if( !s_combatValid || !Config::Get().vr.m_swingAttack || s_ui )
			{
				return roles;
			}
			const PSUCombat::WeaponKind right = PSUCombat::ClassifyWeapon( s_combat.weaponClass );
			const PSUCombat::WeaponKind left = PSUCombat::ClassifyWeapon( s_combat.leftClass );
			roles.swing[ 1 ] = MeleeKind( right );
			// Two-handed and twin weapons: either hand swings.
			roles.swing[ 0 ] = right == PSUCombat::WeaponKind::Melee2H || right == PSUCombat::WeaponKind::Twin;
			roles.shoot[ 0 ] = left == PSUCombat::WeaponKind::Gun1H;
			return roles;
		}

		float WrapRadians( float angle )
		{
			while( angle > 3.14159265f ) angle -= 6.2831853f;
			while( angle < -3.14159265f ) angle += 6.2831853f;
			return angle;
		}

		// Angle from the character's facing to where the player looks; false when unknown.
		bool FacingOff( float &off )
		{
			float look = 0.0f;
			if( !s_combatValid || !VRCamera::LookYaw( look ) )
			{
				return false;
			}
			const float facing = static_cast< float >( s_combat.facing ) * ( 6.2831853f / 65536.0f );
			off = WrapRadians( look - facing );
			return true;
		}

		// The character faces away from where the player looks (first hit of a chain only).
		bool NeedsFacing()
		{
			float off = 0.0f;
			if( !Config::Get().vr.m_attackFaceLook || s_combat.comboStep > 0 || !FacingOff( off ) )
			{
				return false;
			}
#ifdef _DEBUG
			PSU_LOG( "VRFace: off %.0f deg (facing raw %04X)\n", off * 57.29578f, s_combat.facing );
#endif
			return std::fabs( off ) > kFaceToleranceRadians;
		}

		void StartAttack( int button, int hand )
		{
			s_lastAttackHand = hand;
			s_pulseButton = button;
			s_pulseTime = kAttackPulseSeconds;
			// PSU aims an attack along the stick held when it starts, so the stick stays toward
			// the look direction through the press, not just before it.
			// The game turns at its own speed (about 75 degrees per 0.1 s), so the press waits
			// until the facing is within tolerance, at most kMaxTurnSeconds.
			s_turning = NeedsFacing();
			s_turnTime = 0.0f;
			s_faceHold = 0.0f;
		}

		// The weapon X / Y use now; 0 (weapon not drawn yet) counts as the right-hand one.
		uint32_t ActiveWeapon()
		{
			return s_combat.activeClass ? s_combat.activeClass : s_combat.weaponClass;
		}

		// A gesture asked for `button` with `weapon` (0 = whatever is active). Sent by
		// PumpRequest once that weapon is active and the combo allows it.
		void RequestAttack( int button, int hand, uint32_t weapon, const char *what )
		{
			VRControllers::Haptic( hand, 0.25f, 0.02f );
			s_pendingButton = button;
			s_pendingHand = hand;
			s_pendingWeapon = weapon;
			s_switchSent = false;
			const bool needSwitch = weapon && ActiveWeapon() != weapon;
			s_pendingTime = needSwitch ? kSwitchWaitSeconds : kSwingBufferSeconds;
#ifdef _DEBUG
			PSU_LOG( "VRSwing: %s hand %s -> %s, step %d, active %X%s\n", hand == 0 ? "left" : "right", what,
				button == kPadY ? "Y (PA)" : "X", s_combatValid ? s_combat.comboStep : 0, ActiveWeapon(),
				needSwitch ? ", switching weapon (R1)" : "" );
#endif
		}

		void PumpRequest( float dt )
		{
			if( s_pendingButton < 0 || s_pulseButton >= 0 )
			{
				return;
			}
			if( s_pendingWeapon && ActiveWeapon() != s_pendingWeapon )
			{
				// Switch hands first: R1, then wait for +0x1DC to follow.
				if( !s_switchSent )
				{
					s_switchSent = true;
					s_pulseButton = kPadR1;
					s_pulseTime = kAttackPulseSeconds;
				}
			}
			else if( s_switchSent && s_combatValid && ( s_combat.attackState & 0xFF ) == 0x06 )
			{
				// Still in the weapon switch animation (+0x710 0x..06): presses would be lost.
			}
			else if( !( s_combatValid && PSUCombat::Swinging( s_combat ) ) || !Config::Get().vr.m_comboBuffer )
			{
				StartAttack( s_pendingButton, s_pendingHand );
				s_pendingButton = -1;
				return;
			}
			if( ( s_pendingTime -= dt ) <= 0.0f )
			{
#ifdef _DEBUG
				PSU_LOG( "VRSwing: request dropped (%s)\n", s_switchSent && ActiveWeapon() != s_pendingWeapon ?
					"weapon didn't switch" : "window didn't open in time" );
#endif
				s_pendingButton = -1;
			}
		}

		// Swing detection for one hand: fires once per swing, re-arms when the hand slows.
		bool Swung( int hand )
		{
			const ControllerState &c = VRControllers::State( hand );
			if( !c.active || !c.velocityValid )
			{
				s_swingArmed[ hand ] = true;
				s_swingFrames[ hand ] = 0;
				return false;
			}
			const float *v = c.linearVelocity;
			const float speed = std::sqrt( v[ 0 ] * v[ 0 ] + v[ 1 ] * v[ 1 ] + v[ 2 ] * v[ 2 ] );
			if( speed < kSwingRearmSpeed )
			{
				s_swingArmed[ hand ] = true;
				s_swingFrames[ hand ] = 0;
				return false;
			}
			if( s_swingArmed[ hand ] && speed > Config::Get().vr.m_swingSpeed && ++s_swingFrames[ hand ] >= 2 )
			{
				s_swingArmed[ hand ] = false;
				return true;
			}
			return false;
		}

		// Left-hand gun. The left weapon is the active one only while R1 is held (+0x1DC 0x22xx),
		// so a shot holds R1, waits for the gun to come up, then presses X. A click fires once;
		// holding the trigger fires again each time the game takes the next shot. R1 stays held
		// until the shot is over, so releasing doesn't cancel it.
		bool s_gunShot = false;          // a click still owed a shot
		float s_gunShotTime = 0.0f;
		float s_gunTail = 0.0f;          // keep R1 held a little after the last shot
		float s_gunGap = 0.0f;           // after an X press, wait before judging the state
		float s_gunUpTime = 0.0f;        // how long the left gun has been the active weapon
		constexpr float kGunSettleSeconds = 0.1f;
		constexpr float kGunShotWaitSeconds = 0.8f;
		constexpr float kGunTailSeconds = 0.2f;
		constexpr float kGunGapSeconds = 0.15f;

		void Gun( float dt, const HandRoles &roles, PadState &pad )
		{
			if( !roles.shoot[ 0 ] )
			{
				s_gunShot = false;
				s_gunTail = 0.0f;
				return;
			}
			const bool held = s_triggerDown[ 0 ];
			if( s_gunShot && ( s_gunShotTime -= dt ) <= 0.0f )
			{
				s_gunShot = false;
#ifdef _DEBUG
				PSU_LOG( "VRGun: shot dropped (gun didn't come up)\n" );
#endif
			}
			const bool shooting = held || s_gunShot || ( s_lastAttackHand == 0 && s_combat.comboStep > 0 );
			if( shooting )
			{
				s_gunTail = kGunTailSeconds;
			}
			else if( s_gunTail > 0.0f )
			{
				s_gunTail -= dt;
			}
			if( s_gunTail <= 0.0f )
			{
				return;
			}
			pad.buttons |= Bit( kPadR1 );

			if( s_gunGap > 0.0f )
			{
				s_gunGap -= dt;
				return;
			}
			// +0x710 stays 0x..06 (gun stance) the whole time R1 is held: only the active weapon
			// tells that the gun is up; give it a moment to settle.
			if( s_combat.activeClass == s_combat.leftClass )
			{
				s_gunUpTime += dt;
			}
			else
			{
				s_gunUpTime = 0.0f;
			}
			const bool gunUp = s_gunUpTime >= kGunSettleSeconds;
			const bool ready = !PSUCombat::Swinging( s_combat );
			if( ( held || s_gunShot ) && gunUp && ready && s_pulseButton < 0 )
			{
				// Same as a swing: the first shot of a chain turns toward where you look.
				StartAttack( kPadX, 0 );
				s_gunGap = kAttackPulseSeconds + kGunGapSeconds;
#ifdef _DEBUG
				PSU_LOG( "VRGun: shot (%s), step %d\n", s_gunShot ? "click" : "held", s_combat.comboStep );
#endif
				s_gunShot = false;
			}
		}

		// Adds the combat presses to the pad. Returns the roles so the caller can keep the
		// gesture controls (triggers, right grip) from also sending their plain buttons.
		HandRoles Combat( float dt, bool modifier, PadState &pad )
		{
			const HandRoles roles = Roles();
			const auto &vr = Config::Get().vr;
			if( !modifier )
			{
				for( int hand = 0; hand < 2; ++hand )
				{
					if( roles.swing[ hand ] && Swung( hand ) )
					{
						// Photon Art: swing while holding that hand's trigger.
						const bool pa = vr.m_paGesture && s_triggerDown[ hand ];
						RequestAttack( pa ? kPadY : kPadX, hand, 0, pa ? "swing+trigger" : "swing" );
					}
					else if( !roles.swing[ hand ] )
					{
						Swung( hand );  // keep the detector armed state current
					}
					if( roles.shoot[ hand ] && ( VRControllers::Pressed( hand ) & kControllerTrigger ) )
					{
						// One click = one shot (even if released before the gun is up).
						s_gunShot = true;
						s_gunShotTime = kGunShotWaitSeconds;
						VRControllers::Haptic( hand, 0.25f, 0.02f );
					}
				}
			}

			PumpRequest( dt );
			Gun( dt, roles, pad );

			if( s_turning )
			{
				// Stick toward the look direction (forward in the camera's frame) until the
				// character faces it; PSU aims the attack along the stick held as it starts,
				// so the stick stays there through the press.
				s_turnTime += dt;
				float off = 0.0f;
				const bool facing = FacingOff( off ) && std::fabs( off ) <= kFaceToleranceRadians;
				if( ( facing && s_turnTime >= kMinTurnSeconds ) || s_turnTime >= kMaxTurnSeconds )
				{
					s_turning = false;
					s_faceHold = kAttackPulseSeconds + kFaceHoldAfterSeconds;
#ifdef _DEBUG
					PSU_LOG( "VRFace: turned in %.2f s (off %.0f deg)\n", s_turnTime, off * 57.29578f );
#endif
				}
				pad.lx = 0.0f;
				pad.ly = 1.0f;
			}
			else if( s_faceHold > 0.0f )
			{
				s_faceHold -= dt;
				pad.lx = 0.0f;
				pad.ly = 1.0f;
			}
			if( s_turning )
			{
				// The press waits for the turn.
			}
			else if( s_pulseTime > 0.0f && s_pulseButton >= 0 )
			{
				s_pulseTime -= dt;
				pad.buttons |= Bit( s_pulseButton );
				if( s_pulseTime <= 0.0f )
				{
					s_pulseButton = -1;
				}
			}
			return roles;
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
				s_pendingButton = -1;
				s_pulseButton = -1;
				s_pulseTime = 0.0f;
				s_turning = false;
				s_faceHold = 0.0f;
				s_gunShot = false;
				s_gunTail = 0.0f;
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

			// Menus switch the left stick to the D-pad; a context change drops the manual flip.
			const bool ui = UiContext();
			if( ui != s_ui )
			{
				s_ui = ui;
				s_dpadMode = false;
#ifdef _DEBUG
				PSU_LOG( "VRSyntheticPad: %s (left stick = %s)\n", ui ? "menu input" : "field input",
					DpadActive() ? "D-pad" : "move" );
#endif
			}

			// Left grip + left stick click toggles the D-pad; that grip and click send nothing.
			if( !s_leftClickConsumed && ( VRControllers::Pressed( 0 ) & kControllerStick ) && leftGripDown &&
				s_grip[ 0 ] != Grip::Modifier && s_grip[ 1 ] != Grip::Modifier )
			{
				s_dpadMode = !s_dpadMode;
				s_leftClickConsumed = true;
				s_grip[ 0 ] = Grip::Consumed;
				s_pulse[ 0 ] = 0.0f;
				VRBindings::ShowToast( DpadActive() ? "D-pad on" : "D-pad off" );
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
			const HandRoles roles = Roles();
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
				// A hand's trigger is its Photon Art modifier (melee) or its gun trigger.
				if( entry.control == Control::Trigger && ( roles.swing[ entry.hand ] || roles.shoot[ entry.hand ] ) )
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
			if( DpadActive() )
			{
				pad.buttons |= DpadButtons( left.stick[ 0 ], left.stick[ 1 ] );
			}
			else
			{
				pad.lx = Deadzone( left.stick[ 0 ], 0.15f );
				pad.ly = Deadzone( left.stick[ 1 ], 0.15f );
			}
			Combat( dt, modifier, pad );
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
