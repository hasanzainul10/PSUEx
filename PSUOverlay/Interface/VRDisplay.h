#pragma once

#include "IFWinCtrl.h"
#include "../../PSUVR/VRSettingsPanel.h"

// In-game VR settings window (Insert menu > VR Settings, or the "Show / hide VR
// settings" binding). Also draws VR shortcut toasts. While a stereo frame is active
// the overlay is drawn into the HUD panel too, so this is usable in the headset.
class VRDisplay : public IFWinCtrl
{
public:
	VRDisplay( const VRDisplay & ) = delete;
	VRDisplay &operator=( const VRDisplay & ) = delete;

	VRDisplay();
	~VRDisplay() override = default;

	static std::shared_ptr< VRDisplay > Create()
	{
		return std::make_shared< VRDisplay >();
	}

	void Render() override;

private:
	void DrawToast();

	PSUVR::VRPanelContext m_context;
};
