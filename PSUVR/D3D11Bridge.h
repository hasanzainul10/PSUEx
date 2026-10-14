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

		// Per-eye 3D targets: shared D3D9Ex render targets (opened in D3D11) plus
		// one private depth surface reused by both eyes.
		bool EnsureEyeTargets( IDirect3DDevice9 *d3d9, UINT width, UINT height, D3DFORMAT depthFormat );
		IDirect3DSurface9 *EyeSurface( int eye ) const { return m_eyes[ eye ].surface9; }
		IDirect3DSurface9 *EyeDepth() const { return m_eyeDepth; }
		ID3D11Texture2D *EyeTexture( int eye ) const { return m_eyes[ eye ].texture11; }

		// GPU fence so D3D11 only reads finished D3D9 rendering.
		bool Fence( IDirect3DDevice9 *d3d9 ) { return WaitGpu( d3d9 ); }

	private:
		struct SharedTarget
		{
			IDirect3DTexture9 *texture9 = nullptr;
			IDirect3DSurface9 *surface9 = nullptr;
			ID3D11Texture2D *texture11 = nullptr;
		};

		bool RecreateShared( IDirect3DDevice9 *d3d9, ID3D11Device *d3d11, UINT width, UINT height );
		bool CreateSharedTarget( IDirect3DDevice9 *d3d9, UINT width, UINT height, SharedTarget &out );
		void ReleaseSharedTarget( SharedTarget &target );
		void ShutdownEyes();
		bool WaitGpu( IDirect3DDevice9 *d3d9 );

		SharedTarget m_eyes[ 2 ];
		IDirect3DSurface9 *m_eyeDepth = nullptr;
		UINT m_eyeWidth = 0;
		UINT m_eyeHeight = 0;
		D3DFORMAT m_eyeDepthFormat = D3DFMT_UNKNOWN;

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
