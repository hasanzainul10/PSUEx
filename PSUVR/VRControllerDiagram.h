#pragma once

#include <string>

namespace PSUVR
{
	// Draws both motion controllers with each button's game action (VRMotionLayout), plus
	// the both-grip chords, into the current ImGui window. profile: vr.motionProfile.
	void DrawControllerDiagram( const std::string &profile );
}
