#pragma once

struct IDirect3DDevice9;
struct ID3D11Texture2D;

namespace PSUVR
{
	// Captures town UI onto transparent shared RTs for OpenXR quad layers
	// (head-locked personal HUD + world-locked shop/dialog panel). Never paints eyes.
	class VRHud
	{
	public:
		static VRHud &Get();

		// Clear both HUD RTs at game resolution. Call once per stereo frame after Pass A.
		void BeginStereoFrame( IDirect3DDevice9 *device, unsigned gameW, unsigned gameH );

		bool Active() const { return m_active; }
		bool Capturing() const { return m_capturing; }

		// After the game's Pass B DP-UP: replay onto the world HUD RT (do not re-run Pass B).
		void CapturePassBDraw( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride );

		// After 0x793C40: replay onto user or world RT by widget AABB.
		void CaptureUiDraw( IDirect3DDevice9 *device,
			void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType,
			int( __thiscall *drawFn )( void *, int, int, void *, int ) );

		ID3D11Texture2D *UserTexture() const;
		ID3D11Texture2D *WorldTexture() const;
		bool HasUserContent() const { return m_hasUser; }
		bool HasWorldContent() const { return m_hasWorld; }

		void EndStereoFrame() { m_active = false; }

	private:
		VRHud() = default;

		bool EnsureTargets( IDirect3DDevice9 *device, unsigned w, unsigned h );
		void ReplayOnto( IDirect3DDevice9 *device, bool worldLayer,
			void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType,
			int( __thiscall *drawFn )( void *, int, int, void *, int ) );
		void ReplayPassBOntoWorld( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride );
		static bool IsWorldUiWidget( const void *verts, int vertexCount, unsigned stride,
			unsigned gameW, unsigned gameH );
		// Nameplates / speech bubbles: small-to-medium Pass B quads above mid-screen.
		static bool IsAvatarAnchoredPassB( const void *verts, unsigned primitiveType,
			unsigned primitiveCount, unsigned stride, unsigned gameW, unsigned gameH );

		bool m_active = false;
		bool m_capturing = false;
		bool m_hasUser = false;
		bool m_hasWorld = false;
		bool m_logged = false;
		unsigned m_w = 0;
		unsigned m_h = 0;
	};
}
