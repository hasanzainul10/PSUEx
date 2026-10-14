#pragma once

#include "VRRuntime.h"

struct IDirect3DDevice9;

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
		void ConsumeFrame() { m_hasFrame = false; }

	private:
		StereoRenderer() = default;

		bool m_hasFrame = false;
		bool m_loggedFirst = false;
		EyePose m_poses[ 2 ]{};
	};
}
