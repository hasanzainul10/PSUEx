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

// key: Win32 virtual-key code (0 = none), with the exact modifiers in mods. pad: XInput
// button mask pressed while Back/Select is held (0 = none).
enum VRKeyMod : uint8_t
{
	kVRModCtrl = 1,
	kVRModShift = 2,
	kVRModAlt = 4,
};

struct VRBinding
{
	int key = 0;
	uint16_t pad = 0;
	uint8_t mods = 0;
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
	// Third person: camera distance behind and height above the head (meters), right-stick
	// zoom speed (m/s) and range, and the Back + right stick orbit / height steps.
	float m_tpDistance = 4.0f;
	float m_tpHeight = 1.0f;
	float m_tpZoomSpeed = 3.0f;
	float m_tpMinDistance = 1.5f;
	float m_tpMaxDistance = 10.0f;
	float m_tpOrbitDegrees = 45.0f;
	// Third person turning, separate from first person: snap steps (tpOrbitDegrees) or
	// smooth (tpTurnSpeed, deg/s). Tilt: camera pitch in degrees (negative = looking down),
	// changed with Back + right stick left/right at tpTiltSpeed deg/s.
	bool m_tpSnapTurn = true;
	float m_tpTurnSpeed = 90.0f;
	float m_tpPitch = 0.0f;
	float m_tpTiltSpeed = 45.0f;
	// What LB (refocus / lock-on) does to the VR camera. "follow": for lbWindowSeconds after
	// an LB press (or while held) the game's camera direction is adopted. "off": ignored.
	std::string m_tpLbCamera = "follow";
	std::string m_fpLbCamera = "off";
	float m_lbWindowSeconds = 2.0f;
	bool m_snapTurn = true;
	bool m_comfortVignette = false;
	// Motion controllers: draw a marker and pointer ray at each tracked controller.
	bool m_showControllers = true;
	// First person: the character's arms reach for the controllers (two-bone IK).
	bool m_handsFollow = true;
	// Button names in the controller layout picture: "psvr2" (Sense) or "generic" (A/B/X/Y).
	std::string m_motionProfile = "psvr2";
	// Buzz the right controller when the game's combo window opens (melee).
	bool m_comboHaptic = true;
	// Motion mode: the left stick works the D-pad while a menu has the input.
	bool m_autoDpad = true;
	// Motion mode combat: swing a melee hand to attack (X), pull a gun hand's trigger to
	// shoot (R1 = left-hand weapon). Weapon per hand comes from the game.
	bool m_swingAttack = true;
	// Swing with that hand's trigger held = Photon Art (Y).
	bool m_paGesture = true;
	// Before the first hit of a chain, turn the character toward where you look.
	bool m_attackFaceLook = true;
	// Free aim (R3 with a gun): the gun hand steers the aim.
	bool m_controllerAim = true;
	bool m_aimInvertPitch = false;
	// Free aim reticle radius in degrees of view (the game's shots spread around the centre).
	float m_reticleSize = 3.0f;
	float m_swingSpeed = 1.8f;      // m/s to count as a swing
	// A swing during the attack animation waits for the combo window (up to 0.25 s).
	bool m_comboBuffer = true;
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
