#pragma once

#include "VRRuntime.h"

#include <d3d11.h>
#include <string>

namespace PSUVR
{
	// Phase 0 prototype: opens an OpenXR session with D3D11 and clears both eyes
	// to a solid color each frame. No game stereo yet.
	class OpenXRRuntime final : public IVRRuntime
	{
	public:
		bool Init( ID3D11Device *d3d11 ) override;
		void Shutdown() override;
		bool BeginFrame() override;
		bool AcquireSwapchains( ID3D11Texture2D **left, ID3D11Texture2D **right ) override;
		bool EndFrame() override;
		bool GetEyePose( int eye, EyePose &out ) override;
		const char *Name() const override { return "OpenXR"; }

		// Poll events without xrWaitFrame (safe to call before Present).
		void PumpEvents();
		bool SessionRunning() const { return m_sessionRunning; }

		// Clears acquired swapchain images to RGB and releases them.
		bool ClearEyes( float r, float g, float b );

		// Copies a D3D11 texture into one swapchain for a mono quad layer (virtual screen).
		bool SubmitSharedTexture( ID3D11Texture2D *src );

		// Recreate eye swapchains to match the game back-buffer (virtual screen).
		bool ResizeSwapchains( int32_t width, int32_t height );

		void SetVirtualScreenPlacement( float distanceMeters, float heightMeters );

		int32_t SwapchainWidth() const { return m_swapchainWidth; }
		int32_t SwapchainHeight() const { return m_swapchainHeight; }

	private:
		bool LoadLoader();
		bool CreateInstance();
		bool CreateSession( ID3D11Device *d3d11 );
		bool CreateSwapchains( int32_t width, int32_t height );
		void DestroySwapchains();
		int64_t PickSwapchainFormat( uint64_t sessionHandle );
		bool EnsureBlitPipeline();
		bool BlitTexture( ID3D11Texture2D *src, ID3D11Texture2D *dst );

		HMODULE m_loader = nullptr;
		bool m_ready = false;

		ID3D11VertexShader *m_blitVS = nullptr;
		ID3D11PixelShader *m_blitPS = nullptr;
		ID3D11SamplerState *m_blitSampler = nullptr;
		ID3D11RasterizerState *m_blitRS = nullptr;

		// Opaque OpenXR handles stored as uint64 to avoid requiring headers at
		// every include site; implementation uses real Xr* types.
		uint64_t m_instance = 0;
		uint64_t m_systemId = 0;
		uint64_t m_session = 0;
		uint64_t m_space = 0;     // LOCAL — stereo projection
		uint64_t m_viewSpace = 0; // VIEW — head-locked virtual screen quad
		uint64_t m_swapchain[ 2 ]{};
		int32_t m_swapchainWidth = 0;
		int32_t m_swapchainHeight = 0;
		int64_t m_swapchainFormat = 0;
		uint32_t m_swapchainImageCount[ 2 ]{};
		void *m_swapchainImages[ 2 ]{}; // XrSwapchainImageD3D11KHR*
		ID3D11Device *m_device = nullptr;
		ID3D11DeviceContext *m_context = nullptr;
		std::string m_lastError;
		bool m_frameStarted = false;
		bool m_sessionRunning = false;
		bool m_shouldRender = false;
		bool m_clearedThisFrame = false;
		bool m_submitQuadLayer = false; // virtual screen vs stereo clear
		int m_sessionState = 1; // XR_SESSION_STATE_IDLE
		int64_t m_displayTime = 0;
		uint32_t m_acquiredIndex[ 2 ]{};
		bool m_loggedSubmitFormats = false;
		float m_quadDistance = 2.0f;
		float m_quadHeight = 1.2f;
	};
}
