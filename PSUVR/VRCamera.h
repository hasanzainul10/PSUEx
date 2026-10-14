#pragma once

#include "VRRuntime.h"

#include <d3d9.h>

namespace PSUVR
{
	// The one place OpenXR poses become D3D matrices.
	//
	// The game leaves D3D VIEW at identity and bakes its camera into WORLD, so
	// game camera space (right-handed, +Y up, looking down -Z) is the space the
	// 3D pass draws in. An eye's VIEW is the inverse of its pose relative to the
	// recenter reference, which puts the HMD on top of the game camera.
	namespace VRCamera
	{
		// Game world units per real meter (before vr.worldScale). The follow camera sits
		// 40-70 units behind the player and focuses 15 units up: ~4-7 m and ~1.5 m.
		constexpr float kGameUnitsPerMeter = 10.0f;

		// Reference = head position and yaw now; the game camera sits there.
		void Recenter( const EyePose views[ 2 ] );
		bool HasReference();

		// gameProj supplies the depth mapping; FOV terms come from the eye.
		void BuildEye( const EyePose &eye, float unitsPerMeter, const D3DMATRIX &gameProj,
			D3DMATRIX &view, D3DMATRIX &proj );

		// First person: the orbit camera is steered level behind the player's head,
		// looking along body yaw (right stick) plus HMD yaw when vr.fpMoveDirection is
		// "head" (default). With "body" the HMD looks around without steering.
		bool FirstPersonMode();
		// Camera task hook, after the game's update. Measures right-stick turn, then
		// rewrites the camera position/focus (game world units). False = not applied.
		bool SteerFirstPerson( float cameraPosition[ 3 ], float focusPosition[ 3 ],
			const float playerPosition[ 3 ] );
		// True once a steer happened for the current camera; eyes then use BuildEyeFirstPerson.
		bool FirstPersonActive();
		void ClearFirstPerson();
		// The right stick is read through XInput (false = turning falls back to the game's orbit).
		bool TurnFromStick();
		// Game units from the steered first-person camera to the head.
		float FirstPersonCameraDistance();
		// Leash the first-person head to the body: physical lean/crouch beyond
		// vr.fpLeanLimit / vr.fpCrouchLimit drags the anchor instead of the view.
		void UpdateFirstPersonAnchor( const EyePose views[ 2 ] );
		void BuildEyeFirstPerson( const EyePose &eye, float unitsPerMeter, const D3DMATRIX &gameProj,
			D3DMATRIX &view, D3DMATRIX &proj );

		// LOCAL-space pose for a panel in front of the reference, dropped by dropMeters.
		void PanelPose( float distanceMeters, float dropMeters, float position[ 3 ], float orientation[ 4 ] );
	}
}
