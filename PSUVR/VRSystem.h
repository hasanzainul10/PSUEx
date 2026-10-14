#pragma once

#include "VRRuntime.h"

struct IDirect3D9;
struct IDirect3DDevice9;
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace PSUVR
{
	class D3D11Bridge;
	class OpenXRRuntime;

	class System
	{
	public:
		static System &Get();

		void OnProcessAttach();
		void OnProcessDetach();
		void OnDirect3DCreated( IDirect3D9 *d3d9 );
		void OnDeviceCreated( IDirect3DDevice9 *device );

		// Split around the game Present so WaitFrame cannot black-screen the monitor.
		void OnPresentCapture(); // StretchRect + GPU fence (before Present)
		void OnPresentSubmit();  // OpenXR wait/blit/end (after Present)

		bool Enabled() const { return m_enabled; }

		// Stereo inputs for StereoRenderer; null/false until OpenXR and the bridge are live.
		D3D11Bridge *StereoBridge() const;
		bool NextViews( EyePose localOut[ 2 ], EyePose viewSpaceOut[ 2 ] ) const;
		bool EyeSwapchainSize( uint32_t &width, uint32_t &height ) const;

	private:
		System() = default;

		void LoadConfig();
		bool EnsureD3D11();
		bool EnsureRuntime();
		bool EnsureBridge();

		bool m_enabled = false;
		bool m_forceD3D9Ex = false;
		bool m_d3d9exOnly = false;
		bool m_solidColorPrototype = false;
		bool m_d3d9ExInstalled = false;
		bool m_runtimeReady = false;
		bool m_runtimeFailed = false;
		bool m_bridgeReady = false;
		bool m_loggedVirtualScreen = false;
		bool m_capturedThisFrame = false;
		bool m_haveNextViews = false;
		bool m_loggedStereo = false;
		bool m_recenterKeyDown = false;
		EyePose m_nextViews[ 2 ]{};
		EyePose m_nextViewSpacePoses[ 2 ]{};

		IDirect3DDevice9 *m_device = nullptr;
		ID3D11Device *m_d3d11 = nullptr;
		ID3D11DeviceContext *m_d3d11Context = nullptr;
		OpenXRRuntime *m_xr = nullptr;
		D3D11Bridge *m_bridge = nullptr;
	};
}
