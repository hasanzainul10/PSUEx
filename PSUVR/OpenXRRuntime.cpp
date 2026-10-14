#include "OpenXRRuntime.h"

#include "../Misc/Log.h"

#define XR_USE_GRAPHICS_API_D3D11
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <vector>
#include <cstring>

#include <d3dcompiler.h>

#pragma comment( lib, "openxr_loader.lib" )
#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )

namespace PSUVR
{
	static const char *
	XrResultString( XrResult r )
	{
		switch( r )
		{
			case XR_SUCCESS: return "XR_SUCCESS";
			case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
			case XR_ERROR_RUNTIME_FAILURE: return "XR_ERROR_RUNTIME_FAILURE";
			case XR_ERROR_EXTENSION_NOT_PRESENT: return "XR_ERROR_EXTENSION_NOT_PRESENT";
			case XR_ERROR_GRAPHICS_DEVICE_INVALID: return "XR_ERROR_GRAPHICS_DEVICE_INVALID";
			case XR_ERROR_SESSION_NOT_READY: return "XR_ERROR_SESSION_NOT_READY";
			case XR_ERROR_SESSION_NOT_RUNNING: return "XR_ERROR_SESSION_NOT_RUNNING";
			case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
			case XR_ERROR_INSTANCE_LOST: return "XR_ERROR_INSTANCE_LOST";
			default: return "XR_ERROR";
		}
	}

	bool OpenXRRuntime::LoadLoader()
	{
		return true;
	}

	bool OpenXRRuntime::CreateInstance()
	{
		const char *exts[] = { XR_KHR_D3D11_ENABLE_EXTENSION_NAME };

		XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
		std::strncpy( ci.applicationInfo.applicationName, "PSUEx", XR_MAX_APPLICATION_NAME_SIZE );
		ci.applicationInfo.applicationVersion = 1;
		std::strncpy( ci.applicationInfo.engineName, "PSUEx", XR_MAX_ENGINE_NAME_SIZE );
		ci.applicationInfo.engineVersion = 1;
		// Headers are 1.1.x; older loaders (and many runtimes) only accept 1.0.
		ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
		ci.enabledExtensionCount = 1;
		ci.enabledExtensionNames = exts;

		XrInstance instance = XR_NULL_HANDLE;
		XrResult r = xrCreateInstance( &ci, &instance );
		if( XR_FAILED( r ) )
		{
			m_lastError = XrResultString( r );
			PSU_LOG( "OpenXR: xrCreateInstance failed: %s (0x%08X)\n", m_lastError.c_str(), ( unsigned )r );
			return false;
		}
		m_instance = static_cast< uint64_t >( instance  );

		XrSystemGetInfo sysInfo{ XR_TYPE_SYSTEM_GET_INFO };
		sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
		XrSystemId systemId = XR_NULL_SYSTEM_ID;
		r = xrGetSystem( instance, &sysInfo, &systemId );
		if( XR_FAILED( r ) )
		{
			m_lastError = XrResultString( r );
			PSU_LOG( "OpenXR: xrGetSystem failed: %s (start SteamVR / headset runtime)\n", m_lastError.c_str() );
			return false;
		}
		m_systemId = static_cast< uint64_t >( systemId );
		PSU_LOG( "OpenXR: instance+system ok\n" );
		return true;
	}

