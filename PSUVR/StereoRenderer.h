#pragma once

#include "VRRuntime.h"

#include <d3d9.h>

struct IDirect3DDevice9;
struct IDirect3DSurface9;
struct _D3DVIEWPORT9;
typedef struct _D3DVIEWPORT9 D3DVIEWPORT9;
typedef struct tagRECT RECT;

namespace PSUVR
{
	// Synchronized sequential stereo: after the game's own 3D pass, the same pass
	// (0x004FD990) runs once per eye into the bridge's eye targets, in the same tick.
	class StereoRenderer
	{
	public:
		using Pass = void( __cdecl * )();

		static StereoRenderer &Get();

		void RenderEyes( IDirect3DDevice9 *device, Pass pass );

		// True from RenderEyes until the frame is submitted.
		bool HasFrame() const { return m_hasFrame; }
		const EyePose *RenderedPoses() const { return m_poses; }
		const EyePose *RenderedViewSpacePoses() const { return m_viewSpacePoses; }
		void ConsumeFrame() { m_hasFrame = false; m_haveEyeMatrices = false; }

		unsigned EyeWidth() const { return m_lastEyeW; }
		unsigned EyeHeight() const { return m_lastEyeH; }
		unsigned GameWidth() const { return m_lastGameW; }
		unsigned GameHeight() const { return m_lastGameH; }

		// True while pass() runs for an eye. Used by D3D hooks to skip flat-sourced
		// screen-space effect quads that would ghost the mono camera over stereo.
		static bool InEyePass();
		// vr.enabled + vr.stereo and the current scene matches vr.stereoMode (town by default).
		static bool SceneAllowsStereo();
		// True while remapping Pass B nameplates/bubbles onto eye RTs (skip HUD capture).
		static bool InNameplateDraw();

		// Debug diagnostics for screen-space (XYZRHW) effect draws ("VREffect:" log). where: 0 eye
		// pass, 1 pass B / UI, 2 elsewhere. verts may be null (vertex buffer draws). Release: no-op.
		static void NoteEffectDraw( IDirect3DDevice9 *device, int where, unsigned primitiveType, unsigned primitiveCount,
			const void *verts, unsigned stride, void *ret );
		// A technique is being cast: effect draws in the next 2 s are tagged "tech".
		static void NoteTechnique();

		// Unproject game XYZRHW (Pass B nameplate/bubble) and draw onto both eye RTs.
		void DrawNameplateToEyes( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride );

		// Track RT textures written during the current eye pass (SetRT / StretchRect dest).
		static void OnSetRenderTarget( IDirect3DSurface9 *target );
		static void OnStretchRect( IDirect3DSurface9 *source, IDirect3DSurface9 *dest, void *ret );

		// XYZRHW draws that sample an RT texture not written this eye pass → skip.
		static bool ShouldSkipScreenQuad( IDirect3DDevice9 *device, void *ret, unsigned primitiveCount, bool userPointer );

		// If this DP-UP is a near-full-screen XYZRHW quad sized for the game RT, return a
		// rewritten vertex buffer scaled to the eye RT. Otherwise return nullptr (use original).
		static const void *StretchScreenQuadIfNeeded( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride, void *ret );
		// Eye pass: a world sprite the game projected with the flat camera (2D sprite draw with a
		// projected depth), re-projected for the current eye. Null = draw as is.
		// Eye pass: a world matrix that puts a model at the game camera (sky box) is moved to the
		// current eye. True = use `out`.
		static bool RecenterCameraAnchored( const D3DMATRIX &world, D3DMATRIX &out, void *ret );
		static const void *ReprojectSpriteIfNeeded( IDirect3DDevice9 *device, unsigned primitiveType,
			unsigned primitiveCount, const void *verts, unsigned stride, void *ret );

		// True when RT0 is the current eye color target (shadow RTs return false).
		static bool EyeColorTargetBound( IDirect3DDevice9 *device );
		static bool EyeColorTargetIs( IDirect3DSurface9 *surface );
		static void GetEyeViewport( D3DVIEWPORT9 &out );
		static void GetEyeScissor( RECT &out );

	private:
		StereoRenderer() = default;

		static void BeginEyePass( IDirect3DSurface9 *eyeTarget, unsigned eyeW, unsigned eyeH,
			unsigned gameW, unsigned gameH );
		static void EndEyePass();
		static void MarkSurfaceTexture( IDirect3DSurface9 *surface );
		// The eye pose of the next eye pass (dressing room background placement).
		static void SetPassEye( const EyePose &eye );

		bool m_hasFrame = false;
		bool m_loggedFirst = false;
		bool m_haveEyeMatrices = false;
		bool m_loggedNameplate = false;
		unsigned m_lastEyeW = 0;
		unsigned m_lastEyeH = 0;
		unsigned m_lastGameW = 0;
		unsigned m_lastGameH = 0;
	public:
		// Game PROJ captured for the current stereo frame (pre-transformed 2D was built with it).
		const D3DMATRIX &GameProj() const { return m_gameProj; }
	private:
		D3DMATRIX m_gameProj{};
		D3DMATRIX m_invGameProj{};
		D3DMATRIX m_invGameView{};
		D3DMATRIX m_gameView{};
		D3DMATRIX m_invEyeView[ 2 ]{};  // each eye in game world
		bool m_haveEyePosition[ 2 ]{};
		bool m_haveInvGameView = false;
		D3DMATRIX m_eyeView[ 2 ]{};
		D3DMATRIX m_eyeProj[ 2 ]{};
		EyePose m_poses[ 2 ]{};
		EyePose m_viewSpacePoses[ 2 ]{};
	};
}
