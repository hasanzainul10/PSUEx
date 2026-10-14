#pragma once

#include <array>
#include <cstdint>
#include <string>

// VR settings shared by the game DLL (Config::vr) and the standalone configurator
// (tools/PSUExVRConfig). Serialized to the "vr" object of config.json and to
// psuex_vr.json. No game dependencies.

enum class VRAction : int
{
	Recenter = 0,
	ToggleCameraMode,
	ToggleSettings,
	TogglePause,
	ToggleSnapTurn,
	ToggleHudFollow,
	Count
};

constexpr int kVRActionCount = static_cast< int >( VRAction::Count );

// key: Win32 virtual-key code (0 = none). pad: XInput button mask pressed while
// Back/Select is held (0 = none).
struct VRBinding
{
	int key = 0;
	uint16_t pad = 0;
};

const char *VRActionLabel( VRAction action );
std::array< VRBinding, kVRActionCount > DefaultVRBindings();

struct VRSettings
{
	bool m_enabled = false;
	bool m_forceD3D9Ex = false;
	// Ex upgrade + CreateDeviceEx + managed-pool remap, but no OpenXR/bridge.
	bool m_d3d9exOnly = false;
	bool m_solidColorPrototype = false; // true = teal clear; false = virtual-screen blit
	std::string m_runtime = "openxr";
	std::string m_cameraMode = "thirdPerson";
	std::string m_inputMode = "gamepad";
	// Read by the dinput8 proxy at startup: keep the gamepad (and optionally the keyboard)
	// working while the game window isn't focused. Gamepad defaults on with VR.
	bool m_backgroundInput = true;
	bool m_backgroundKeyboard = false;
	float m_worldScale = 1.0f;
	float m_ipdScale = 1.0f;
	float m_hudDistance = 2.5f;
	float m_hudSize = 1.0f;
	// Scale XYZRHW UI when capturing to VR HUD layers / nameplates (1 = native).
	float m_uiScale = 1.5f;
	// Town UI: false = everything on the visor panel; true = also use the world-locked kiosk panel.
	bool m_hudWorldPanel = false;
	// Opacity kept for near-black parts of the mission map backdrop on the visor (0 = invisible).
	float m_hudMapBlackAlpha = 0.15f;
	// HUD/menu panel in front of the body (lazy follow) instead of locked to the head.
	bool m_hudFollow = true;
	// Degrees the head may look away before the panel glides back in front.
	float m_hudFollowAngle = 35.0f;
	// How far below eye level the panel sits (meters).
	float m_hudDrop = 0.1f;
	// First person: eye height above the player's feet and forward offset (meters, avatar scale).
	float m_fpEyeHeight = 1.6f;
	float m_fpEyeForward = 0.12f;
	// Right-stick snap turn step in degrees (snapTurn = true).
	float m_snapTurnDegrees = 30.0f;
	// Right-stick smooth turn speed in degrees per second (snapTurn = false).
	float m_fpTurnSpeed = 120.0f;
	// "head" = left stick forward goes where you look; "body" = where the body faces.
	std::string m_fpMoveDirection = "head";
	// Own character in first person: "headless" (cut above the neck), "hidden", "full".
	std::string m_fpBody = "headless";
	// How far below the eyes the headless cut sits (meters).
	float m_fpNeckDrop = 0.22f;
	// How far the head may lean / crouch away from the body before the body follows (meters).
	float m_fpLeanLimit = 0.15f;
	float m_fpCrouchLimit = 0.2f;
	bool m_snapTurn = true;
	bool m_comfortVignette = false;
	// 0..1 how far the vignette closes in at full motion.
	float m_vignetteStrength = 0.7f;
	// Render the 3D pass per eye (projection layer) with the game frame as a panel.
	bool m_stereo = true;
	// Scene mode that gets stereo eyes (5 = town). 0 = every mode (old behavior).
	unsigned m_stereoMode = 5;
	// During eye passes, skip XYZRHW quads that sample an RT filled only by the flat pass
	// (glare / afterimage ghosts the mono camera over stereo). Shadows rewritten per eye stay.
	bool m_stereoSkipScreenQuads = true;
	// When stereo is on, also submit the flat game frame as a world-locked quad (HUD stand-in).
	// Default off — the panel reads as a dark PiP sliding over the stereo floor.
	bool m_stereoPanel = false;
	// Debug: shift the 3D pass sideways by ±this many view units, alternating each second.
	float m_debugEyeShift = 0.0f;
	// Debug: run the 3D pass twice per frame to test repeat-safety.
	bool m_debugDoublePass = false;

	// VR shortcuts (keyboard and Back+button pad chords).
	std::array< VRBinding, kVRActionCount > m_bindings = DefaultVRBindings();

	// Runtime only (never saved): stereo, first person and HUD capture stand down and the
	// headset shows the flat game on the virtual screen.
	bool m_paused = false;
};

// JSON text of a "vr" object <-> settings. Loading only overwrites keys that are present
// and well-typed, so partial files keep the other values.
bool LoadVRSettingsJson( VRSettings &settings, const std::string &jsonText );
std::string SaveVRSettingsJson( const VRSettings &settings );
