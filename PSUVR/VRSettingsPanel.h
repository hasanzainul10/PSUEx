#pragma once

#include "VRSettings.h"

#include <functional>
#include <string>
#include <vector>

namespace PSUVR
{
	// What the host (in-game overlay or standalone configurator) offers the panel.
	struct VRPanelContext
	{
		bool inGame = false;                 // shows Pause VR and Reload
		std::vector< std::string > status;   // live status lines (in-game)
		std::function< bool() > save;        // returns true on success
		std::function< bool() > reload;      // re-read from file
		std::string message;                 // last save/reload result, shown in the footer
	};

	// Draws the VR settings into the current ImGui window and edits settings live.
	void DrawVRSettingsPanel( VRSettings &settings, VRPanelContext &context );

	// Display names for bindings.
	std::string VRKeyName( int virtualKey );
	std::string VRPadName( unsigned short padButton );
}
