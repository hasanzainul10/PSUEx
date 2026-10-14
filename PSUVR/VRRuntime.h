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
	};
}
