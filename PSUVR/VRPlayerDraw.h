#pragma once

#include <cstdint>

struct IDirect3DDevice9;

namespace PSUVR
{
	// Knows when the game is drawing the local player's character, so first person can
	// cut its head away (or hide it) in the eye passes. Each object in the draw layers is
	// drawn by one callback (node +0x10) called from PSU_DrawLayer 0x00560830; the local
	// player's node gets a thunk that brackets the original callback.
	namespace VRPlayerDraw
	{
		// Once per stereo frame before the eye passes. enable = first person is active;
		// false restores every patched callback.
		void Update( bool enable );

		// Inside the local player's draw callback.
		bool DrawingLocalPlayer();

		// Right after 0x0074B760 wrote a model's bone-indexed, model-space bone matrices
		// (out = model +0x64 buffer). The loaded player character takes this path.
		void OnBonePose( float *out, const uint8_t *skeleton );
	}

	// Wraps one D3D draw in the device hooks. While the local player is drawn in a
	// first-person eye pass: vr.fpBody "headless" clips everything above the neck with
	// user clip plane 0, "hidden" skips the draw, "full" leaves it alone.
	class LocalPlayerDrawScope
	{
	public:
		explicit LocalPlayerDrawScope( IDirect3DDevice9 *device );
		~LocalPlayerDrawScope();

		LocalPlayerDrawScope( const LocalPlayerDrawScope & ) = delete;
		LocalPlayerDrawScope &operator=( const LocalPlayerDrawScope & ) = delete;

		bool Skip() const { return m_skip; }

	private:
		IDirect3DDevice9 *m_device = nullptr;  // set only when the clip plane was applied
		bool m_skip = false;
		unsigned long m_savedClipEnable = 0;
		float m_savedPlane[ 4 ]{};
	};
}
