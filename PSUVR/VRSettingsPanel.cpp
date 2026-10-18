#include "VRSettingsPanel.h"

#include "VRControllerDiagram.h"

#include "VRGamepad.h"

#include <imgui.h>

#include <Windows.h>
#include <Xinput.h>

#include <cstdio>

namespace PSUVR
{
	namespace
	{
		// Rebind capture: which action and whether a key or a pad button is awaited.
		int s_captureAction = -1;
		bool s_captureKey = true;
		bool s_heldAtStart[ 256 ]{};
		unsigned short s_padAtStart = 0;

		bool KeyDown( int vk )
		{
			return ( GetAsyncKeyState( vk ) & 0x8000 ) != 0;
		}

		void BeginCapture( int action, bool key )
		{
			s_captureAction = action;
			s_captureKey = key;
			for( int vk = 0; vk < 256; ++vk )
			{
				s_heldAtStart[ vk ] = KeyDown( vk );
			}
			VRGamepad::Buttons( s_padAtStart );
		}

		// Polls for the binding being captured. Escape cancels.
		void UpdateCapture( VRSettings &settings )
		{
			if( s_captureAction < 0 )
			{
				return;
			}
			for( int vk = 8; vk < 256; ++vk )
			{
				if( vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2 )
				{
					continue;
				}
				const bool down = KeyDown( vk );
				if( !down )
				{
					s_heldAtStart[ vk ] = false;
					continue;
				}
				if( s_heldAtStart[ vk ] )
				{
					continue;
				}
				if( vk == VK_ESCAPE )
				{
					s_captureAction = -1;
					return;
				}
				// Modifiers only qualify the main key.
				if( vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
					( vk >= VK_LSHIFT && vk <= VK_RMENU ) || vk == VK_LWIN || vk == VK_RWIN )
				{
					continue;
				}
				if( s_captureKey )
				{
					uint8_t mods = 0;
					if( KeyDown( VK_CONTROL ) ) mods |= kVRModCtrl;
					if( KeyDown( VK_SHIFT ) ) mods |= kVRModShift;
					if( KeyDown( VK_MENU ) ) mods |= kVRModAlt;
					settings.m_bindings[ s_captureAction ].key = vk;
					settings.m_bindings[ s_captureAction ].mods = mods;
					s_captureAction = -1;
					return;
				}
			}
			if( !s_captureKey )
			{
				unsigned short buttons = 0;
				VRGamepad::Buttons( buttons );
				const unsigned short pressed = static_cast< unsigned short >( buttons & ~s_padAtStart ) &
					static_cast< unsigned short >( ~XINPUT_GAMEPAD_BACK );
				s_padAtStart = buttons;
				if( pressed )
				{
					// Lowest newly pressed button.
					settings.m_bindings[ s_captureAction ].pad = static_cast< unsigned short >( pressed & ( ~pressed + 1 ) );
					s_captureAction = -1;
				}
			}
		}

		bool Combo( const char *label, std::string &value, const char *const *ids, const char *const *names, int count )
		{
			int current = 0;
			for( int i = 0; i < count; ++i )
			{
				if( value == ids[ i ] )
				{
					current = i;
				}
			}
			if( ImGui::Combo( label, &current, names, count ) )
			{
				value = ids[ current ];
				return true;
			}
			return false;
		}

		void Help( const char *text )
		{
			ImGui::SameLine();
			ImGui::TextDisabled( "(?)" );
			if( ImGui::IsItemHovered() )
			{
				ImGui::SetTooltip( "%s", text );
			}
		}
	}

