#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

#include <imgui.h>

class Config {
private:
	struct s_window_info {
		bool m_enabled;
		ImVec2 m_position;
		ImVec2 m_size;
	};

public:
	static Config &Get()
	{
		static Config instance;
		return instance;
	}

	Config( const Config & ) = delete;
	Config &operator=( const Config & ) = delete;

	Config();
	~Config();

	void SaveToFile();
	void LoadFromFile();

public:
	ImGuiStyle m_style;

	struct s_config {
		s_window_info m_windowInfo;
	} config;

	struct s_loot {
		s_window_info m_windowInfo;

		bool m_displayMission;

		int8_t m_minimumElementPercent;
		int8_t m_minimumRarity;
		int8_t m_maximumRarity;

		bool m_colorItemByElement;
		ImColor m_elementColor[ 7 ];
		
		struct s_float_item {
			bool m_displayItem;
			bool m_colorItemByElement;
			bool m_displayElementIcon;
			bool m_displayItemIcon;
			bool m_displayItemRarity;
			bool m_displayFade;
			int m_iconDisplayType;
			float m_floatHeight;
			float m_fadeDistance;
		} hover;
		
		std::array< bool, 4 > m_columnVisibility;
		std::array< bool, 20 > m_itemTypeFilter;
		std::array< bool, 28 > m_weaponTypeFilter;
		std::array< bool, 7 > m_elementFilter;
	} loot_reader;

	struct s_camera {
		s_window_info m_windowInfo;

		float m_smoothing;
		float m_refocusSpeed;
		float m_fov;
		float m_followDistance;
		float m_followHeight;

		float m_rotationSpeedH;
		float m_rotationSpeedV;
		float m_turnAcceleration;
	} camera;

	struct s_vr {
		bool m_enabled = false;
		bool m_forceD3D9Ex = false;
		// Ex upgrade + CreateDeviceEx + managed-pool remap, but no OpenXR/bridge.
		bool m_d3d9exOnly = false;
		bool m_solidColorPrototype = false; // true = teal clear; false = virtual-screen blit
		std::string m_runtime = "openxr";
		std::string m_cameraMode = "thirdPerson";
		std::string m_inputMode = "gamepad";
		float m_worldScale = 1.0f;
		float m_ipdScale = 1.0f;
		float m_hudDistance = 2.0f;
		float m_hudSize = 1.0f;
		bool m_snapTurn = true;
		bool m_comfortVignette = true;
		// Render the 3D pass per eye (projection layer) with the game frame as a panel.
		bool m_stereo = true;
		// During eye passes, skip XYZRHW quads that sample an RT filled only by the flat pass
		// (glare / afterimage ghosts the mono camera over stereo). Shadows rewritten per eye stay.
		bool m_stereoSkipScreenQuads = true;
		// When stereo is on, also submit the flat game frame as a world-locked quad (HUD stand-in).
		// Default off — the panel reads as a dark PiP sliding over the stereo floor.
		bool m_stereoPanel = false;
		// Debug: shift the 3D pass sideways by ±this many view units, alternating each second.
		float m_debugEyeShift = 0.0f;
		// Debug: run the 3D pass twice per frame to test repeat-safety.
		bool m_debugDoublePass = false;
	} vr;
};