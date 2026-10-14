#pragma once

struct IDirect3DDevice9;
struct ID3D11Texture2D;

namespace PSUVR
{
	// Captures town UI onto transparent shared RTs for OpenXR quad layers
	// (head-locked personal HUD + world-locked shop/dialog panel).
	// Avatar nameplates/bubbles are remapped onto stereo eyes separately.
	class VRHud
	{
	public:
		static VRHud &Get();

		// Clear both HUD RTs at game resolution. Call once per stereo frame after Pass A.
		void BeginStereoFrame( IDirect3DDevice9 *device, unsigned gameW, unsigned gameH );

		bool Active() const { return m_active; }
		bool Capturing() const { return m_capturing; }
		// While 0x793C40 runs: Pass B DP-UPs are still captured; count them for UI-replay fallback.
		void BeginUiDraw() { m_inUiDraw = true; m_uiPassBCaptures = 0; }
		void EndUiDraw() { m_inUiDraw = false; }
		bool InUiDraw() const { return m_inUiDraw; }
		unsigned UiPassBCaptures() const { return m_uiPassBCaptures; }

		// After a 2D DP-UP (Pass B or later UI): eyes / user visor / world kiosk.
		void CapturePassBDraw( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride, void *ret );

		// Font / glyph batches: XYZRHW DrawIndexedPrimitive → HUD RTs.
		void CaptureXyzrhwIndexed( IDirect3DDevice9 *device, unsigned primitiveType,
			int baseVertexIndex, unsigned minIndex, unsigned numVertices,
			unsigned startIndex, unsigned primitiveCount, void *ret );

		// Font batches already in system memory (DIP-UP).
		void CaptureXyzrhwIndexedUp( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned minVertexIndex, unsigned numVertices, unsigned primitiveCount,
			const void *indexData, unsigned indexFormat, const void *verts, unsigned stride, void *ret );

		// Fallback when 0x793C40 issued no Pass B DP-UP (no eyes path).
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
		void ReplayPassBOnto( IDirect3DDevice9 *device, bool worldLayer, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride, bool rememberAabb = true );
		// Expanded font triangle list: route per glyph quad, one replay per layer.
		void ReplayFontBatch( IDirect3DDevice9 *device, const unsigned char *verts,
			unsigned vertCount, unsigned stride, void *ret );
		// true = HudWorld, false = HudUser. Uses sticky world before visor zones.
		bool ClassifyWorldLayer( const void *verts, unsigned vertCount, unsigned stride ) const;
		bool AlreadyCapturedUserAabb( float minX, float minY, float maxX, float maxY ) const;
		void RememberUserAabb( float minX, float minY, float maxX, float maxY );
		void RememberWorldAabb( float minX, float minY, float maxX, float maxY );
		bool NearExistingUserAabb( float cx, float cy, float pad ) const;
		// Current render target is the game-sized frame (not a render-to-texture pass).
		bool TargetsGameFrame( IDirect3DDevice9 *device ) const;
		bool NearExistingWorldAabb( float cx, float cy, float pad ) const;
		static bool IsWorldUiWidget( const void *verts, int vertexCount, unsigned stride,
			unsigned gameW, unsigned gameH );
		static bool IsAvatarAnchoredPassB( const void *verts, unsigned primitiveType,
			unsigned primitiveCount, unsigned stride, unsigned gameW, unsigned gameH );
		bool IsVisorChrome( float minX, float minY, float maxX, float maxY,
			unsigned gameW, unsigned gameH ) const;

		bool m_active = false;
		bool m_capturing = false;
		bool m_inUiDraw = false;
		unsigned m_uiPassBCaptures = 0;
		bool m_hasUser = false;
		bool m_hasWorld = false;
		bool m_logged = false;
		unsigned m_w = 0;
		unsigned m_h = 0;

		static constexpr unsigned kMaxLayerAabbs = 96;
		struct Aabb { float minX, minY, maxX, maxY; };
		Aabb m_userAabbs[ kMaxLayerAabbs ]{};
		unsigned m_userAabbCount = 0;
		Aabb m_worldAabbs[ kMaxLayerAabbs ]{};
		unsigned m_worldAabbCount = 0;
	};
}
