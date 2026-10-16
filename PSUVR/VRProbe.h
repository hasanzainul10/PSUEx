#pragma once

namespace PSUVR
{
	// Reverse-engineering probe (Debug builds only; read-only).
	//   Ctrl+F11: start / stop watching the input regions; every changed dword is logged.
	//   Ctrl+F12: snapshot the local player struct; the next Ctrl+F12 logs what changed.
	namespace VRProbe
	{
		// Once per frame (game thread).
		void Update();
		// The camera task (TCameraTaskPlay this), watched with the player struct (Ctrl+F11).
		void NoteCameraTask( const void *task );
	}
}
