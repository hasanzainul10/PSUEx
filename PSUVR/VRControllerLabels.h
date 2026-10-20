#pragma once

#include "VRRuntime.h"

struct IDirect3DDevice9;
struct ImFont;

namespace PSUVR
{
	class D3D11Bridge;
	class OpenXRRuntime;

	// Label cards floating above the motion controllers, listing what each button does now
	// (VRSyntheticPad::DescribeControls). A card shows when you look down at its controller
	// (vr.controllerLabels; both grips + right stick click toggles), and both show while a
	// grip is held still (vr.controllerLabelsGrip) with that grip's context. Both cards share
	// one texture (left half / right half), submitted as two quad layers.
	namespace VRControllerLabels
	{
		constexpr unsigned kTextureWidth = 1024;
		constexpr unsigned kTextureHeight = 512;

		// A larger font for the cards (loaded with the overlay's font atlas); default font if null.
		void SetFont( ImFont *font );
		// Once per frame after VRSyntheticPad::Update. views: the next eye poses (LOCAL), or
		// nullptr when unknown (the cards hide).
		void Update( const EyePose *views );
		// Draws the visible cards into the bridge's label target (overlay, after ImGui::Render).
		void Render( IDirect3DDevice9 *device, D3D11Bridge *bridge );
		// Hands the drawn cards and their poses to the runtime (before EndFrame).
		void Submit( OpenXRRuntime *runtime, D3D11Bridge *bridge );
	}
}
