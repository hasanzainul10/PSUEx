#pragma once

// Short gated D3D/UI breadcrumb log for finding the 3D scene vs UI split.
// Present-based: first kMaxFrames after first Present.
// Scene-callback: light ENTER/LEAVE for first kMaxCallbackFrames; DIP sampling
// stays armed until enough unique returns are collected (title does not burn budget).
// After TCamera/gameplay, also samples DIPs outside SceneCB and post-UI DIPs inside.

#include <d3d9.h>

namespace FrameTrace
{
	constexpr unsigned kMaxFrames = 8;
	constexpr unsigned kMaxCallbackFrames = 6;
	constexpr unsigned kMaxEventsPerKind = 24;
	constexpr unsigned kMaxDipSamples = 16;
	constexpr unsigned kMaxGameplayLeaveLogs = 12;

	bool Active();
	bool InSceneCallback();
	bool GameplayArmed();

	// Call once when play camera is live so town/mission DIP capture re-arms.
	void NotifyGameplay();

	void SceneCallbackEnter( unsigned callbackAddress );
	void SceneCallbackLeave();

	// Render passes called from every scene callback: 0x004FD990 (layers 0x02-0x29)
	// and 0x004FDBC0 (layers 0x2B-0x3F). Logs per-pass draw/UI/matrix counts.
	void PassEnter( unsigned passAddress );
	void PassLeave();
	unsigned CurrentPassAddress();
	void OnDrawPrimitive( bool userPointer );
	void OnVertexShaderConstantF( unsigned startRegister, unsigned vectorCount, void *returnAddress );

	void OnBeginScene( void *returnAddress );
	void OnEndScene( void *returnAddress );
	void OnSetRenderTarget( IDirect3DDevice9 *device, DWORD index, IDirect3DSurface9 *rt, void *returnAddress );
	void OnSetTransform( D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix, void *returnAddress );
	void OnUiDraw( void *returnAddress );
	void OnDrawIndexed( void *returnAddress );
	void OnPresent();
}
