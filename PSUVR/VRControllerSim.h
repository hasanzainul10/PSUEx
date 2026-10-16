#pragma once

#include "VRRuntime.h"

namespace PSUVR
{
	// Emulated motion controllers for testing without the headset on (remote desktop, the
	// SteamVR null HMD). When on (vr.emulateControllers, Ctrl+Shift+F9) both hands' state comes from
	// here instead of the runtime: head-relative pose presets, buttons, sticks and swings,
	// driven by Ctrl+numpad hotkeys or a timed command file (vr.simScript, re-run whenever the
	// file changes). Every action logs a "VRSim:" line.
	namespace VRControllerSim
	{
		// Once per frame, before the hands are read: hotkeys, script, easing.
		void Update();
		bool Active();
		// Overwrites one hand's state (call only when Active()).
		void Fill( int hand, ControllerState &state );
		// Haptics go to the log while emulating.
		void NoteHaptic( int hand, float amplitude, float seconds );
	}
}
