#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11Texture2D;

namespace PSUVR
{
	enum class RuntimeKind
	{
		OpenXR,
		OpenVR
	};

	struct EyePose
	{
		float position[ 3 ];
		float orientation[ 4 ]; // x,y,z,w
		float fovAngleLeft;
		float fovAngleRight;
		float fovAngleUp;
		float fovAngleDown;
	};

	// Motion controller buttons (ControllerState::buttons).
	enum ControllerButton : uint32_t
	{
		kControllerTrigger = 1u << 0,
		kControllerGrip = 1u << 1,
		kControllerStick = 1u << 2,     // thumbstick click
		kControllerPrimary = 1u << 3,   // A / X
		kControllerSecondary = 1u << 4, // B / Y
		kControllerMenu = 1u << 5,
	};

	// One motion controller, in the same space as the eye poses (LOCAL, meters),
	// predicted for the same display time as the next views.
	struct ControllerState
	{
		bool active = false;         // the runtime is tracking / reading this hand
		bool gripValid = false;
		bool aimValid = false;
		bool velocityValid = false;
		float gripPosition[ 3 ]{};   // where the hand holds the controller
		float gripOrientation[ 4 ]{ 0.0f, 0.0f, 0.0f, 1.0f };
		float aimPosition[ 3 ]{};    // pointing ray origin, looks down -Z
		float aimOrientation[ 4 ]{ 0.0f, 0.0f, 0.0f, 1.0f };
		float linearVelocity[ 3 ]{}; // grip, m/s
		float angularVelocity[ 3 ]{};
		float trigger = 0.0f;
		float grip = 0.0f;
		float stick[ 2 ]{};
		uint32_t buttons = 0;        // ControllerButton bits
	};

	class IVRRuntime
	{
	public:
		virtual ~IVRRuntime() = default;

		virtual bool Init( ID3D11Device *d3d11 ) = 0;
		virtual void Shutdown() = 0;
		virtual bool BeginFrame() = 0;
		virtual bool AcquireSwapchains( ID3D11Texture2D **left, ID3D11Texture2D **right ) = 0;
		virtual bool EndFrame() = 0;
		virtual bool GetEyePose( int eye /*0=left,1=right*/, EyePose &out ) = 0;
		virtual const char *Name() const = 0;

		// hand: 0 = left, 1 = right. Valid after the frame's input update.
		virtual bool GetControllerState( int /*hand*/, ControllerState &out ) { out = {}; return false; }
		virtual void Haptic( int /*hand*/, float /*amplitude*/, float /*seconds*/ ) {}
	};
}
