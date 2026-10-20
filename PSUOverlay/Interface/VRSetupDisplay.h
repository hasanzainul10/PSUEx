#pragma once

#include "IFWinCtrl.h"

// First-run VR setup wizard: play position (recenter), eye height (measured from the OpenXR
// STAGE floor), hand angle calibration and a controls tour. Opens on the first stereo frame
// while vr.setupDone is off (motion input), or from "Run VR setup again" in the VR settings.
// Like the settings window it is drawn on the HUD panel and the controller pointer drives it.
class VRSetupDisplay : public IFWinCtrl
{
public:
	VRSetupDisplay( const VRSetupDisplay & ) = delete;
	VRSetupDisplay &operator=( const VRSetupDisplay & ) = delete;

	VRSetupDisplay();
	~VRSetupDisplay() override = default;

	static std::shared_ptr< VRSetupDisplay > Create()
	{
		return std::make_shared< VRSetupDisplay >();
	}

	// "Run VR setup again": opens the wizard at its first step on the next frame.
	static void RequestOpen();

	void Render() override;

private:
	enum class Step : int { Welcome, Position, EyeHeight, Hands, Controls, Count };

	void Open();
	void Close( bool finished );
	bool StepShown( Step step ) const;
	void Go( int direction );

	void DrawWelcome();
	void DrawPosition();
	void DrawEyeHeight();
	void DrawHands();
	void DrawControls();

	Step m_step = Step::Welcome;
	bool m_motion = false;      // motion input when opened: the hands and controls steps
	bool m_autoChecked = false; // the first stereo frame was checked for the automatic open
	bool m_recentered = false;
	bool m_measured = false;
	float m_eyeHeightOnEntry = 0.0f;
	int m_calibRequestOnEntry = 0;
};
