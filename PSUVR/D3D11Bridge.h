#pragma once

#include <d3d9.h>
#include <d3d11.h>

namespace PSUVR
{
	// Shared D3D9Ex rendertarget opened in D3D11 for OpenXR submit.
	class D3D11Bridge
	{
	public:
		bool Init( IDirect3DDevice9 *d3d9, ID3D11Device *d3d11, UINT preferredWidth = 0, UINT preferredHeight = 0 );
		void Shutdown();
		bool EnsureSize( IDirect3DDevice9 *d3d9, UINT width, UINT height );
		// StretchRect BB -> shared, then GPU-fence so D3D11 can safely read.
		bool BlitBackBuffer( IDirect3DDevice9 *d3d9 );
		ID3D11Texture2D *GetSharedTexture() const { return m_d3d11Texture; }
		bool Ready() const { return m_d3d11Texture != nullptr && m_d3d9Surface != nullptr; }

	private:
		bool RecreateShared( IDirect3DDevice9 *d3d9, ID3D11Device *d3d11, UINT width, UINT height );
		bool WaitGpu( IDirect3DDevice9 *d3d9 );

		IDirect3DTexture9 *m_d3d9Texture = nullptr;
		IDirect3DSurface9 *m_d3d9Surface = nullptr;
		IDirect3DQuery9 *m_eventQuery = nullptr;
		ID3D11Texture2D *m_d3d11Texture = nullptr;
		ID3D11Device *m_d3d11 = nullptr;
		HANDLE m_sharedHandle = nullptr;
		UINT m_width = 0;
		UINT m_height = 0;
	};
}