	std::string VRKeyName( int virtualKey )
	{
		if( virtualKey <= 0 )
		{
			return "-";
		}
		UINT scan = MapVirtualKeyA( static_cast< UINT >( virtualKey ), MAPVK_VK_TO_VSC );
		LONG param = static_cast< LONG >( scan << 16 );
		switch( virtualKey )
		{
			// Extended keys need bit 24 for the right name.
			case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
			case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_RCONTROL: case VK_RMENU:
			case VK_DIVIDE: case VK_NUMLOCK:
				param |= 1 << 24;
				break;
			default:
				break;
		}
		char name[ 64 ]{};
		if( scan && GetKeyNameTextA( param, name, sizeof( name ) ) > 0 )
		{
			return name;
		}
		char fallback[ 16 ];
		std::snprintf( fallback, sizeof( fallback ), "VK 0x%02X", virtualKey );
		return fallback;
	}

	std::string VRBindingKeyName( const VRBinding &binding )
	{
		if( binding.key <= 0 )
		{
			return "-";
		}
		std::string name;
		if( binding.mods & kVRModCtrl ) name += "Ctrl + ";
		if( binding.mods & kVRModShift ) name += "Shift + ";
		if( binding.mods & kVRModAlt ) name += "Alt + ";
		return name + VRKeyName( binding.key );
	}

	std::string VRPadName( unsigned short padButton )
	{
		switch( padButton )
		{
			case 0: return "-";
			case XINPUT_GAMEPAD_A: return "Back + A";
			case XINPUT_GAMEPAD_B: return "Back + B";
			case XINPUT_GAMEPAD_X: return "Back + X";
			case XINPUT_GAMEPAD_Y: return "Back + Y";
			case XINPUT_GAMEPAD_START: return "Back + Start";
			case XINPUT_GAMEPAD_LEFT_SHOULDER: return "Back + LB";
			case XINPUT_GAMEPAD_RIGHT_SHOULDER: return "Back + RB";
			case XINPUT_GAMEPAD_LEFT_THUMB: return "Back + L3";
			case XINPUT_GAMEPAD_RIGHT_THUMB: return "Back + R3";
			case XINPUT_GAMEPAD_DPAD_UP: return "Back + D-pad up";
			case XINPUT_GAMEPAD_DPAD_DOWN: return "Back + D-pad down";
			case XINPUT_GAMEPAD_DPAD_LEFT: return "Back + D-pad left";
			case XINPUT_GAMEPAD_DPAD_RIGHT: return "Back + D-pad right";
			default:
			{
				char text[ 24 ];
				std::snprintf( text, sizeof( text ), "Back + 0x%04X", padButton );
				return text;
			}
		}
	}