	bool OpenXRRuntime::CreateSession( ID3D11Device *d3d11 )
	{
		XrInstance instance = static_cast< XrInstance >( m_instance  );
		XrSystemId systemId = static_cast< XrSystemId >( m_systemId );

		PFN_xrGetD3D11GraphicsRequirementsKHR pfnReqs = nullptr;
		xrGetInstanceProcAddr( instance, "xrGetD3D11GraphicsRequirementsKHR",
			reinterpret_cast< PFN_xrVoidFunction * >( &pfnReqs ) );
		if( !pfnReqs )
		{
			PSU_LOG( "OpenXR: missing xrGetD3D11GraphicsRequirementsKHR\n" );
			return false;
		}

		XrGraphicsRequirementsD3D11KHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
		XrResult r = pfnReqs( instance, systemId, &reqs );
		if( XR_FAILED( r ) )
		{
			PSU_LOG( "OpenXR: graphics requirements failed %s\n", XrResultString( r ) );
			return false;
		}

		XrGraphicsBindingD3D11KHR binding{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
		binding.device = d3d11;

		XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
		sci.next = &binding;
		sci.systemId = systemId;

		XrSession session = XR_NULL_HANDLE;
		r = xrCreateSession( instance, &sci, &session );
		if( XR_FAILED( r ) )
		{
			PSU_LOG( "OpenXR: xrCreateSession failed %s\n", XrResultString( r ) );
			return false;
		}
		m_session = static_cast< uint64_t >( session  );

		XrReferenceSpaceCreateInfo spaceInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
		spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
		XrSpace space = XR_NULL_HANDLE;
		r = xrCreateReferenceSpace( session, &spaceInfo, &space );
		if( XR_FAILED( r ) )
		{
			PSU_LOG( "OpenXR: xrCreateReferenceSpace(LOCAL) failed %s\n", XrResultString( r ) );
			return false;
		}
		m_space = static_cast< uint64_t >( space );

		XrReferenceSpaceCreateInfo viewInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
		viewInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
		viewInfo.poseInReferenceSpace.orientation.w = 1.0f;
		XrSpace viewSpace = XR_NULL_HANDLE;
		r = xrCreateReferenceSpace( session, &viewInfo, &viewSpace );
		if( XR_FAILED( r ) )
		{
			PSU_LOG( "OpenXR: xrCreateReferenceSpace(VIEW) failed %s\n", XrResultString( r ) );
			return false;
		}
		m_viewSpace = static_cast< uint64_t >( viewSpace );
		return true;
	}

	int64_t OpenXRRuntime::PickSwapchainFormat( uint64_t sessionHandle )
	{
		XrSession session = static_cast< XrSession >( sessionHandle );
		uint32_t formatCount = 0;
		xrEnumerateSwapchainFormats( session, 0, &formatCount, nullptr );
		std::vector< int64_t > formats( formatCount );
		if( formatCount )
		{
			xrEnumerateSwapchainFormats( session, formatCount, &formatCount, formats.data() );
		}

		for( uint32_t i = 0; i < formatCount; ++i )
		{
			PSU_LOG( "OpenXR: listed format[%u]=%lld\n", i, ( long long )formats[ i ] );
		}

		// D3D9 A8R8G8B8 shared surfaces open as B8G8R8A8_* — prefer that family.
		const int64_t preferred[] = {
			DXGI_FORMAT_B8G8R8A8_UNORM,
			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_FORMAT_B8G8R8A8_TYPELESS,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
			DXGI_FORMAT_R8G8B8A8_TYPELESS,
		};
		for( int64_t want : preferred )
		{
			for( int64_t have : formats )
			{
				if( have == want )
				{
					PSU_LOG( "OpenXR: picked swapchain format %lld\n", ( long long )want );
					return want;
				}
			}
		}

		if( !formats.empty() )
		{
			PSU_LOG( "OpenXR: using first listed format %lld\n", ( long long )formats[ 0 ] );
			return formats[ 0 ];
		}
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	}

	void OpenXRRuntime::DestroySwapchains()
	{
		for( int eye = 0; eye < 2; ++eye )
		{
			if( m_swapchainImages[ eye ] )
			{
				delete[] reinterpret_cast< XrSwapchainImageD3D11KHR * >( m_swapchainImages[ eye ] );
				m_swapchainImages[ eye ] = nullptr;
			}
			m_swapchainImageCount[ eye ] = 0;
			if( m_swapchain[ eye ] )
			{
				xrDestroySwapchain( static_cast< XrSwapchain >( m_swapchain[ eye ] ) );
				m_swapchain[ eye ] = 0;
			}
		}
		m_swapchainWidth = 0;
		m_swapchainHeight = 0;
	}

	bool OpenXRRuntime::CreateSwapchains( int32_t width, int32_t height )
	{
		XrSession session = static_cast< XrSession >( m_session );
		XrInstance instance = static_cast< XrInstance >( m_instance );
		XrSystemId systemId = static_cast< XrSystemId >( m_systemId );

		uint32_t viewCount = 0;
		xrEnumerateViewConfigurationViews( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr );
		std::vector< XrViewConfigurationView > views( viewCount, { XR_TYPE_VIEW_CONFIGURATION_VIEW } );
		xrEnumerateViewConfigurationViews( instance, systemId, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, views.data() );
		if( viewCount < 2 )
		{
			PSU_LOG( "OpenXR: expected stereo views, got %u\n", viewCount );
			return false;
		}

		// Virtual screen: match game BB when provided. Avoid 2K+ shared D3D9 RTs (device loss / black).
		if( width < 1 || height < 1 )
		{
			width = static_cast< int32_t >( views[ 0 ].recommendedImageRectWidth );
			height = static_cast< int32_t >( views[ 0 ].recommendedImageRectHeight );
		}
		// Clamp to runtime limits.
		const int32_t maxW = static_cast< int32_t >( views[ 0 ].maxImageRectWidth );
		const int32_t maxH = static_cast< int32_t >( views[ 0 ].maxImageRectHeight );
		if( width < 1 ) width = 1;
		if( height < 1 ) height = 1;
		if( maxW > 0 && width > maxW ) width = maxW;
		if( maxH > 0 && height > maxH ) height = maxH;

		m_swapchainFormat = PickSwapchainFormat( m_session );
		m_swapchainWidth = width;
		m_swapchainHeight = height;

		DestroySwapchains();
		m_swapchainWidth = width;
		m_swapchainHeight = height;

		for( int eye = 0; eye < 2; ++eye )
		{
			if( !CreateSwapchain( width, height, m_swapchain[ eye ], m_swapchainImages[ eye ], m_swapchainImageCount[ eye ] ) )
			{
				DestroySwapchains();
				return false;
			}
		}

		PSU_LOG( "OpenXR: eye swapchains %dx%d fmt=%lld\n",
			m_swapchainWidth, m_swapchainHeight, ( long long )m_swapchainFormat );
		return true;
	}

	bool OpenXRRuntime::CreateSwapchain( int32_t width, int32_t height, uint64_t &handle, void *&images, uint32_t &imageCount )
	{
		XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
		sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		sci.format = m_swapchainFormat;
		sci.sampleCount = 1;
		sci.width = static_cast< uint32_t >( width );
		sci.height = static_cast< uint32_t >( height );
		sci.faceCount = 1;
		sci.arraySize = 1;
		sci.mipCount = 1;

		XrSwapchain swapchain = XR_NULL_HANDLE;
		XrResult r = xrCreateSwapchain( static_cast< XrSession >( m_session ), &sci, &swapchain );
		if( XR_FAILED( r ) )
		{
			PSU_LOG( "OpenXR: xrCreateSwapchain failed %s (0x%08X) fmt=%lld %dx%d\n",
				XrResultString( r ), ( unsigned )r, ( long long )m_swapchainFormat, width, height );
			return false;
		}
		handle = static_cast< uint64_t >( swapchain );

		uint32_t count = 0;
		xrEnumerateSwapchainImages( swapchain, 0, &count, nullptr );
		auto *list = new XrSwapchainImageD3D11KHR[ count ];
		for( uint32_t i = 0; i < count; ++i )
		{
			list[ i ] = { XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR };
		}
		xrEnumerateSwapchainImages( swapchain, count, &count, reinterpret_cast< XrSwapchainImageBaseHeader * >( list ) );
		imageCount = count;
		images = list;
		return true;
	}

	bool OpenXRRuntime::CreatePanelSwapchain( int32_t width, int32_t height )
	{
		DestroyPanelSwapchain();
		if( !CreateSwapchain( width, height, m_panelSwapchain, m_panelImages, m_panelImageCount ) )
		{
			return false;
		}
		m_panelWidth = width;
		m_panelHeight = height;
		PSU_LOG( "OpenXR: panel swapchain %dx%d\n", width, height );
		return true;
	}

	void OpenXRRuntime::DestroyPanelSwapchain()
	{
		if( m_panelImages )
		{
			delete[] reinterpret_cast< XrSwapchainImageD3D11KHR * >( m_panelImages );
			m_panelImages = nullptr;
		}
		m_panelImageCount = 0;
		if( m_panelSwapchain )
		{
			xrDestroySwapchain( static_cast< XrSwapchain >( m_panelSwapchain ) );
			m_panelSwapchain = 0;
		}
		m_panelWidth = 0;
		m_panelHeight = 0;
	}

	bool OpenXRRuntime::ResizeSwapchains( int32_t width, int32_t height )
	{
		if( !m_ready || !m_session )
		{
			return false;
		}
		if( width == m_panelWidth && height == m_panelHeight && m_panelSwapchain )
		{
			return true;
		}
		return CreatePanelSwapchain( width, height );
	}

	bool OpenXRRuntime::Init( ID3D11Device *d3d11 )
	{
		Shutdown();
		m_device = d3d11;
		d3d11->GetImmediateContext( &m_context );

		// Eye swapchains at the runtime's recommended size; the panel swapchain is
		// created later at the game back-buffer size (ResizeSwapchains).
		if( !LoadLoader() || !CreateInstance() || !CreateSession( d3d11 ) || !CreateSwapchains( 0, 0 ) )
		{
			Shutdown();
			return false;
		}
		m_ready = true;
		PSU_LOG( "OpenXR: runtime ready\n" );
		return true;
	}

	void OpenXRRuntime::Shutdown()
	{
		m_ready = false;
		m_frameStarted = false;
		m_sessionRunning = false;
		m_loggedSubmitFormats = false;

		DestroyPanelSwapchain();
		DestroySwapchains();
		if( m_blitRS ) { m_blitRS->Release(); m_blitRS = nullptr; }
		if( m_blitSampler ) { m_blitSampler->Release(); m_blitSampler = nullptr; }
		if( m_blitPS ) { m_blitPS->Release(); m_blitPS = nullptr; }
		if( m_blitVS ) { m_blitVS->Release(); m_blitVS = nullptr; }
		if( m_viewSpace )
		{
			xrDestroySpace( static_cast< XrSpace >( m_viewSpace ) );
			m_viewSpace = 0;
		}
		if( m_space )
		{
			xrDestroySpace( static_cast< XrSpace >( m_space ) );
			m_space = 0;
		}
		if( m_session )
		{
			xrDestroySession( static_cast< XrSession >( m_session ) );
			m_session = 0;
		}
		if( m_instance )
		{
			xrDestroyInstance( static_cast< XrInstance >( m_instance ) );
			m_instance = 0;
		}
		if( m_context )
		{
			m_context->Release();
			m_context = nullptr;
		}
		m_device = nullptr;
	}

	void OpenXRRuntime::PumpEvents()
	{
		if( !m_ready || !m_instance )
		{
			return;
		}

		XrSession session = static_cast< XrSession >( m_session );
		XrInstance instance = static_cast< XrInstance >( m_instance );

		XrEventDataBuffer ev{ XR_TYPE_EVENT_DATA_BUFFER };
		while( xrPollEvent( instance, &ev ) == XR_SUCCESS )
		{
			if( ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED )
			{
				auto *changed = reinterpret_cast< XrEventDataSessionStateChanged * >( &ev );
				m_sessionState = static_cast< int >( changed->state );
				PSU_LOG( "OpenXR: session state %d\n", m_sessionState );
				if( changed->state == XR_SESSION_STATE_READY )
				{
					XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
					bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
					const XrResult beginHr = xrBeginSession( session, &bi );
					if( XR_SUCCEEDED( beginHr ) )
					{
						m_sessionRunning = true;
						PSU_LOG( "OpenXR: xrBeginSession ok\n" );
					}
					else
					{
						PSU_LOG( "OpenXR: xrBeginSession failed %d\n", static_cast< int >( beginHr ) );
					}
				}
				else if( changed->state == XR_SESSION_STATE_STOPPING )
				{
					xrEndSession( session );
					m_sessionRunning = false;
				}
				else if( changed->state == XR_SESSION_STATE_EXITING ||
						 changed->state == XR_SESSION_STATE_LOSS_PENDING )
				{
					m_sessionRunning = false;
				}
			}
			ev = { XR_TYPE_EVENT_DATA_BUFFER };
		}
	}

	bool OpenXRRuntime::BeginFrame()
	{
		if( !m_ready )
		{
			return false;
		}

		PumpEvents();

		if( !m_sessionRunning )
		{
			static bool s_loggedWait = false;
			if( !s_loggedWait && m_sessionState >= XR_SESSION_STATE_READY )
			{
				s_loggedWait = true;
				PSU_LOG( "OpenXR: BeginFrame waiting (sessionRunning=0 state=%d)\n", m_sessionState );
			}
			return false;
		}

		// SteamVR only advances READY → SYNCHRONIZED → VISIBLE after WaitFrame.
		// Gating on VISIBLE deadlocks the session (HMD stays on the SteamVR dashboard).
		XrSession session = static_cast< XrSession >( m_session );

		XrFrameWaitInfo waitInfo{ XR_TYPE_FRAME_WAIT_INFO };
		XrFrameState frameState{ XR_TYPE_FRAME_STATE };
		const XrResult waitHr = xrWaitFrame( session, &waitInfo, &frameState );
		if( XR_FAILED( waitHr ) )
		{
			static unsigned s_waitFails = 0;
			if( ++s_waitFails <= 8 )
			{
				PSU_LOG( "OpenXR: xrWaitFrame failed %d state=%d\n",
					static_cast< int >( waitHr ), m_sessionState );
			}
			return false;
		}
		m_displayTime = frameState.predictedDisplayTime;
		m_displayPeriod = frameState.predictedDisplayPeriod;
		m_shouldRender = frameState.shouldRender;

		XrFrameBeginInfo beginInfo{ XR_TYPE_FRAME_BEGIN_INFO };
		if( XR_FAILED( xrBeginFrame( session, &beginInfo ) ) )
		{
			return false;
		}
		m_frameStarted = true;

		static bool s_loggedFrame = false;
		if( !s_loggedFrame )
		{
			s_loggedFrame = true;
			PSU_LOG( "OpenXR: first WaitFrame/BeginFrame ok state=%d shouldRender=%d\n",
				m_sessionState, ( int )m_shouldRender );
		}
		return true;
	}

	bool OpenXRRuntime::AcquireSwapchains( ID3D11Texture2D **left, ID3D11Texture2D **right )
	{
		if( !m_frameStarted || !m_shouldRender )
		{
			return false;
		}
		ID3D11Texture2D **outs[ 2 ] = { left, right };
		for( int eye = 0; eye < 2; ++eye )
		{
			XrSwapchain swapchain = static_cast< XrSwapchain >( m_swapchain[ eye ]  );
			XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
			uint32_t index = 0;
			if( XR_FAILED( xrAcquireSwapchainImage( swapchain, &ai, &index ) ) )
			{
				return false;
			}
			XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
			wi.timeout = XR_INFINITE_DURATION;
			if( XR_FAILED( xrWaitSwapchainImage( swapchain, &wi ) ) )
			{
				return false;
			}
			m_acquiredIndex[ eye ] = index;
			auto *images = reinterpret_cast< XrSwapchainImageD3D11KHR * >( m_swapchainImages[ eye ] );
			if( outs[ eye ] )
			{
				*outs[ eye ] = images[ index ].texture;
			}
		}
		return true;
	}

	static DXGI_FORMAT RtvFormatForClear( DXGI_FORMAT fmt, int64_t swapchainFmt )
	{
		switch( fmt )
		{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS:
				return ( swapchainFmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB )
					? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
					: DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS:
				return ( swapchainFmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB )
					? DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
					: DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_UNKNOWN:
				return static_cast< DXGI_FORMAT >( swapchainFmt );
			default:
				// SteamVR often returns the typed sRGB format on the texture itself.
				return fmt;
		}
	}

	bool OpenXRRuntime::ClearEyes( float r, float g, float b )
	{
		ID3D11Texture2D *left = nullptr;
		ID3D11Texture2D *right = nullptr;
		if( !AcquireSwapchains( &left, &right ) || !m_context )
		{
			return false;
		}

		const float color[ 4 ] = { r, g, b, 1.0f };
		ID3D11Texture2D *eyes[ 2 ] = { left, right };
		int cleared = 0;
		for( int eye = 0; eye < 2; ++eye )
		{
			D3D11_TEXTURE2D_DESC desc{};
			eyes[ eye ]->GetDesc( &desc );
			DXGI_FORMAT rtvFmt = RtvFormatForClear( desc.Format, m_swapchainFormat );

			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = rtvFmt;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

			ID3D11RenderTargetView *rtv = nullptr;
			const HRESULT hr = m_device->CreateRenderTargetView( eyes[ eye ], &rtvDesc, &rtv );
			if( SUCCEEDED( hr ) && rtv )
			{
				m_context->ClearRenderTargetView( rtv, color );
				rtv->Release();
				++cleared;
			}
			else
			{
				static unsigned s_rtvFails = 0;
				if( ++s_rtvFails <= 8 )
				{
					PSU_LOG( "OpenXR: ClearEyes CreateRTV failed hr=0x%08X texFmt=%u as %u\n",
						hr, ( unsigned )desc.Format, ( unsigned )rtvFmt );
				}
			}
			XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			xrReleaseSwapchainImage( static_cast< XrSwapchain >( m_swapchain[ eye ]  ), &ri );
		}
		m_clearedThisFrame = ( cleared == 2 );
		m_submitQuadLayer = false;
		return m_clearedThisFrame;
	}

	void OpenXRRuntime::SetVirtualScreenPlacement( float distanceMeters, float heightMeters )
	{
		if( distanceMeters > 0.1f )
		{
			m_quadDistance = distanceMeters;
		}
		if( heightMeters > 0.1f )
		{
			m_quadHeight = heightMeters;
		}
	}

	bool OpenXRRuntime::EnsureBlitPipeline()
	{
		if( m_blitVS && m_blitPS && m_blitSampler && m_blitRS )
		{
			return true;
		}
		if( !m_device )
		{
			return false;
		}

		static const char *kVS = R"(
			struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
			VSOut main( uint id : SV_VertexID ) {
				VSOut o;
				o.uv = float2((id << 1) & 2, id & 2);
				o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
				return o;
			}
		)";
		static const char *kPS = R"(
			Texture2D tex : register(t0);
			SamplerState samp : register(s0);
			float4 main( float4 pos : SV_POSITION, float2 uv : TEXCOORD0 ) : SV_TARGET {
				return tex.Sample(samp, uv);
			}
		)";

