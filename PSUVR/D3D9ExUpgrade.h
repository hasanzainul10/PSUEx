#pragma once

#include <d3d9.h>

namespace PSUVR
{
	// When enabled, Direct3DCreate9 is redirected to Direct3DCreate9Ex so the game
	// device can share surfaces with D3D11 / OpenXR.
	bool InstallD3D9ExUpgrade();
	void RemoveD3D9ExUpgrade();
	bool IsD3D9ExActive();

	// Prefer CreateDeviceEx when the D3D object is Ex. On failure, marks Ex
	// inactive and returns a failure HRESULT so the caller can fall back.
	HRESULT TryCreateDeviceEx(
		IDirect3D9 *pD3D,
		UINT Adapter,
		D3DDEVTYPE DeviceType,
		HWND hFocusWindow,
		DWORD BehaviorFlags,
		D3DPRESENT_PARAMETERS *pPresentationParameters,
		IDirect3DDevice9 **ppReturnedDeviceInterface );
}
