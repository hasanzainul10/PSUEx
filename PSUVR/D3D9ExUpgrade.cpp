#include "D3D9ExUpgrade.h"

#include "../Misc/Log.h"
#include "detours.h"

#include <cstdio>

namespace PSUVR
{
	using Direct3DCreate9_t = IDirect3D9 * ( WINAPI * )( UINT );
	using Direct3DCreate9Ex_t = HRESULT( WINAPI * )( UINT, IDirect3D9Ex ** );

	static Direct3DCreate9_t s_originalCreate9 = nullptr;
	static Direct3DCreate9Ex_t s_create9Ex = nullptr;
	static bool s_installed = false;
	static bool s_activeEx = false;

	static IDirect3D9 *WINAPI
	Hook_Direct3DCreate9( UINT sdkVersion )
	{
		if( s_create9Ex )
		{
			IDirect3D9Ex *ex = nullptr;
			HRESULT hr = s_create9Ex( sdkVersion, &ex );
			if( SUCCEEDED( hr ) && ex )
			{
				s_activeEx = true;
				PSU_LOG( "D3D9ExUpgrade: Direct3DCreate9Ex succeeded (%p)\n", ex );
				return ex;
			}
			PSU_LOG( "D3D9ExUpgrade: Direct3DCreate9Ex failed hr=0x%08X, falling back\n", hr );
		}
		s_activeEx = false;
		return s_originalCreate9 ? s_originalCreate9( sdkVersion ) : nullptr;
	}

	bool InstallD3D9ExUpgrade()
	{
		if( s_installed )
		{
			return true;
		}

		HMODULE d3d9 = GetModuleHandleA( "d3d9.dll" );
		if( !d3d9 )
		{
			d3d9 = LoadLibraryA( "d3d9.dll" );
		}
		if( !d3d9 )
		{
			PSU_LOG( "D3D9ExUpgrade: d3d9.dll not loaded\n" );
			return false;
		}

		s_originalCreate9 = reinterpret_cast< Direct3DCreate9_t >( GetProcAddress( d3d9, "Direct3DCreate9" ) );
		s_create9Ex = reinterpret_cast< Direct3DCreate9Ex_t >( GetProcAddress( d3d9, "Direct3DCreate9Ex" ) );
		if( !s_originalCreate9 || !s_create9Ex )
		{
			PSU_LOG( "D3D9ExUpgrade: missing Direct3DCreate9/Ex exports\n" );
			return false;
		}

		DetourTransactionBegin();
		DetourUpdateThread( GetCurrentThread() );
		DetourAttach( &( PVOID & )s_originalCreate9, Hook_Direct3DCreate9 );
		LONG err = DetourTransactionCommit();
		if( err != NO_ERROR )
		{
			PSU_LOG( "D3D9ExUpgrade: DetourAttach failed %ld\n", err );
			return false;
		}

		s_installed = true;
		PSU_LOG( "D3D9ExUpgrade: installed\n" );
		return true;
	}

	void RemoveD3D9ExUpgrade()
	{
		if( !s_installed || !s_originalCreate9 )
		{
			return;
		}
		DetourTransactionBegin();
		DetourUpdateThread( GetCurrentThread() );
		DetourDetach( &( PVOID & )s_originalCreate9, Hook_Direct3DCreate9 );
		DetourTransactionCommit();
		s_installed = false;
		PSU_LOG( "D3D9ExUpgrade: removed\n" );
	}

	bool IsD3D9ExActive()
	{
		return s_activeEx;
	}

