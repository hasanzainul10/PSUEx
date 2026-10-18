#pragma once

#include <cstdint>

namespace PSUVR
{
	// First person: the player's arms reach for the motion controllers. The loaded
	// character's bone pose (model space, one 4x4 per bone, from 0x0074B760) is edited
	// right after the game builds it: the upper arm and forearm subtrees are rotated
	// (two-bone IK) so the wrist lands on the controller grip.
	namespace VRHands
	{
		// model = the player's TDivModel, out = its bone matrices (model +0x64),
		// skeleton = its skeleton (+0x28 bone count, +0x30 bone records).
		void Apply( void *model, float *out, const uint8_t *skeleton );
		// After the local player's draw callback: measures the model's world matrix through
		// the game's bone query (which re-runs the pose build, so not from inside Apply).
		void AfterPlayerDraw( void *model );
		// The pose has the head folded away (vr.fpBody "headless"): no neck clip plane needed.
		bool HeadHidden();
		// A bullet started at this muzzle (game world): learns the gun's barrel direction in
		// the hand's weapon bone, so free aim can point the barrel along the shot.
		void NoteMuzzle( const float start[ 3 ] );
	}
}
