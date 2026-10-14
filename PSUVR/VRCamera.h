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
		// Reference = head position and yaw now; the game camera sits there.
		void Recenter( const EyePose views[ 2 ] );
		bool HasReference();

		// gameProj supplies the depth mapping; FOV terms come from the eye.
		void BuildEye( const EyePose &eye, float unitsPerMeter, const D3DMATRIX &gameProj,
			D3DMATRIX &view, D3DMATRIX &proj );

		// LOCAL-space pose for a panel in front of the reference, dropped by dropMeters.
		void PanelPose( float distanceMeters, float dropMeters, float position[ 3 ], float orientation[ 4 ] );
	}
}
