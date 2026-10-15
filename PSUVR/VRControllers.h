#pragma once

#include "VRRuntime.h"

#include <cstdint>

struct IDirect3DDevice9;

namespace PSUVR
{
	// Motion controllers, as seen by the rest of PSUVR (runtime-independent).
	// Poses are in the eye poses' space (OpenXR LOCAL, meters) for the next views.
	namespace VRControllers
	{
		// Once per frame after the runtime's input update: latch state and button edges.
		void Update( IVRRuntime *runtime );

		// hand: 0 = left, 1 = right.
		const ControllerState &State( int hand );
		bool Active( int hand );
		bool AnyActive();
		// ControllerButton bits that went down / up this frame.
		uint32_t Pressed( int hand );
		uint32_t Released( int hand );

		void Haptic( int hand, float amplitude, float seconds );

		// Hand markers and pointer rays over the bound eye RT (after the eye pass).
		// eye: the pose and FOV the eye was rendered with.
		void DrawMarkers( IDirect3DDevice9 *device, const EyePose &eye, unsigned width, unsigned height );
	}
}