		ID3DBlob *vsBlob = nullptr;
		ID3DBlob *psBlob = nullptr;
		ID3DBlob *err = nullptr;
		HRESULT hr = D3DCompile( kVS, strlen( kVS ), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &err );
		if( FAILED( hr ) )
		{
			PSU_LOG( "OpenXR: blit VS compile failed 0x%08X\n", hr );
			if( err ) { PSU_LOG( "%s\n", ( char * )err->GetBufferPointer() ); err->Release(); }
			return false;
		}
		hr = D3DCompile( kPS, strlen( kPS ), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &err );
		if( FAILED( hr ) )
		{
			PSU_LOG( "OpenXR: blit PS compile failed 0x%08X\n", hr );
			if( err ) { PSU_LOG( "%s\n", ( char * )err->GetBufferPointer() ); err->Release(); }
			vsBlob->Release();
			return false;
		}

		m_device->CreateVertexShader( vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_blitVS );
		m_device->CreatePixelShader( psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_blitPS );
		vsBlob->Release();
		psBlob->Release();

		D3D11_SAMPLER_DESC samp{};
		samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samp.AddressU = samp.AddressV = samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		m_device->CreateSamplerState( &samp, &m_blitSampler );

		D3D11_RASTERIZER_DESC rs{};
		rs.FillMode = D3D11_FILL_SOLID;
		rs.CullMode = D3D11_CULL_NONE;
		m_device->CreateRasterizerState( &rs, &m_blitRS );

