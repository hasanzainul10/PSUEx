#pragma once

struct IDirect3DDevice9;

namespace PSUVR
{
	// Remaps D3DPOOL_MANAGED creates to DEFAULT+DYNAMIC on a D3D9Ex device
	// (managed pool is invalid under Ex and returns D3DERR_INVALIDCALL).
	bool InstallD3D9ExCompatHooks( IDirect3DDevice9 *device );
	void LogD3D9ExCompatStats();
}
