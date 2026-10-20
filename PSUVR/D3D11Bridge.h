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

		// Transparent HUD capture targets at game resolution (user = visor, world = kiosk).
		bool EnsureHudTargets( IDirect3DDevice9 *d3d9, UINT width, UINT height );
		IDirect3DSurface9 *HudUserSurface() const { return m_hudUser.surface9; }
		IDirect3DSurface9 *HudWorldSurface() const { return m_hudWorld.surface9; }
		ID3D11Texture2D *HudUserTexture() const { return m_hudUser.texture11; }
		ID3D11Texture2D *HudWorldTexture() const { return m_hudWorld.texture11; }
		// Controller label cards (VRControllerLabels), a fixed small transparent target.
		bool EnsureLabelTarget( IDirect3DDevice9 *d3d9, UINT width, UINT height );
		IDirect3DSurface9 *LabelSurface() const { return m_label.surface9; }
		ID3D11Texture2D *LabelTexture() const { return m_label.texture11; }

		UINT HudWidth() const { return m_hudWidth; }
		UINT HudHeight() const { return m_hudHeight; }

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
		void ShutdownHud();
		bool WaitGpu( IDirect3DDevice9 *d3d9 );

		SharedTarget m_eyes[ 2 ];
		IDirect3DSurface9 *m_eyeDepth = nullptr;
		UINT m_eyeWidth = 0;
		UINT m_eyeHeight = 0;
		D3DFORMAT m_eyeDepthFormat = D3DFMT_UNKNOWN;

		SharedTarget m_hudUser;
		SharedTarget m_hudWorld;
		UINT m_hudWidth = 0;
		UINT m_hudHeight = 0;

		SharedTarget m_label;

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
