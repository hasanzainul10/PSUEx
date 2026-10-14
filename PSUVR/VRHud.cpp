#include "VRHud.h"

#include "D3D11Bridge.h"
#include "StereoRenderer.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace PSUVR
{
	namespace
	{
		unsigned FvfStride( DWORD fvf )
		{
			unsigned size = 0;
			switch( fvf & D3DFVF_POSITION_MASK )
			{
				case D3DFVF_XYZ: size += 12; break;
				case D3DFVF_XYZRHW: size += 16; break;
				case D3DFVF_XYZB1: size += 16; break;
				case D3DFVF_XYZB2: size += 20; break;
				case D3DFVF_XYZB3: size += 24; break;
				case D3DFVF_XYZB4: size += 28; break;
				case D3DFVF_XYZB5: size += 32; break;
				default: size += 16; break;
			}
			if( fvf & D3DFVF_NORMAL ) size += 12;
			if( fvf & D3DFVF_PSIZE ) size += 4;
			if( fvf & D3DFVF_DIFFUSE ) size += 4;
			if( fvf & D3DFVF_SPECULAR ) size += 4;
			const unsigned texCount = ( fvf & D3DFVF_TEXCOUNT_MASK ) >> D3DFVF_TEXCOUNT_SHIFT;
			size += texCount * 8;
			return size ? size : 32;
		}

		constexpr unsigned kMaxUiXformVerts = 12288;
		constexpr unsigned kMaxUiXformStride = 64;
		alignas( 16 ) unsigned char s_uiXformBuf[ kMaxUiXformVerts * kMaxUiXformStride ]{};

		bool ComputeAabb( const void *verts, unsigned vertCount, unsigned stride,
			float &minX, float &minY, float &maxX, float &maxY )
		{
			if( !verts || vertCount < 1 || stride < 8 )
			{
				return false;
			}
			minX = 1.0e30f;
			minY = 1.0e30f;
			maxX = -1.0e30f;
			maxY = -1.0e30f;
			const auto *bytes = static_cast< const unsigned char * >( verts );
			for( unsigned i = 0; i < vertCount; ++i )
			{
				const float *xy = reinterpret_cast< const float * >( bytes + i * stride );
				minX = ( std::min )( minX, xy[ 0 ] );
				maxX = ( std::max )( maxX, xy[ 0 ] );
				minY = ( std::min )( minY, xy[ 1 ] );
				maxY = ( std::max )( maxY, xy[ 1 ] );
			}
			return true;
		}

		unsigned VertCountForPrimitive( unsigned primitiveType, unsigned primitiveCount )
		{
			switch( static_cast< D3DPRIMITIVETYPE >( primitiveType ) )
			{
				case D3DPT_TRIANGLELIST: return primitiveCount * 3;
				case D3DPT_TRIANGLESTRIP:
				case D3DPT_TRIANGLEFAN: return primitiveCount + 2;
				case D3DPT_LINELIST: return primitiveCount * 2;
				case D3DPT_LINESTRIP: return primitiveCount + 1;
				case D3DPT_POINTLIST: return primitiveCount;
				default: return 0;
			}
		}

		unsigned IndexCountForPrimitive( unsigned primitiveType, unsigned primitiveCount )
		{
			return VertCountForPrimitive( primitiveType, primitiveCount );
		}

		// Near-fullscreen XYZRHW (flat BB / RT blit) must never land on HUD quads over stereo.
		bool IsNearFullscreenDraw( const void *verts, unsigned vertCount, unsigned stride,
			unsigned gameW, unsigned gameH )
		{
			if( !verts || vertCount < 3 || stride < 8 || gameW < 1 || gameH < 1 )
			{
				return false;
			}
			float minX = 0, minY = 0, maxX = 0, maxY = 0;
			if( !ComputeAabb( verts, vertCount, stride, minX, minY, maxX, maxY ) )
			{
				return false;
			}
			const float gw = static_cast< float >( gameW );
			const float gh = static_cast< float >( gameH );
			const float boxW = maxX - minX;
			const float boxH = maxY - minY;
			if( boxW <= 0.0f || boxH <= 0.0f )
			{
				return false;
			}
			const float areaFrac = ( boxW * boxH ) / ( gw * gh );
			if( areaFrac >= 0.45f )
			{
				return true;
			}
			// Span almost the full width and height even if area is a bit lower.
			return boxW >= gw * 0.90f && boxH >= gh * 0.85f;
		}

		// Screen-space 2D UI: every vertex at rhw 1 and one shared z (nothing came through a 3D camera).
		bool IsFlatScreenSpace( const void *verts, unsigned vertCount, unsigned stride )
		{
			if( !verts || vertCount < 1 || stride < 16 )
			{
				return false;
			}
			const auto *bytes = static_cast< const unsigned char * >( verts );
			const float z0 = reinterpret_cast< const float * >( bytes )[ 2 ];
			for( unsigned i = 0; i < vertCount; ++i )
			{
				const float *v = reinterpret_cast< const float * >( bytes + i * stride );
				if( std::fabs( v[ 3 ] - 1.0f ) > 1.0e-3f || std::fabs( v[ 2 ] - z0 ) > 1.0e-4f )
				{
					return false;
				}
			}
			return true;
		}

		// One line per unique (caller, route), so psuex.log shows where each UI piece went.
		void LogRoute( void *ret, const char *route, const void *verts, unsigned vertCount, unsigned stride )
		{
			constexpr unsigned kMaxRouteLogs = 64;
			struct Seen { void *ret; const char *route; };
			static Seen s_seen[ kMaxRouteLogs ]{};
			static unsigned s_seenCount = 0;
			if( s_seenCount >= kMaxRouteLogs )
			{
				return;
			}
			for( unsigned i = 0; i < s_seenCount; ++i )
			{
				if( s_seen[ i ].ret == ret && s_seen[ i ].route == route )
				{
					return;
				}
			}
			s_seen[ s_seenCount++ ] = { ret, route };
			float minX = 0, minY = 0, maxX = 0, maxY = 0;
			ComputeAabb( verts, vertCount, stride, minX, minY, maxX, maxY );
			float z = 0.0f, rhw = 0.0f;
			if( verts && stride >= 16 )
			{
				const float *v = static_cast< const float * >( verts );
				z = v[ 2 ];
				rhw = v[ 3 ];
			}
			PSU_LOG( "VRHud route: ret=%p %s box=(%.0f,%.0f)-(%.0f,%.0f) z=%.4f rhw=%.4f n=%u stride=%u\n",
				ret, route, minX, minY, maxX, maxY, z, rhw, vertCount, stride );
		}

		// Expand indexed XYZRHW into a triangle-list UP buffer in s_uiXformBuf.
		// Returns expanded vert count, or 0 on failure. Sets outPrimCount for D3DPT_TRIANGLELIST.
		unsigned ExpandIndexedToUp( const unsigned char *vb, unsigned vbStride, int baseVertexIndex,
			const void *ib, bool ib32, unsigned startIndex, unsigned primitiveType,
			unsigned primitiveCount, unsigned &outPrimCount )
		{
			outPrimCount = 0;
			const unsigned indexCount = IndexCountForPrimitive( primitiveType, primitiveCount );
			if( indexCount < 1 || vbStride < 8 || vbStride > kMaxUiXformStride )
			{
				return 0;
			}

			auto readIndex = [ & ]( unsigned i ) -> unsigned
			{
				if( ib32 )
				{
					return static_cast< const unsigned * >( ib )[ startIndex + i ];
				}
				return static_cast< const unsigned short * >( ib )[ startIndex + i ];
			};

			auto copyVert = [ & ]( unsigned outI, unsigned srcIndex ) -> bool
			{
				if( outI >= kMaxUiXformVerts )
				{
					static bool s_loggedCap = false;
					if( !s_loggedCap )
					{
						s_loggedCap = true;
						PSU_LOG( "VR: HUD expand cap hit (%u verts), font batch dropped\n", kMaxUiXformVerts );
					}
					return false;
				}
				const int src = static_cast< int >( srcIndex ) + baseVertexIndex;
				if( src < 0 )
				{
					return false;
				}
				std::memcpy( s_uiXformBuf + outI * vbStride,
					vb + static_cast< unsigned >( src ) * vbStride, vbStride );
				return true;
			};

			unsigned outVerts = 0;
			const auto type = static_cast< D3DPRIMITIVETYPE >( primitiveType );
			if( type == D3DPT_TRIANGLELIST )
			{
				if( ( indexCount % 3 ) != 0 )
				{
					return 0;
				}
				for( unsigned i = 0; i < indexCount; ++i )
				{
					if( !copyVert( outVerts++, readIndex( i ) ) )
					{
						return 0;
					}
				}
				outPrimCount = primitiveCount;
				return outVerts;
			}
			if( type == D3DPT_TRIANGLESTRIP )
			{
				if( indexCount < 3 )
				{
					return 0;
				}
				for( unsigned i = 0; i + 2 < indexCount; ++i )
				{
					const unsigned i0 = readIndex( i );
					const unsigned i1 = readIndex( i + 1 );
					const unsigned i2 = readIndex( i + 2 );
					if( ( i & 1u ) == 0 )
					{
						if( !copyVert( outVerts++, i0 ) || !copyVert( outVerts++, i1 ) ||
							!copyVert( outVerts++, i2 ) )
						{
							return 0;
						}
					}
					else
					{
						if( !copyVert( outVerts++, i1 ) || !copyVert( outVerts++, i0 ) ||
							!copyVert( outVerts++, i2 ) )
						{
							return 0;
						}
					}
					++outPrimCount;
				}
				return outVerts;
			}
			return 0;
		}
	}

	VRHud &VRHud::Get()
	{
		static VRHud instance;
		return instance;
	}

	bool VRHud::EnsureTargets( IDirect3DDevice9 *device, unsigned w, unsigned h )
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge || !device )
		{
			return false;
		}
		if( !bridge->EnsureHudTargets( device, w, h ) )
		{
			return false;
		}
		m_w = w;
		m_h = h;
		return true;
	}

	void VRHud::BeginStereoFrame( IDirect3DDevice9 *device, unsigned gameW, unsigned gameH )
	{
		m_active = false;
		m_hasUser = false;
		m_hasWorld = false;
		m_userAabbCount = 0;
		m_worldAabbCount = 0;
		if( !device || gameW < 1 || gameH < 1 || !Config::Get().vr.m_enabled || !Config::Get().vr.m_stereo )
		{
			return;
		}
		if( !EnsureTargets( device, gameW, gameH ) )
		{
			return;
		}
		D3D11Bridge *bridge = System::Get().StereoBridge();
		IDirect3DSurface9 *user = bridge->HudUserSurface();
		IDirect3DSurface9 *world = bridge->HudWorldSurface();
		if( !user || !world )
		{
			return;
		}

		IDirect3DSurface9 *savedRt = nullptr;
		IDirect3DSurface9 *savedDs = nullptr;
		device->GetRenderTarget( 0, &savedRt );
		device->GetDepthStencilSurface( &savedDs );

		device->SetDepthStencilSurface( nullptr );
		device->SetRenderTarget( 0, user );
		device->Clear( 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB( 0, 0, 0, 0 ), 1.0f, 0 );
		device->SetRenderTarget( 0, world );
		device->Clear( 0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB( 0, 0, 0, 0 ), 1.0f, 0 );

		device->SetRenderTarget( 0, savedRt );
		device->SetDepthStencilSurface( savedDs );
		if( savedRt )
		{
			savedRt->Release();
		}
		if( savedDs )
		{
			savedDs->Release();
		}

		m_active = true;
		if( !m_logged )
		{
			m_logged = true;
			PSU_LOG( "VR: HUD quad capture active (%ux%u user+world)\n", gameW, gameH );
		}
	}

	ID3D11Texture2D *VRHud::UserTexture() const
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		return bridge ? bridge->HudUserTexture() : nullptr;
	}

	ID3D11Texture2D *VRHud::WorldTexture() const
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		return bridge ? bridge->HudWorldTexture() : nullptr;
	}

	bool VRHud::AlreadyCapturedUserAabb( float minX, float minY, float maxX, float maxY ) const
	{
		constexpr float kTol = 3.0f;
		for( unsigned i = 0; i < m_userAabbCount; ++i )
		{
			const Aabb &a = m_userAabbs[ i ];
			if( std::fabs( a.minX - minX ) <= kTol && std::fabs( a.minY - minY ) <= kTol &&
				std::fabs( a.maxX - maxX ) <= kTol && std::fabs( a.maxY - maxY ) <= kTol )
			{
				return true;
			}
		}
		return false;
	}

	void VRHud::RememberUserAabb( float minX, float minY, float maxX, float maxY )
	{
		if( m_userAabbCount >= kMaxLayerAabbs )
		{
			return;
		}
		m_userAabbs[ m_userAabbCount++ ] = { minX, minY, maxX, maxY };
	}

	void VRHud::RememberWorldAabb( float minX, float minY, float maxX, float maxY )
	{
		if( m_worldAabbCount >= kMaxLayerAabbs )
		{
			return;
		}
		m_worldAabbs[ m_worldAabbCount++ ] = { minX, minY, maxX, maxY };
	}

	bool VRHud::NearExistingUserAabb( float cx, float cy, float pad ) const
	{
		for( unsigned i = 0; i < m_userAabbCount; ++i )
		{
			const Aabb &a = m_userAabbs[ i ];
			if( cx >= a.minX - pad && cx <= a.maxX + pad &&
				cy >= a.minY - pad && cy <= a.maxY + pad )
			{
				return true;
			}
		}
		return false;
	}

	bool VRHud::NearExistingWorldAabb( float cx, float cy, float pad ) const
	{
		for( unsigned i = 0; i < m_worldAabbCount; ++i )
		{
			const Aabb &a = m_worldAabbs[ i ];
			if( cx >= a.minX - pad && cx <= a.maxX + pad &&
				cy >= a.minY - pad && cy <= a.maxY + pad )
			{
				return true;
			}
		}
		return false;
	}

	bool VRHud::IsWorldUiWidget( const void *verts, int vertexCount, unsigned stride,
		unsigned gameW, unsigned gameH )
	{
		if( !verts || vertexCount < 3 || stride < 8 || gameW < 1 || gameH < 1 )
		{
			return false;
		}
		float minX = 0, minY = 0, maxX = 0, maxY = 0;
		if( !ComputeAabb( verts, static_cast< unsigned >( vertexCount ), stride, minX, minY, maxX, maxY ) )
		{
			return false;
		}
		const float gw = static_cast< float >( gameW );
		const float gh = static_cast< float >( gameH );
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		const float areaFrac = ( boxW * boxH ) / ( gw * gh );
		if( areaFrac >= 0.35f )
		{
			return true;
		}
		const float cx = ( minX + maxX ) * 0.5f;
		const float cy = ( minY + maxY ) * 0.5f;
		const bool centered = ( cx > gw * 0.25f && cx < gw * 0.75f && cy > gh * 0.25f && cy < gh * 0.75f );
		return centered && areaFrac >= 0.15f;
	}

	bool VRHud::IsAvatarAnchoredPassB( const void *verts, unsigned primitiveType,
		unsigned primitiveCount, unsigned stride, unsigned gameW, unsigned gameH )
	{
		if( !verts || stride < 8 || gameW < 1 || gameH < 1 )
		{
			return false;
		}
		const unsigned vertCount = VertCountForPrimitive( primitiveType, primitiveCount );
		// Menu windows are flat 2D; re-projecting them per eye skews them.
		if( IsFlatScreenSpace( verts, vertCount, stride ) )
		{
			return false;
		}
		float minX = 0, minY = 0, maxX = 0, maxY = 0;
		if( !ComputeAabb( verts, vertCount, stride, minX, minY, maxX, maxY ) )
		{
			return false;
		}
		const float gw = static_cast< float >( gameW );
		const float gh = static_cast< float >( gameH );
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		if( boxW <= 0.0f || boxH <= 0.0f )
		{
			return false;
		}
		const float areaFrac = ( boxW * boxH ) / ( gw * gh );
		if( areaFrac >= 0.28f )
		{
			return false;
		}
		const float cx = ( minX + maxX ) * 0.5f;
		const float cy = ( minY + maxY ) * 0.5f;
		if( cx < gw * 0.2f || cx > gw * 0.8f )
		{
			return false;
		}
		return cy > gh * 0.12f && cy < gh * 0.65f;
	}

	bool VRHud::IsVisorChrome( float minX, float minY, float maxX, float maxY,
		unsigned gameW, unsigned gameH ) const
	{
		const float gw = static_cast< float >( gameW );
		const float gh = static_cast< float >( gameH );
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		if( boxW <= 0.0f || boxH <= 0.0f )
		{
			return false;
		}
		const float areaFrac = ( boxW * boxH ) / ( gw * gh );
		if( areaFrac >= 0.22f )
		{
			return false;
		}
		const float cx = ( minX + maxX ) * 0.5f;
		const float cy = ( minY + maxY ) * 0.5f;

		const float marginX = gw * 0.12f;
		const float marginY = gh * 0.12f;
		if( minX < marginX || maxX > gw - marginX || minY < marginY || maxY > gh - marginY )
		{
			return true;
		}

		// Wide enough for name above HP / lobby title above buttons (not only the very bottom strip).
		const bool bottomLeft = ( cx < gw * 0.42f && cy > gh * 0.55f );
		const bool bottomRight = ( cx > gw * 0.58f && cy > gh * 0.55f );
		const bool topLeft = ( cx < gw * 0.48f && cy < gh * 0.28f );
		const bool topRight = ( cx > gw * 0.52f && cy < gh * 0.38f );
		if( bottomLeft || bottomRight || topLeft || topRight )
		{
			return true;
		}

		const float pad = ( std::min )( gw, gh ) * 0.12f;
		return NearExistingUserAabb( cx, cy, pad );
	}

	bool VRHud::ClassifyWorldLayer( const void *verts, unsigned vertCount, unsigned stride ) const
	{
		// Default: one visor layer keeps the game's exact 2D layout (no split windows).
		if( !Config::Get().vr.m_hudWorldPanel )
		{
			return false;
		}
		if( !verts || vertCount < 1 || stride < 8 || m_w < 1 || m_h < 1 )
		{
			return true;
		}
		if( IsWorldUiWidget( verts, static_cast< int >( vertCount ), stride, m_w, m_h ) )
		{
			return true;
		}
		float minX = 0, minY = 0, maxX = 0, maxY = 0;
		if( !ComputeAabb( verts, vertCount, stride, minX, minY, maxX, maxY ) )
		{
			return true;
		}
		const float gw = static_cast< float >( m_w );
		const float gh = static_cast< float >( m_h );
		const float cx = ( minX + maxX ) * 0.5f;
		const float cy = ( minY + maxY ) * 0.5f;
		const float pad = ( std::min )( gw, gh ) * 0.12f;
		// Sticky world first so menu labels / titles follow the kiosk, not the visor.
		if( NearExistingWorldAabb( cx, cy, pad ) )
		{
			return true;
		}
		if( IsVisorChrome( minX, minY, maxX, maxY, m_w, m_h ) )
		{
			return false;
		}
		return true;
	}

	void VRHud::ReplayOnto( IDirect3DDevice9 *device, bool worldLayer,
		void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType,
		int( __thiscall *drawFn )( void *, int, int, void *, int ) )
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		IDirect3DSurface9 *target = worldLayer ? bridge->HudWorldSurface() : bridge->HudUserSurface();
		if( !target || !drawFn )
		{
			return;
		}

		DWORD fvf = 0;
		device->GetFVF( &fvf );
		const unsigned stride = FvfStride( fvf );
		float minX = 0, minY = 0, maxX = 0, maxY = 0;
		const bool haveAabb = vertexPtr && vertexCount > 0 &&
			ComputeAabb( vertexPtr, static_cast< unsigned >( vertexCount ), stride, minX, minY, maxX, maxY );
		if( !worldLayer && haveAabb && AlreadyCapturedUserAabb( minX, minY, maxX, maxY ) )
		{
			return;
		}

		IDirect3DSurface9 *savedRt = nullptr;
		IDirect3DSurface9 *savedDs = nullptr;
		D3DVIEWPORT9 savedVp{};
		RECT savedScissor{};
		DWORD zEnable = 0, zWrite = 0, scissorEnable = 0;
		device->GetRenderTarget( 0, &savedRt );
		device->GetDepthStencilSurface( &savedDs );
		device->GetViewport( &savedVp );
		device->GetScissorRect( &savedScissor );
		device->GetRenderState( D3DRS_ZENABLE, &zEnable );
		device->GetRenderState( D3DRS_ZWRITEENABLE, &zWrite );
		device->GetRenderState( D3DRS_SCISSORTESTENABLE, &scissorEnable );

		m_capturing = true;
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		device->SetRenderTarget( 0, target );
		device->SetDepthStencilSurface( nullptr );
		D3DVIEWPORT9 vp{ 0, 0, m_w, m_h, 0.0f, 1.0f };
		device->SetViewport( &vp );
		RECT scissor{ 0, 0, static_cast< LONG >( m_w ), static_cast< LONG >( m_h ) };
		device->SetScissorRect( &scissor );
		drawFn( thisPtr, a4, vertexCount, vertexPtr, drawType );
		m_capturing = false;

		device->SetRenderTarget( 0, savedRt );
		device->SetDepthStencilSurface( savedDs );
		device->SetViewport( &savedVp );
		device->SetScissorRect( &savedScissor );
		device->SetRenderState( D3DRS_SCISSORTESTENABLE, scissorEnable );
		device->SetRenderState( D3DRS_ZENABLE, zEnable );
		device->SetRenderState( D3DRS_ZWRITEENABLE, zWrite );
		device->SetFVF( fvf );
		if( savedRt )
		{
			savedRt->Release();
		}
		if( savedDs )
		{
			savedDs->Release();
		}

		if( worldLayer )
		{
			m_hasWorld = true;
			if( haveAabb )
			{
				RememberWorldAabb( minX, minY, maxX, maxY );
			}
		}
		else
		{
			m_hasUser = true;
			if( haveAabb )
			{
				RememberUserAabb( minX, minY, maxX, maxY );
			}
		}
	}

	void VRHud::ReplayPassBOnto( IDirect3DDevice9 *device, bool worldLayer, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride, bool rememberAabb )
	{
		D3D11Bridge *bridge = System::Get().StereoBridge();
		IDirect3DSurface9 *target = bridge
			? ( worldLayer ? bridge->HudWorldSurface() : bridge->HudUserSurface() )
			: nullptr;
		if( !target || !verts )
		{
			return;
		}

		const unsigned vertCount = VertCountForPrimitive( primitiveType, primitiveCount );
		float minX = 0, minY = 0, maxX = 0, maxY = 0;
		const bool haveAabb = ComputeAabb( verts, vertCount, stride, minX, minY, maxX, maxY );

		IDirect3DSurface9 *savedRt = nullptr;
		IDirect3DSurface9 *savedDs = nullptr;
		D3DVIEWPORT9 savedVp{};
		RECT savedScissor{};
		DWORD zEnable = 0, zWrite = 0, scissorEnable = 0;
		device->GetRenderTarget( 0, &savedRt );
		device->GetDepthStencilSurface( &savedDs );
		device->GetViewport( &savedVp );
		device->GetScissorRect( &savedScissor );
		device->GetRenderState( D3DRS_ZENABLE, &zEnable );
		device->GetRenderState( D3DRS_ZWRITEENABLE, &zWrite );
		device->GetRenderState( D3DRS_SCISSORTESTENABLE, &scissorEnable );
		// DP-UP resets stream 0 (and the font DIP path keeps its IB bound), so the
		// game's next DIP on the same VB would draw nothing unless we put them back.
		DWORD savedFvf = 0;
		IDirect3DVertexBuffer9 *savedVb = nullptr;
		UINT savedVbOffset = 0;
		UINT savedVbStride = 0;
		IDirect3DIndexBuffer9 *savedIb = nullptr;
		device->GetFVF( &savedFvf );
		device->GetStreamSource( 0, &savedVb, &savedVbOffset, &savedVbStride );
		device->GetIndices( &savedIb );

		m_capturing = true;
		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		device->SetRenderTarget( 0, target );
		device->SetDepthStencilSurface( nullptr );
		D3DVIEWPORT9 vp{ 0, 0, m_w, m_h, 0.0f, 1.0f };
		device->SetViewport( &vp );
		RECT scissor{ 0, 0, static_cast< LONG >( m_w ), static_cast< LONG >( m_h ) };
		device->SetScissorRect( &scissor );
		device->DrawPrimitiveUP( static_cast< D3DPRIMITIVETYPE >( primitiveType ),
			primitiveCount, verts, stride );
		m_capturing = false;

		device->SetFVF( savedFvf );
		device->SetStreamSource( 0, savedVb, savedVbOffset, savedVbStride );
		device->SetIndices( savedIb );
		if( savedVb )
		{
			savedVb->Release();
		}
		if( savedIb )
		{
			savedIb->Release();
		}

		device->SetRenderTarget( 0, savedRt );
		device->SetDepthStencilSurface( savedDs );
		device->SetViewport( &savedVp );
		device->SetScissorRect( &savedScissor );
		device->SetRenderState( D3DRS_SCISSORTESTENABLE, scissorEnable );
		device->SetRenderState( D3DRS_ZENABLE, zEnable );
		device->SetRenderState( D3DRS_ZWRITEENABLE, zWrite );
		if( savedRt )
		{
			savedRt->Release();
		}
		if( savedDs )
		{
			savedDs->Release();
		}
		if( worldLayer )
		{
			m_hasWorld = true;
			if( haveAabb && rememberAabb )
			{
				RememberWorldAabb( minX, minY, maxX, maxY );
			}
		}
		else
		{
			m_hasUser = true;
			if( haveAabb && rememberAabb )
			{
				RememberUserAabb( minX, minY, maxX, maxY );
			}
		}
	}

	void VRHud::ReplayFontBatch( IDirect3DDevice9 *device, const unsigned char *verts,
		unsigned vertCount, unsigned stride, void *ret )
	{
		// One font DIP can hold every string on screen, so its batch AABB says nothing.
		// Route each glyph quad on its own and replay one draw per layer.
		static alignas( 16 ) unsigned char s_userBucket[ kMaxUiXformVerts * kMaxUiXformStride ]{};
		static alignas( 16 ) unsigned char s_worldBucket[ kMaxUiXformVerts * kMaxUiXformStride ]{};
		if( !verts || vertCount < 3 || stride > kMaxUiXformStride )
		{
			return;
		}

		// Glyph clusters (strings) for the sticky lists, so single glyphs don't flood them.
		constexpr unsigned kMaxClusters = 32;
		struct Cluster { Aabb box; bool world; };
		Cluster clusters[ kMaxClusters ];
		unsigned clusterCount = 0;
		auto addToCluster = [ & ]( bool world, float x0, float y0, float x1, float y1 )
		{
			const float gap = ( y1 - y0 ) * 1.5f;
			for( unsigned c = 0; c < clusterCount; ++c )
			{
				Aabb &b = clusters[ c ].box;
				if( clusters[ c ].world == world &&
					x0 <= b.maxX + gap && x1 >= b.minX - gap &&
					y0 <= b.maxY + gap && y1 >= b.minY - gap )
				{
					b.minX = ( std::min )( b.minX, x0 );
					b.minY = ( std::min )( b.minY, y0 );
					b.maxX = ( std::max )( b.maxX, x1 );
					b.maxY = ( std::max )( b.maxY, y1 );
					return;
				}
			}
			if( clusterCount < kMaxClusters )
			{
				clusters[ clusterCount++ ] = { { x0, y0, x1, y1 }, world };
			}
		};

		const unsigned group = ( vertCount % 6 ) == 0 ? 6u : 3u;
		unsigned userVerts = 0;
		unsigned worldVerts = 0;
		for( unsigned i = 0; i + group <= vertCount; i += group )
		{
			const unsigned char *quad = verts + i * stride;
			if( IsNearFullscreenDraw( quad, group, stride, m_w, m_h ) )
			{
				continue;
			}
			const bool world = ClassifyWorldLayer( quad, group, stride );
			unsigned &count = world ? worldVerts : userVerts;
			unsigned char *dst = ( world ? s_worldBucket : s_userBucket ) + count * stride;
			std::memcpy( dst, quad, group * stride );
			count += group;

			float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
			if( ComputeAabb( quad, group, stride, x0, y0, x1, y1 ) )
			{
				addToCluster( world, x0, y0, x1, y1 );
			}
		}

		if( userVerts >= 3 )
		{
			LogRoute( ret, "font-user", s_userBucket, userVerts, stride );
			ReplayPassBOnto( device, false, D3DPT_TRIANGLELIST, userVerts / 3, s_userBucket, stride, false );
		}
		if( worldVerts >= 3 )
		{
			LogRoute( ret, "font-world", s_worldBucket, worldVerts, stride );
			ReplayPassBOnto( device, true, D3DPT_TRIANGLELIST, worldVerts / 3, s_worldBucket, stride, false );
		}
		for( unsigned c = 0; c < clusterCount; ++c )
		{
			const Aabb &b = clusters[ c ].box;
			if( clusters[ c ].world )
			{
				RememberWorldAabb( b.minX, b.minY, b.maxX, b.maxY );
			}
			else
			{
				RememberUserAabb( b.minX, b.minY, b.maxX, b.maxY );
			}
		}
	}

	void VRHud::CapturePassBDraw( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride, void *ret )
	{
		if( m_capturing || !m_active || !device || !verts ||
			StereoRenderer::InEyePass() || StereoRenderer::InNameplateDraw() )
		{
			return;
		}
		auto &stereo = StereoRenderer::Get();
		if( !stereo.HasFrame() )
		{
			return;
		}
		const unsigned vertCount = VertCountForPrimitive( primitiveType, primitiveCount );
		if( IsNearFullscreenDraw( verts, vertCount, stride, m_w, m_h ) )
		{
			LogRoute( ret, "skip-fullscreen", verts, vertCount, stride );
			return;
		}
		if( IsAvatarAnchoredPassB( verts, primitiveType, primitiveCount, stride, m_w, m_h ) )
		{
			LogRoute( ret, "eyes", verts, vertCount, stride );
			stereo.DrawNameplateToEyes( device, primitiveType, primitiveCount, verts, stride );
			if( m_inUiDraw )
			{
				++m_uiPassBCaptures;
			}
			return;
		}
		const bool world = ClassifyWorldLayer( verts, vertCount, stride );
		LogRoute( ret, world ? "world" : "user", verts, vertCount, stride );
		ReplayPassBOnto( device, world, primitiveType, primitiveCount, verts, stride );
		if( m_inUiDraw )
		{
			++m_uiPassBCaptures;
		}
	}

	void VRHud::CaptureXyzrhwIndexed( IDirect3DDevice9 *device, unsigned primitiveType,
		int baseVertexIndex, unsigned minIndex, unsigned numVertices,
		unsigned startIndex, unsigned primitiveCount, void *ret )
	{
		( void )minIndex;
		( void )numVertices;
		if( m_capturing || !m_active || !device ||
			StereoRenderer::InEyePass() || StereoRenderer::InNameplateDraw() )
		{
			return;
		}
		if( !StereoRenderer::Get().HasFrame() )
		{
			return;
		}

		DWORD fvf = 0;
		device->GetFVF( &fvf );
		if( ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
		{
			return;
		}
		const unsigned stride = FvfStride( fvf );
		if( stride < 16 || stride > kMaxUiXformStride )
		{
			return;
		}

		IDirect3DVertexBuffer9 *vb = nullptr;
		UINT offsetBytes = 0;
		UINT vbStride = 0;
		if( FAILED( device->GetStreamSource( 0, &vb, &offsetBytes, &vbStride ) ) || !vb )
		{
			return;
		}
		if( vbStride == 0 )
		{
			vbStride = stride;
		}

		IDirect3DIndexBuffer9 *ib = nullptr;
		if( FAILED( device->GetIndices( &ib ) ) || !ib )
		{
			vb->Release();
			return;
		}

		D3DINDEXBUFFER_DESC ibDesc{};
		ib->GetDesc( &ibDesc );
		const bool ib32 = ( ibDesc.Format == D3DFMT_INDEX32 );

		void *vbData = nullptr;
		void *ibData = nullptr;
		const HRESULT vbHr = vb->Lock( 0, 0, &vbData, D3DLOCK_READONLY );
		const HRESULT ibHr = ib->Lock( 0, 0, &ibData, D3DLOCK_READONLY );
		if( FAILED( vbHr ) || FAILED( ibHr ) || !vbData || !ibData )
		{
			if( SUCCEEDED( vbHr ) )
			{
				vb->Unlock();
			}
			if( SUCCEEDED( ibHr ) )
			{
				ib->Unlock();
			}
			ib->Release();
			vb->Release();
			return;
		}

		const auto *vbBytes = static_cast< const unsigned char * >( vbData ) + offsetBytes;
		unsigned outPrimCount = 0;
		const unsigned outVerts = ExpandIndexedToUp( vbBytes, vbStride, baseVertexIndex,
			ibData, ib32, startIndex, primitiveType, primitiveCount, outPrimCount );

		ib->Unlock();
		vb->Unlock();
		ib->Release();
		vb->Release();

		if( outVerts < 3 || outPrimCount < 1 )
		{
			return;
		}

		ReplayFontBatch( device, s_uiXformBuf, outVerts, vbStride, ret );
		if( m_inUiDraw )
		{
			++m_uiPassBCaptures;
		}
	}

	void VRHud::CaptureXyzrhwIndexedUp( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned minVertexIndex, unsigned numVertices, unsigned primitiveCount,
		const void *indexData, unsigned indexFormat, const void *verts, unsigned stride, void *ret )
	{
		( void )minVertexIndex;
		( void )numVertices;
		if( m_capturing || !m_active || !device || !indexData || !verts ||
			StereoRenderer::InEyePass() || StereoRenderer::InNameplateDraw() )
		{
			return;
		}
		if( !StereoRenderer::Get().HasFrame() )
		{
			return;
		}
		if( stride < 16 || stride > kMaxUiXformStride )
		{
			return;
		}

		DWORD fvf = 0;
		device->GetFVF( &fvf );
		if( ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
		{
			return;
		}

		const bool ib32 = ( indexFormat == D3DFMT_INDEX32 );
		unsigned outPrimCount = 0;
		const unsigned outVerts = ExpandIndexedToUp(
			static_cast< const unsigned char * >( verts ), stride, 0,
			indexData, ib32, 0, primitiveType, primitiveCount, outPrimCount );
		if( outVerts < 3 || outPrimCount < 1 )
		{
			return;
		}

		ReplayFontBatch( device, s_uiXformBuf, outVerts, stride, ret );
		if( m_inUiDraw )
		{
			++m_uiPassBCaptures;
		}
	}

	void VRHud::CaptureUiDraw( IDirect3DDevice9 *device,
		void *thisPtr, int a4, int vertexCount, void *vertexPtr, int drawType,
		int( __thiscall *drawFn )( void *, int, int, void *, int ) )
	{
		if( m_capturing || !m_active || !device || !drawFn || StereoRenderer::InEyePass() )
		{
			return;
		}
		if( !StereoRenderer::Get().HasFrame() )
		{
			return;
		}

		DWORD fvf = 0;
		device->GetFVF( &fvf );
		unsigned stride = FvfStride( fvf );
		const bool world = ClassifyWorldLayer( vertexPtr, static_cast< unsigned >( vertexCount ), stride );
		ReplayOnto( device, world, thisPtr, a4, vertexCount, vertexPtr, drawType, drawFn );
	}
}