	void DrawVRSettingsPanel( VRSettings &s, VRPanelContext &context )
	{
		UpdateCapture( s );

		if( !context.status.empty() )
		{
			for( const std::string &line : context.status )
			{
				ImGui::TextUnformatted( line.c_str() );
			}
			ImGui::Separator();
		}

		if( ImGui::CollapsingHeader( "General", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::Checkbox( "VR enabled", &s.m_enabled );
			Help( "Applies on the next game launch (the game's Direct3D device is set up for VR at startup)." );
			if( context.inGame )
			{
				ImGui::Checkbox( "Pause VR", &s.m_paused );
				Help( "Instantly switch the game to flat play; the headset shows it on a big screen. Not saved." );
			}
			ImGui::Checkbox( "Gamepad works when the game isn't focused", &s.m_backgroundInput );
			Help( "With the headset on, the game window often isn't the focused one. Applies on the next game launch." );
			if( s.m_backgroundInput )
			{
				ImGui::Checkbox( "Keyboard too", &s.m_backgroundKeyboard );
				Help( "Keys typed in other apps then also reach the game. Applies on the next game launch." );
			}
			static const char *const cameraIds[] = { "firstPerson", "thirdPerson" };
			static const char *const cameraNames[] = { "First person", "Third person" };
			Combo( "Camera", s.m_cameraMode, cameraIds, cameraNames, 2 );
			ImGui::SliderFloat( "World scale", &s.m_worldScale, 0.5f, 2.0f, "%.2f" );
		}

		if( ImGui::CollapsingHeader( "First person", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			static const char *const moveIds[] = { "head", "body" };
			static const char *const moveNames[] = { "Where you look (head)", "Where the body faces" };
			Combo( "Left stick forward", s.m_fpMoveDirection, moveIds, moveNames, 2 );
			static const char *const bodyIds[] = { "headless", "hands", "hidden", "full" };
			static const char *const bodyNames[] = { "Headless (hands, body, legs)", "Hands only", "Hidden", "Full" };
			Combo( "Own body", s.m_fpBody, bodyIds, bodyNames, 4 );
			Help( "Hands only: just the hands and weapons, right on the controllers (no reach limit), while the arms "
				"follow the controllers (otherwise like Headless)." );
			ImGui::SliderFloat( "Eye height (m)", &s.m_fpEyeHeight, 1.2f, 2.0f, "%.2f" );
			ImGui::SliderFloat( "Eye forward (m)", &s.m_fpEyeForward, 0.0f, 0.3f, "%.2f" );
			ImGui::SliderFloat( "Neck cut below eyes (m)", &s.m_fpNeckDrop, 0.05f, 0.5f, "%.2f" );
			ImGui::SliderFloat( "Lean limit (m)", &s.m_fpLeanLimit, 0.0f, 0.5f, "%.2f" );
			ImGui::SliderFloat( "Crouch limit (m)", &s.m_fpCrouchLimit, 0.0f, 0.5f, "%.2f" );
			ImGui::Checkbox( "Walk in the room moves the character", &s.m_roomWalk );
			Help( "Step away from your character and it walks to where you are (the left stick still works and takes "
				"over). Needs the motion controllers (Play with motion controllers)." );
			if( s.m_roomWalk )
			{
				ImGui::SliderFloat( "Room walk starts at (m)", &s.m_roomWalkStart, 0.1f, 0.6f, "%.2f" );
			}
			static const char *const lbIds[] = { "off", "follow" };
			static const char *const lbFpNames[] = { "Off (lock-on only, view stays yours)", "Turn view toward the target" };
			Combo( "LB camera##fp", s.m_fpLbCamera, lbIds, lbFpNames, 2 );
			Help( "Turning the view toward a target rotates the world under you; off is more comfortable." );
			ImGui::SliderFloat( "LB follow time (s)", &s.m_lbWindowSeconds, 0.5f, 5.0f, "%.1f" );
		}

		if( ImGui::CollapsingHeader( "Third person" ) )
		{
			ImGui::TextDisabled( "Right stick: left/right turns, up/down zooms. Hold Back: left/right tilts, up/down height." );
			ImGui::Checkbox( "Snap turn##tp", &s.m_tpSnapTurn );
			if( s.m_tpSnapTurn )
			{
				ImGui::SliderFloat( "Turn step (deg)##tp", &s.m_tpOrbitDegrees, 15.0f, 90.0f, "%.0f" );
			}
			else
			{
				ImGui::SliderFloat( "Turn speed (deg/s)##tp", &s.m_tpTurnSpeed, 30.0f, 360.0f, "%.0f" );
			}
			ImGui::SliderFloat( "Tilt (deg)", &s.m_tpPitch, -60.0f, 30.0f, "%.0f" );
			Help( "Negative looks down at your character. A tilted camera also tilts the horizon." );
			ImGui::SliderFloat( "Tilt speed (deg/s)", &s.m_tpTiltSpeed, 10.0f, 120.0f, "%.0f" );
			static const char *const lbIds[] = { "follow", "off" };
			static const char *const lbTpNames[] = { "Camera behind you / lock-on (vanilla)", "Off" };
			Combo( "LB camera##tp", s.m_tpLbCamera, lbIds, lbTpNames, 2 );
			ImGui::SliderFloat( "Distance (m)", &s.m_tpDistance, s.m_tpMinDistance, s.m_tpMaxDistance, "%.1f" );
			ImGui::SliderFloat( "Height above head (m)", &s.m_tpHeight, -1.0f, 4.0f, "%.1f" );
			ImGui::SliderFloat( "Zoom speed (m/s)", &s.m_tpZoomSpeed, 0.5f, 10.0f, "%.1f" );
			ImGui::SliderFloat( "Closest (m)", &s.m_tpMinDistance, 0.5f, 5.0f, "%.1f" );
			ImGui::SliderFloat( "Farthest (m)", &s.m_tpMaxDistance, 3.0f, 20.0f, "%.1f" );
		}

		if( ImGui::CollapsingHeader( "Turning", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::Checkbox( "Snap turn", &s.m_snapTurn );
			Help( "Snap turn is the comfort default. Smooth turn can cause motion sickness." );
			if( s.m_snapTurn )
			{
				ImGui::SliderFloat( "Snap step (deg)", &s.m_snapTurnDegrees, 15.0f, 90.0f, "%.0f" );
			}
			else
			{
				ImGui::SliderFloat( "Smooth speed (deg/s)", &s.m_fpTurnSpeed, 30.0f, 360.0f, "%.0f" );
			}
		}

		if( ImGui::CollapsingHeader( "HUD and menus" ) )
		{
			ImGui::Checkbox( "Body-locked panel (lazy follow)", &s.m_hudFollow );
			ImGui::SliderFloat( "Follow angle (deg)", &s.m_hudFollowAngle, 10.0f, 90.0f, "%.0f" );
			ImGui::SliderFloat( "Distance (m)", &s.m_hudDistance, 1.0f, 4.0f, "%.2f" );
			ImGui::SliderFloat( "Size", &s.m_hudSize, 0.5f, 2.0f, "%.2f" );
			ImGui::SliderFloat( "Below eye level (m)", &s.m_hudDrop, -0.3f, 0.6f, "%.2f" );
			ImGui::SliderFloat( "Map backdrop opacity", &s.m_hudMapBlackAlpha, 0.0f, 1.0f, "%.2f" );
			ImGui::SliderFloat( "Nameplate scale", &s.m_uiScale, 1.0f, 2.5f, "%.2f" );
			ImGui::Checkbox( "World-locked kiosk panel (experimental)", &s.m_hudWorldPanel );
		}

		if( ImGui::CollapsingHeader( "Comfort" ) )
		{
			ImGui::Checkbox( "Vignette while moving / smooth turning", &s.m_comfortVignette );
			ImGui::SliderFloat( "Vignette strength", &s.m_vignetteStrength, 0.0f, 1.0f, "%.2f" );
		}

		if( ImGui::CollapsingHeader( "Motion controllers", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			bool motion = s.m_inputMode == "motion";
			if( ImGui::Checkbox( "Play with motion controllers", &motion ) )
			{
				s.m_inputMode = motion ? "motion" : "gamepad";
			}
			Help( "The VR controllers act as a gamepad (a connected gamepad keeps working too). "
				"See Controller layout below for every button." );
			if( !context.inGame )
			{
				ImGui::TextDisabled( "Motion controllers: %s (applies when the game starts)", motion ? "on" : "off" );
			}
			ImGui::Checkbox( "Show controllers", &s.m_showControllers );
			ImGui::Checkbox( "Emulate controllers (testing)", &s.m_emulateControllers );
			Help( "For testing without the headset on: Ctrl+Shift+F9 toggles. Ctrl+numpad: 7/8/9 left/both/right, 1-5 rest/forward/gun/"
				"raised/calib, 0 trigger, . grip, / Cross (A), + Circle (B), - stick click, 6 swing, * both grips. Ctrl+T/G/F/H "
				"stick. A command file (psuex_vr_script.txt) runs whenever it changes; see VR_GUIDE.md." );
			ImGui::Checkbox( "Arms follow the controllers (first person)", &s.m_handsFollow );
			Help( "The character's arms reach for where you hold the controllers." );
			if( s.m_handsFollow )
			{
				bool attackAnimation = s.m_handsAttack == "animation";
				if( ImGui::Checkbox( "Play the game's attack animation", &attackAnimation ) )
				{
					s.m_handsAttack = attackAnimation ? "animation" : "controller";
				}
				Help( "Off: your arms stay on the controllers while attacking (your swing is the swing). "
					"On: the arms hand over to the game's attack animation. Photon Arts always use the game's animation." );
				if( context.inGame )
				{
					if( ImGui::Button( "Calibrate hand angles" ) )
					{
						++s.m_handCalibRequest;
					}
					ImGui::SameLine();
					ImGui::TextDisabled( s.m_handCalibrated ? "(calibrated)" : "(automatic)" );
					Help( "After the click, hold both arms straight out in front, hands in fists around the controllers, "
						"palms facing each other (knuckles forward), and keep still: the angle is taken after 5 seconds "
						"(a buzz each second, a long buzz when done); it is saved. Also on both grips + both triggers. "
						"Without it the angle is estimated, which suits most controllers." );
					if( s.m_handCalibrated && ImGui::Button( "Back to automatic" ) )
					{
						s.m_handCalibrated = false;
					}
				}
				{
					ImGui::SliderFloat( "Hand pitch (deg)", &s.m_handPitch, -90.0f, 90.0f, "%.0f" );
					ImGui::SliderFloat( "Hand roll (deg)", &s.m_handRoll, -180.0f, 180.0f, "%.0f" );
					Help( "Fine tune the hand angle on top of the calibration (tip up / down, twist)." );
				}
			}
			ImGui::Checkbox( "Left stick works menus (D-pad) automatically", &s.m_autoDpad );
			Help( "In menus, shops, the mission counter, login screens and the weapon / item palette the left stick sends the D-pad. "
				"Left grip + left stick click still flips it by hand." );
			ImGui::Checkbox( "Buzz when the next combo hit can be pressed", &s.m_comboHaptic );
			Help( "A short buzz in the right hand the moment the game accepts the next attack of a melee combo." );
			ImGui::Checkbox( "Motion combat (swing / shoot)", &s.m_swingAttack );
			Help( "Follows what each hand holds: swing a melee hand to attack, pull the trigger of the hand "
				"holding a gun to shoot. Two-handed and twin weapons swing with either hand. "
				"Needs 'Play with motion controllers'." );
			if( s.m_swingAttack )
			{
				ImGui::Checkbox( "Photon Art gestures", &s.m_paGesture );
				Help( "Melee: swing with that hand's trigger held. Left-hand gun: left grip + trigger." );
				ImGui::Checkbox( "Turn to where I look before attacking", &s.m_attackFaceLook );
				ImGui::Checkbox( "Aim free aim with the gun hand", &s.m_controllerAim );
				Help( "Left stick click turns free aim mode on / off. While on, raising the gun hand into view "
					"enters the game's free aim (reticle) and lowering it leaves it; turning the gun hand turns "
					"the aim by the same angle." );
				if( s.m_controllerAim )
				{
					ImGui::Checkbox( "Invert aim up / down", &s.m_aimInvertPitch );
					ImGui::Checkbox( "Set the aim directly (first person)", &s.m_aimDirect );
					Help( "The game's aim follows the gun hand exactly, with no lag. Off: it is steered toward the "
						"hand like a stick, which trails behind." );
					ImGui::Checkbox( "Reticle shows where the shot goes", &s.m_reticleOnShot );
					Help( "On: the reticle sits on the game's actual aim. Off: on the line the gun hand points along." );
					ImGui::TextDisabled( "Hands and gun in free aim (turn one off if free aim gets slow):" );
					ImGui::Checkbox( "Ignore near-camera hide", &s.m_freeAimNoNearHide );
					ImGui::Checkbox( "Ignore part culling", &s.m_freeAimNoPartCull );
					ImGui::SliderFloat( "Reticle size (degrees)", &s.m_reticleSize, 0.5f, 8.0f, "%.1f" );
				}
				Help( "First person: if the character faces another way (after strafing or backing up), the "
					"first hit turns it toward your view first." );
				ImGui::SliderFloat( "Swing speed (m/s)", &s.m_swingSpeed, 1.0f, 4.0f, "%.1f" );
				Help( "Lower: gentler swings count. Higher: only big swings count." );
				ImGui::Checkbox( "Hold early swings for the combo window", &s.m_comboBuffer );
				Help( "A swing made while the attack is still playing is sent the moment the next hit can connect (up to 0.25 s)." );
			}
			static const char *const profileIds[] = { "psvr2", "generic" };
			static const char *const profileNames[] = { "PlayStation VR2 Sense", "Other (Touch, Index, ...)" };
			Combo( "Controllers", s.m_motionProfile, profileIds, profileNames, 2 );
			if( ImGui::TreeNodeEx( "Controller layout", context.inGame ? 0 : ImGuiTreeNodeFlags_DefaultOpen ) )
			{
				DrawControllerDiagram( s.m_motionProfile );
				ImGui::TreePop();
			}
		}

		if( ImGui::CollapsingHeader( "Controls", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::TextDisabled( "Pad chords: hold Back/Select, then press the button." );
			if( ImGui::BeginTable( "vr_bindings", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg ) )
			{
				ImGui::TableSetupColumn( "Action" );
				ImGui::TableSetupColumn( "Key" );
				ImGui::TableSetupColumn( "Pad" );
				ImGui::TableSetupColumn( "" );
				ImGui::TableHeadersRow();
				for( int i = 0; i < kVRActionCount; ++i )
				{
					VRBinding &binding = s.m_bindings[ i ];
					ImGui::PushID( i );
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted( VRActionLabel( static_cast< VRAction >( i ) ) );

					ImGui::TableNextColumn();
					const bool capturingKey = s_captureAction == i && s_captureKey;
					const std::string keyText = capturingKey ? std::string( "press a key..." ) : VRBindingKeyName( binding );
					if( ImGui::Button( ( keyText + "##key" ).c_str() ) )
					{
						BeginCapture( i, true );
					}

					ImGui::TableNextColumn();
					const bool capturingPad = s_captureAction == i && !s_captureKey;
					const std::string padText = capturingPad ? std::string( "press a button..." ) : VRPadName( binding.pad );
					if( ImGui::Button( ( padText + "##pad" ).c_str() ) )
					{
						BeginCapture( i, false );
					}

					ImGui::TableNextColumn();
					if( ImGui::SmallButton( "Clear" ) )
					{
						binding = VRBinding{};
						s_captureAction = -1;
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
			}
			if( s_captureAction >= 0 )
			{
				ImGui::TextDisabled( s_captureKey ? "Hold Ctrl / Shift / Alt with the key if you want them. Esc cancels." : "Esc cancels." );
			}
			if( ImGui::Button( "Reset controls" ) )
			{
				s.m_bindings = DefaultVRBindings();
				s_captureAction = -1;
			}
		}

		ImGui::Separator();
		if( context.save && ImGui::Button( "Save" ) )
		{
			context.message = context.save() ? "Saved." : "Save failed.";
		}
		if( context.inGame && context.reload )
		{
			ImGui::SameLine();
			if( ImGui::Button( "Reload from file" ) )
			{
				context.message = context.reload() ? "Reloaded psuex_vr.json." : "psuex_vr.json not found.";
			}
		}
		ImGui::SameLine();
		if( ImGui::Button( "Reset VR defaults" ) )
		{
			// Keep whether VR is on and the session pause; reset everything else.
			const bool enabled = s.m_enabled;
			const bool paused = s.m_paused;
			s = VRSettings{};
			s.m_enabled = enabled;
			s.m_paused = paused;
			context.message = "Defaults restored (not saved yet).";
		}
		if( !context.message.empty() )
		{
			ImGui::TextDisabled( "%s", context.message.c_str() );
		}
	}
}
