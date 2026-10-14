#pragma once

#include "VRRuntime.h"

struct IDirect3DDevice9;
struct IDirect3DSurface9;

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
		void ConsumeFrame() { m_hasFrame = false; }

		// True while pass() runs for an eye. Used by D3D hooks to skip flat-sourced
		// screen-space effect quads that would ghost the mono camera over stereo.
		static bool InEyePass();

		// Track RT textures written during the current eye pass (SetRT / StretchRect dest).
		static void OnSetRenderTarget( IDirect3DSurface9 *target );
		static void OnStretchRect( IDirect3DSurface9 *source, IDirect3DSurface9 *dest, void *ret );

		// XYZRHW draws that sample an RT texture not written this eye pass → skip.
		static bool ShouldSkipScreenQuad( IDirect3DDevice9 *device, void *ret, unsigned primitiveCount, bool userPointer );

	private:
		StereoRenderer() = default;

		static void BeginEyePass( IDirect3DSurface9 *eyeTarget );
		static void EndEyePass();
		static void MarkSurfaceTexture( IDirect3DSurface9 *surface );

		bool m_hasFrame = false;
		bool m_loggedFirst = false;
		EyePose m_poses[ 2 ]{};
		EyePose m_viewSpacePoses[ 2 ]{};
	};
}