	HRESULT TryCreateDeviceEx(
		IDirect3D9 *pD3D,
		UINT Adapter,
		D3DDEVTYPE DeviceType,
		HWND hFocusWindow,
		DWORD BehaviorFlags,
		D3DPRESENT_PARAMETERS *pPresentationParameters,
		IDirect3DDevice9 **ppReturnedDeviceInterface )
	{
		if( !s_activeEx || !pD3D || !pPresentationParameters || !ppReturnedDeviceInterface )
		{
			return E_FAIL;
		}

		IDirect3D9Ex *d3dEx = nullptr;
		HRESULT hr = pD3D->QueryInterface( __uuidof( IDirect3D9Ex ), reinterpret_cast< void ** >( &d3dEx ) );
		if( FAILED( hr ) || !d3dEx )
		{
			PSU_LOG( "D3D9ExUpgrade: QI IDirect3D9Ex failed 0x%08X\n", hr );
			s_activeEx = false;
			return E_FAIL;
		}

		// The game fills the formats from the monitor it was set up on; on a display it doesn't
		// know (a streaming host's virtual display) they stay uninitialised. A window can use
		// the desktop format, so fill in sane ones (also for the plain CreateDevice fallback).
		D3DPRESENT_PARAMETERS &pp = *pPresentationParameters;
		if( pp.Windowed )
		{
			const D3DFORMAT format = pp.BackBufferFormat;
			const bool knownFormat = format == D3DFMT_UNKNOWN || format == D3DFMT_X8R8G8B8 || format == D3DFMT_A8R8G8B8 ||
				format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5 || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A2R10G10B10;
			if( !knownFormat )
			{
				PSU_LOG( "D3D9ExUpgrade: back buffer format %d is invalid; using X8R8G8B8\n", static_cast< int >( format ) );
				pp.BackBufferFormat = D3DFMT_X8R8G8B8;
			}
			if( pp.EnableAutoDepthStencil && pp.AutoDepthStencilFormat == D3DFMT_UNKNOWN )
			{
				PSU_LOG( "D3D9ExUpgrade: no depth format; using D24S8\n" );
				pp.AutoDepthStencilFormat = D3DFMT_D24S8;
			}
		}

		D3DDISPLAYMODEEX fullscreenMode{};
		D3DDISPLAYMODEEX *pMode = nullptr;
		if( !pPresentationParameters->Windowed )
		{
			fullscreenMode.Size = sizeof( fullscreenMode );
			fullscreenMode.Width = pPresentationParameters->BackBufferWidth;
			fullscreenMode.Height = pPresentationParameters->BackBufferHeight;
			fullscreenMode.RefreshRate = pPresentationParameters->FullScreen_RefreshRateInHz;
			fullscreenMode.Format = pPresentationParameters->BackBufferFormat;
			fullscreenMode.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
			pMode = &fullscreenMode;
		}

		IDirect3DDevice9Ex *deviceEx = nullptr;
		hr = d3dEx->CreateDeviceEx(
			Adapter, DeviceType, hFocusWindow, BehaviorFlags,
			pPresentationParameters, pMode, &deviceEx );
		d3dEx->Release();

		if( FAILED( hr ) || !deviceEx )
		{
			const D3DPRESENT_PARAMETERS &pp = *pPresentationParameters;
			PSU_LOG( "D3D9ExUpgrade: params adapter=%u windowed=%d bb=%ux%u fmt=%d count=%u swap=%d refresh=%u interval=0x%X "
				"depth=%d/%d flags=0x%X behavior=0x%X hwnd=%p devhwnd=%p\n", Adapter, pp.Windowed, pp.BackBufferWidth,
				pp.BackBufferHeight, pp.BackBufferFormat, pp.BackBufferCount, pp.SwapEffect, pp.FullScreen_RefreshRateInHz,
				pp.PresentationInterval, pp.EnableAutoDepthStencil, pp.AutoDepthStencilFormat, pp.Flags, BehaviorFlags,
				hFocusWindow, pp.hDeviceWindow );
			PSU_LOG( "D3D9ExUpgrade: CreateDeviceEx failed 0x%08X — VR shared surfaces unavailable\n", hr );
			s_activeEx = false;
			return hr;
		}

		*ppReturnedDeviceInterface = deviceEx;
		PSU_LOG( "D3D9ExUpgrade: CreateDeviceEx ok device=%p\n", deviceEx );
		return S_OK;
	}
}