		PSU_LOG( "OpenXR: blit pipeline ready\n" );
		return m_blitVS && m_blitPS && m_blitSampler && m_blitRS;
	}

	static DXGI_FORMAT
	RtvFormatFor( DXGI_FORMAT fmt, int64_t swapchainFmt )
	{
		switch( fmt )
		{
			case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
			case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
			case DXGI_FORMAT_UNKNOWN:
				return static_cast< DXGI_FORMAT >( swapchainFmt );
			default:
				return fmt;
		}
	}

	bool OpenXRRuntime::BlitTexture( ID3D11Texture2D *src, ID3D11Texture2D *dst )
	{
		if( !EnsureBlitPipeline() || !src || !dst || !m_context )
		{
			return false;
		}

		D3D11_TEXTURE2D_DESC srcDesc{};
		D3D11_TEXTURE2D_DESC dstDesc{};
		src->GetDesc( &srcDesc );
		dst->GetDesc( &dstDesc );

		DXGI_FORMAT srvFmt = srcDesc.Format;
		if( srvFmt == DXGI_FORMAT_B8G8R8A8_TYPELESS ) srvFmt = DXGI_FORMAT_B8G8R8A8_UNORM;
		if( srvFmt == DXGI_FORMAT_R8G8B8A8_TYPELESS ) srvFmt = DXGI_FORMAT_R8G8B8A8_UNORM;

		DXGI_FORMAT rtvFmt = RtvFormatFor( dstDesc.Format, m_swapchainFormat );
		// If OpenXR created sRGB swapchain, prefer sRGB RTV when typeless.
		if( dstDesc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS &&
			( m_swapchainFormat == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ) )
		{
			rtvFmt = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		}
		if( dstDesc.Format == DXGI_FORMAT_B8G8R8A8_TYPELESS &&
			( m_swapchainFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ) )
		{
			rtvFmt = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = srvFmt;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = rtvFmt;
		rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

		ID3D11ShaderResourceView *srv = nullptr;
		ID3D11RenderTargetView *rtv = nullptr;
		if( FAILED( m_device->CreateShaderResourceView( src, &srvDesc, &srv ) ) || !srv )
		{
			PSU_LOG( "OpenXR: CreateSRV failed srcFmt=%u\n", ( unsigned )srcDesc.Format );
			return false;
		}
		if( FAILED( m_device->CreateRenderTargetView( dst, &rtvDesc, &rtv ) ) || !rtv )
		{
			PSU_LOG( "OpenXR: CreateRTV failed dstFmt=%u as %u\n",
				( unsigned )dstDesc.Format, ( unsigned )rtvFmt );
			srv->Release();
			return false;
		}

		const float clear[ 4 ] = { 0.0f, 0.0f, 0.0f, 1.0f };
		m_context->ClearRenderTargetView( rtv, clear );
		m_context->OMSetRenderTargets( 1, &rtv, nullptr );

		D3D11_VIEWPORT vp{};
		vp.Width = static_cast< float >( dstDesc.Width );
		vp.Height = static_cast< float >( dstDesc.Height );
		vp.MaxDepth = 1.0f;
		m_context->RSSetViewports( 1, &vp );
		m_context->RSSetState( m_blitRS );
		m_context->IASetInputLayout( nullptr );
		m_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
		m_context->VSSetShader( m_blitVS, nullptr, 0 );
		m_context->PSSetShader( m_blitPS, nullptr, 0 );
		m_context->PSSetShaderResources( 0, 1, &srv );
		m_context->PSSetSamplers( 0, 1, &m_blitSampler );
		m_context->Draw( 3, 0 );

		ID3D11ShaderResourceView *nullSrv = nullptr;
		m_context->PSSetShaderResources( 0, 1, &nullSrv );
		m_context->OMSetRenderTargets( 0, nullptr, nullptr );

		rtv->Release();
		srv->Release();
		return true;
	}

	bool OpenXRRuntime::SubmitSharedTexture( ID3D11Texture2D *src )
	{
		if( !src || !m_context || !m_device || !m_frameStarted || !m_shouldRender )
		{
			return false;
		}

		// Mono virtual screen: one quad layer, not a stereo projection. Blitting
		// the same BB into both eyes with different poses looks like two copies
		// of the player stitched together.
		if( !m_panelSwapchain || !BlitIntoSwapchain( m_panelSwapchain, m_panelImages, src ) )
		{
			return false;
		}
		m_context->Flush();
		m_clearedThisFrame = true;
		m_submitQuadLayer = true;
		return true;
	}

	bool OpenXRRuntime::BlitIntoSwapchain( uint64_t swapchainHandle, void *imageList, ID3D11Texture2D *src )
	{
		XrSwapchain swapchain = static_cast< XrSwapchain >( swapchainHandle );
		XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
		uint32_t index = 0;
		if( XR_FAILED( xrAcquireSwapchainImage( swapchain, &ai, &index ) ) )
		{
			return false;
		}
		XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
		wi.timeout = XR_INFINITE_DURATION;
		if( XR_FAILED( xrWaitSwapchainImage( swapchain, &wi ) ) )
		{
			XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
			xrReleaseSwapchainImage( swapchain, &ri );
			return false;
		}

		auto *images = reinterpret_cast< XrSwapchainImageD3D11KHR * >( imageList );
		ID3D11Texture2D *dst = images[ index ].texture;

		D3D11_TEXTURE2D_DESC srcDesc{};
		D3D11_TEXTURE2D_DESC dstDesc{};
		src->GetDesc( &srcDesc );
		dst->GetDesc( &dstDesc );
		if( !m_loggedSubmitFormats )
		{
			m_loggedSubmitFormats = true;
			PSU_LOG( "OpenXR: first submit blit src=%ux%u fmt=%u dst=%ux%u fmt=%u\n",
				srcDesc.Width, srcDesc.Height, ( unsigned )srcDesc.Format,
				dstDesc.Width, dstDesc.Height, ( unsigned )dstDesc.Format );
		}

		if( !BlitTexture( src, dst ) )
		{
			DXGI_FORMAT rtvFmt = RtvFormatFor( dstDesc.Format, m_swapchainFormat );
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
			rtvDesc.Format = rtvFmt;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			ID3D11RenderTargetView *rtv = nullptr;
			if( SUCCEEDED( m_device->CreateRenderTargetView( dst, &rtvDesc, &rtv ) ) && rtv )
			{
				const float teal[ 4 ] = { 0.0f, 0.45f, 0.55f, 1.0f };
				m_context->ClearRenderTargetView( rtv, teal );
				rtv->Release();
			}
		}

		XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
		xrReleaseSwapchainImage( swapchain, &ri );
		return true;
	}

	bool OpenXRRuntime::SubmitStereo( ID3D11Texture2D *const eyes[ 2 ], const EyePose localPoses[ 2 ],
		const EyePose viewSpacePoses[ 2 ] )
	{
		if( !m_context || !m_frameStarted || !m_shouldRender || !eyes[ 0 ] || !eyes[ 1 ] )
		{
			return false;
		}
		for( int eye = 0; eye < 2; ++eye )
		{
			if( !BlitIntoSwapchain( m_swapchain[ eye ], m_swapchainImages[ eye ], eyes[ eye ] ) )
			{
				return false;
			}
			m_submittedPoses[ eye ] = localPoses[ eye ];
			m_submittedViewSpacePoses[ eye ] = viewSpacePoses[ eye ];
		}
		m_context->Flush();
		m_clearedThisFrame = true;
		m_submitStereo = true;
		return true;
	}

	static void CopyXrViewsToEyePoses( const XrView views[ 2 ], EyePose out[ 2 ] )
	{
		for( int eye = 0; eye < 2; ++eye )
		{
			out[ eye ].position[ 0 ] = views[ eye ].pose.position.x;
			out[ eye ].position[ 1 ] = views[ eye ].pose.position.y;
			out[ eye ].position[ 2 ] = views[ eye ].pose.position.z;
			out[ eye ].orientation[ 0 ] = views[ eye ].pose.orientation.x;
			out[ eye ].orientation[ 1 ] = views[ eye ].pose.orientation.y;
			out[ eye ].orientation[ 2 ] = views[ eye ].pose.orientation.z;
			out[ eye ].orientation[ 3 ] = views[ eye ].pose.orientation.w;
			out[ eye ].fovAngleLeft = views[ eye ].fov.angleLeft;
			out[ eye ].fovAngleRight = views[ eye ].fov.angleRight;
			out[ eye ].fovAngleUp = views[ eye ].fov.angleUp;
			out[ eye ].fovAngleDown = views[ eye ].fov.angleDown;
		}
	}

	static bool LocateViewsInSpace( XrSession session, XrSpace space, XrTime time, XrView outViews[ 2 ] )
	{
		XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
		locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		locateInfo.displayTime = time;
		locateInfo.space = space;
		XrViewState viewState{ XR_TYPE_VIEW_STATE };
		uint32_t viewCount = 0;
		outViews[ 0 ] = { XR_TYPE_VIEW };
		outViews[ 1 ] = { XR_TYPE_VIEW };
		if( XR_FAILED( xrLocateViews( session, &locateInfo, &viewState, 2, &viewCount, outViews ) ) || viewCount < 2 )
		{
			return false;
		}
		const XrViewStateFlags needed = XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
		return ( viewState.viewStateFlags & needed ) == needed;
	}

	bool OpenXRRuntime::LocateNextViews( EyePose localOut[ 2 ], EyePose viewSpaceOut[ 2 ] )
	{
		if( !m_frameStarted || !m_space || !m_viewSpace )
		{
			return false;
		}
		const XrSession session = static_cast< XrSession >( m_session );
		const XrTime time = m_displayTime + m_displayPeriod;
		XrView localViews[ 2 ]{};
		XrView viewViews[ 2 ]{};
		if( !LocateViewsInSpace( session, static_cast< XrSpace >( m_space ), time, localViews ) )
		{
			return false;
		}
		if( !LocateViewsInSpace( session, static_cast< XrSpace >( m_viewSpace ), time, viewViews ) )
		{
			return false;
		}
		CopyXrViewsToEyePoses( localViews, localOut );
		CopyXrViewsToEyePoses( viewViews, viewSpaceOut );
		return true;
	}

	void OpenXRRuntime::SetPanelWorldPose( bool enable, const float position[ 3 ], const float orientation[ 4 ] )
	{
		m_panelWorldLocked = enable;
		if( enable )
		{
			std::memcpy( m_panelPosition, position, sizeof( m_panelPosition ) );
			std::memcpy( m_panelOrientation, orientation, sizeof( m_panelOrientation ) );
		}
	}

	bool OpenXRRuntime::EndFrame()
	{
		if( !m_frameStarted )
		{
			return false;
		}

		XrSession session = static_cast< XrSession >( m_session );
		XrSpace localSpace = static_cast< XrSpace >( m_space );
		XrSpace viewSpace = static_cast< XrSpace >( m_viewSpace );

		XrCompositionLayerProjection projLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };
		XrCompositionLayerProjectionView projViews[ 2 ]{};
		XrCompositionLayerQuad quadLayer{ XR_TYPE_COMPOSITION_LAYER_QUAD };
		const XrCompositionLayerBaseHeader *layers[ 2 ]{};
		uint32_t layerCount = 0;

		if( m_shouldRender && m_clearedThisFrame )
		{
			const bool stereo = m_submitStereo;
			const bool clearOnly = !m_submitStereo && !m_submitQuadLayer;
			if( stereo || clearOnly )
			{
				XrView located[ 2 ]{ { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
				if( clearOnly )
				{
					LocateViewsInSpace( session, localSpace, m_displayTime, located );
				}

				for( int eye = 0; eye < 2; ++eye )
				{
					projViews[ eye ] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
					if( stereo )
					{
						// Head-locked (VIEW space): game stereo is not a photo of the
						// play space, so LOCAL-space ATW/ASW warps sky and water into ghosts.
						const EyePose &fov = m_submittedPoses[ eye ];
						const EyePose &p = m_submittedViewSpacePoses[ eye ];
						projViews[ eye ].pose.position = { p.position[ 0 ], p.position[ 1 ], p.position[ 2 ] };
						projViews[ eye ].pose.orientation = { p.orientation[ 0 ], p.orientation[ 1 ], p.orientation[ 2 ], p.orientation[ 3 ] };
						projViews[ eye ].fov = { fov.fovAngleLeft, fov.fovAngleRight, fov.fovAngleUp, fov.fovAngleDown };
					}
					else
					{
						projViews[ eye ].pose = located[ eye ].pose;
						projViews[ eye ].fov = located[ eye ].fov;
					}
					projViews[ eye ].subImage.swapchain = static_cast< XrSwapchain >( m_swapchain[ eye ] );
					projViews[ eye ].subImage.imageRect.offset = { 0, 0 };
					projViews[ eye ].subImage.imageRect.extent = { m_swapchainWidth, m_swapchainHeight };
				}

				projLayer.space = stereo ? viewSpace : localSpace;
				projLayer.viewCount = 2;
				projLayer.views = projViews;
				layers[ layerCount++ ] = reinterpret_cast< const XrCompositionLayerBaseHeader * >( &projLayer );
			}

			if( m_submitQuadLayer && m_panelSwapchain && ( viewSpace || m_panelWorldLocked ) )
			{
				const float aspect = ( m_panelHeight > 0 )
					? ( static_cast< float >( m_panelWidth ) / static_cast< float >( m_panelHeight ) )
					: ( 16.0f / 9.0f );
				const float heightM = m_quadHeight;
				const float widthM = heightM * aspect;

				quadLayer.layerFlags = 0;
				quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				quadLayer.subImage.swapchain = static_cast< XrSwapchain >( m_panelSwapchain );
				quadLayer.subImage.imageRect.offset = { 0, 0 };
				quadLayer.subImage.imageRect.extent = { m_panelWidth, m_panelHeight };
				if( m_panelWorldLocked )
				{
					quadLayer.space = localSpace;
					quadLayer.pose.position = { m_panelPosition[ 0 ], m_panelPosition[ 1 ], m_panelPosition[ 2 ] };
					quadLayer.pose.orientation = { m_panelOrientation[ 0 ], m_panelOrientation[ 1 ], m_panelOrientation[ 2 ], m_panelOrientation[ 3 ] };
				}
				else
				{
					// OpenXR VIEW space looks down -Z.
					quadLayer.space = viewSpace;
					quadLayer.pose.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
					quadLayer.pose.position = { 0.0f, 0.0f, -m_quadDistance };
				}
				quadLayer.size = { widthM, heightM };
				layers[ layerCount++ ] = reinterpret_cast< const XrCompositionLayerBaseHeader * >( &quadLayer );
			}
		}

		XrFrameEndInfo endInfo{ XR_TYPE_FRAME_END_INFO };
		endInfo.displayTime = m_displayTime;
		endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
		endInfo.layerCount = layerCount;
		endInfo.layers = layerCount ? layers : nullptr;

		XrResult r = xrEndFrame( session, &endInfo );
		m_frameStarted = false;
		m_clearedThisFrame = false;
		m_submitQuadLayer = false;
		m_submitStereo = false;
		if( XR_FAILED( r ) )
		{
			PSU_LOG( "OpenXR: xrEndFrame failed %s\n", XrResultString( r ) );
			return false;
		}
		return true;
	}

	bool OpenXRRuntime::GetEyePose( int /*eye*/, EyePose &out )
	{
		std::memset( &out, 0, sizeof( out ) );
		out.orientation[ 3 ] = 1.0f;
		return m_ready;
	}
}
