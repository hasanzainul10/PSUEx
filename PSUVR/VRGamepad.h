#pragma once

namespace PSUVR
{
	// Read-only XInput polling for VR-side controls (first-person turning).
	// The game keeps reading its pad through DirectInput as before.
	namespace VRGamepad
	{
		// Right stick X of the first connected XInput pad, -1 (left) .. 1 (right),
		// zero inside the standard deadzone. False if no XInput pad is connected.
		bool RightStickX( float &x );
	}
}
