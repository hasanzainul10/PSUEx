#pragma once

#include <cstdint>

namespace PSUVR
{
	// Motion controllers as a gamepad (vr.inputMode "motion"). Controller input becomes a
	// logical PSU pad (the button names of PsuIlluminus.ini [gamepad1]), which the dinput8
	// loader merges into the joystick state the game reads (PSUEx_MergeJoystickState).
	//
	// The layout (plain presses, both-grip chords, the D-pad toggle) is VRMotionLayout::kLayout.
	namespace VRSyntheticPad
	{
		// Once per frame, after VRControllers::Update.
		void Update();
		// True while the controllers drive the pad (motion mode, a controller active).
		bool Active();
		// Free aim mode with the gun raised and the game in free aim: the hand whose ray aims
		// (0 left, 1 right). Our reticle is drawn there and the game's centre reticle hidden.
		bool GunAiming( int &hand );
		// VRAction bits fired by grip chords since the last call.
		unsigned TakeActions();
		// Free aim with the controller: the game yaw / pitch (radians) the aim is being steered
		// to this frame. False when the controller isn't aiming.
		bool LastFreeAim( float &yaw, float &pitch );

		// state: DIJOYSTATE2. axisMin / axisMax: the device range of lX, lY, lZ, lRx, lRy,
		// lRz, slider 0, slider 1. replace: drop the device's own input first (the device is
		// a VR controller seen through DirectInput). Returns false when nothing was merged.
		bool MergeJoystickState( void *state, unsigned size, const long axisMin[ 8 ], const long axisMax[ 8 ], bool replace );
	}
}
