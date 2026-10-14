#include "StereoRenderer.h"

#include "D3D11Bridge.h"
#include "VRCamera.h"
#include "VRHud.h"
#include "VRSystem.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"

#include <d3d9.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace PSUVR
{
	namespace
	{
		bool s_inEyePass = false;
		bool s_inNameplateDraw = false;
		IDirect3DSurface9 *s_eyeSurface = nullptr;
		unsigned s_eyeW = 0;
		unsigned s_eyeH = 0;
		unsigned s_gameW = 0;
		unsigned s_gameH = 0;
		unsigned s_stretchQuadLogCount = 0;
		alignas( 16 ) unsigned char s_stretchQuadBuf[ 64 * 64 ]{};
		alignas( 16 ) unsigned char s_nameplateVertBuf[ 64 * 64 ]{};
		std::unordered_set< IDirect3DBaseTexture9 * > s_writtenTextures;
		unsigned s_screenQuadLogCount = 0;

		// First-person body hide: last WORLD translation (= mesh origin in game-camera space).
		bool s_haveWorld = false;
		float s_worldT[ 3 ]{};
		unsigned s_hideSkipped = 0;
		unsigned s_hideDraws = 0;
		bool s_hideLogged = false;
		unsigned s_hideSampleCount = 0;
		float s_hideSamples[ 8 ][ 4 ]{};  // x, y, z, skipped
		unsigned s_stretchLogCount = 0;
		constexpr unsigned kMaxScreenQuadLogs = 24;
		constexpr unsigned kMaxStretchLogs = 16;
		constexpr unsigned kMaxStretchVerts = 64;
		constexpr unsigned kMaxStretchStride = 64;

		bool HasStencil( D3DFORMAT format )
		{
			return format == D3DFMT_D24S8 || format == D3DFMT_D24X4S4 || format == D3DFMT_D15S1 || format == D3DFMT_D24FS8;
		}

		bool InvertMatrix( const D3DMATRIX &m, D3DMATRIX &out )
		{
			const float *a = &m._11;
			float inv[ 16 ]{};
			inv[ 0 ] = a[ 5 ] * a[ 10 ] * a[ 15 ] - a[ 5 ] * a[ 11 ] * a[ 14 ] - a[ 9 ] * a[ 6 ] * a[ 15 ]
				+ a[ 9 ] * a[ 7 ] * a[ 14 ] + a[ 13 ] * a[ 6 ] * a[ 11 ] - a[ 13 ] * a[ 7 ] * a[ 10 ];
			inv[ 4 ] = -a[ 4 ] * a[ 10 ] * a[ 15 ] + a[ 4 ] * a[ 11 ] * a[ 14 ] + a[ 8 ] * a[ 6 ] * a[ 15 ]
				- a[ 8 ] * a[ 7 ] * a[ 14 ] - a[ 12 ] * a[ 6 ] * a[ 11 ] + a[ 12 ] * a[ 7 ] * a[ 10 ];
			inv[ 8 ] = a[ 4 ] * a[ 9 ] * a[ 15 ] - a[ 4 ] * a[ 11 ] * a[ 13 ] - a[ 8 ] * a[ 5 ] * a[ 15 ]
				+ a[ 8 ] * a[ 7 ] * a[ 13 ] + a[ 12 ] * a[ 5 ] * a[ 11 ] - a[ 12 ] * a[ 7 ] * a[ 9 ];
			inv[ 12 ] = -a[ 4 ] * a[ 9 ] * a[ 14 ] + a[ 4 ] * a[ 10 ] * a[ 13 ] + a[ 8 ] * a[ 5 ] * a[ 14 ]
				- a[ 8 ] * a[ 6 ] * a[ 13 ] - a[ 12 ] * a[ 5 ] * a[ 10 ] + a[ 12 ] * a[ 6 ] * a[ 9 ];
			inv[ 1 ] = -a[ 1 ] * a[ 10 ] * a[ 15 ] + a[ 1 ] * a[ 11 ] * a[ 14 ] + a[ 9 ] * a[ 2 ] * a[ 15 ]
				- a[ 9 ] * a[ 3 ] * a[ 14 ] - a[ 13 ] * a[ 2 ] * a[ 11 ] + a[ 13 ] * a[ 3 ] * a[ 10 ];
			inv[ 5 ] = a[ 0 ] * a[ 10 ] * a[ 15 ] - a[ 0 ] * a[ 11 ] * a[ 14 ] - a[ 8 ] * a[ 2 ] * a[ 15 ]
				+ a[ 8 ] * a[ 3 ] * a[ 14 ] + a[ 12 ] * a[ 2 ] * a[ 11 ] - a[ 12 ] * a[ 3 ] * a[ 10 ];
			inv[ 9 ] = -a[ 0 ] * a[ 9 ] * a[ 15 ] + a[ 0 ] * a[ 11 ] * a[ 13 ] + a[ 8 ] * a[ 1 ] * a[ 15 ]
				- a[ 8 ] * a[ 3 ] * a[ 13 ] - a[ 12 ] * a[ 1 ] * a[ 11 ] + a[ 12 ] * a[ 3 ] * a[ 9 ];
			inv[ 13 ] = a[ 0 ] * a[ 9 ] * a[ 14 ] - a[ 0 ] * a[ 10 ] * a[ 13 ] - a[ 8 ] * a[ 1 ] * a[ 14 ]
				+ a[ 8 ] * a[ 2 ] * a[ 13 ] + a[ 12 ] * a[ 1 ] * a[ 10 ] - a[ 12 ] * a[ 2 ] * a[ 9 ];
			inv[ 2 ] = a[ 1 ] * a[ 6 ] * a[ 15 ] - a[ 1 ] * a[ 7 ] * a[ 14 ] - a[ 5 ] * a[ 2 ] * a[ 15 ]
				+ a[ 5 ] * a[ 3 ] * a[ 14 ] + a[ 13 ] * a[ 2 ] * a[ 7 ] - a[ 13 ] * a[ 3 ] * a[ 6 ];
			inv[ 6 ] = -a[ 0 ] * a[ 6 ] * a[ 15 ] + a[ 0 ] * a[ 7 ] * a[ 14 ] + a[ 4 ] * a[ 2 ] * a[ 15 ]
				- a[ 4 ] * a[ 3 ] * a[ 14 ] - a[ 12 ] * a[ 2 ] * a[ 7 ] + a[ 12 ] * a[ 3 ] * a[ 6 ];
			inv[ 10 ] = a[ 0 ] * a[ 5 ] * a[ 15 ] - a[ 0 ] * a[ 7 ] * a[ 13 ] - a[ 4 ] * a[ 1 ] * a[ 15 ]
				+ a[ 4 ] * a[ 3 ] * a[ 13 ] + a[ 12 ] * a[ 1 ] * a[ 7 ] - a[ 12 ] * a[ 3 ] * a[ 5 ];
			inv[ 14 ] = -a[ 0 ] * a[ 5 ] * a[ 14 ] + a[ 0 ] * a[ 6 ] * a[ 13 ] + a[ 4 ] * a[ 1 ] * a[ 14 ]
				- a[ 4 ] * a[ 2 ] * a[ 13 ] - a[ 12 ] * a[ 1 ] * a[ 6 ] + a[ 12 ] * a[ 2 ] * a[ 5 ];
			inv[ 3 ] = -a[ 1 ] * a[ 6 ] * a[ 11 ] + a[ 1 ] * a[ 7 ] * a[ 10 ] + a[ 5 ] * a[ 2 ] * a[ 11 ]
				- a[ 5 ] * a[ 3 ] * a[ 10 ] - a[ 9 ] * a[ 2 ] * a[ 7 ] + a[ 9 ] * a[ 3 ] * a[ 6 ];
			inv[ 7 ] = a[ 0 ] * a[ 6 ] * a[ 11 ] - a[ 0 ] * a[ 7 ] * a[ 10 ] - a[ 4 ] * a[ 2 ] * a[ 11 ]
				+ a[ 4 ] * a[ 3 ] * a[ 10 ] + a[ 8 ] * a[ 2 ] * a[ 7 ] - a[ 8 ] * a[ 3 ] * a[ 6 ];
			inv[ 11 ] = -a[ 0 ] * a[ 5 ] * a[ 11 ] + a[ 0 ] * a[ 7 ] * a[ 9 ] + a[ 4 ] * a[ 1 ] * a[ 11 ]
				- a[ 4 ] * a[ 3 ] * a[ 9 ] - a[ 8 ] * a[ 1 ] * a[ 7 ] + a[ 8 ] * a[ 3 ] * a[ 5 ];
			inv[ 15 ] = a[ 0 ] * a[ 5 ] * a[ 10 ] - a[ 0 ] * a[ 6 ] * a[ 9 ] - a[ 4 ] * a[ 1 ] * a[ 10 ]
				+ a[ 4 ] * a[ 2 ] * a[ 9 ] + a[ 8 ] * a[ 1 ] * a[ 6 ] - a[ 8 ] * a[ 2 ] * a[ 5 ];

			const float det = a[ 0 ] * inv[ 0 ] + a[ 1 ] * inv[ 4 ] + a[ 2 ] * inv[ 8 ] + a[ 3 ] * inv[ 12 ];
			if( det > -1.0e-12f && det < 1.0e-12f )
			{
				return false;
			}
			const float idet = 1.0f / det;
			float *o = &out._11;
			for( int i = 0; i < 16; ++i )
			{
				o[ i ] = inv[ i ] * idet;
			}
			return true;
		}

		void MulVec4Mat( const float v[ 4 ], const D3DMATRIX &m, float out[ 4 ] )
		{
			out[ 0 ] = v[ 0 ] * m._11 + v[ 1 ] * m._21 + v[ 2 ] * m._31 + v[ 3 ] * m._41;
			out[ 1 ] = v[ 0 ] * m._12 + v[ 1 ] * m._22 + v[ 2 ] * m._32 + v[ 3 ] * m._42;
			out[ 2 ] = v[ 0 ] * m._13 + v[ 1 ] * m._23 + v[ 2 ] * m._33 + v[ 3 ] * m._43;
			out[ 3 ] = v[ 0 ] * m._14 + v[ 1 ] * m._24 + v[ 2 ] * m._34 + v[ 3 ] * m._44;
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
	}

	StereoRenderer &StereoRenderer::Get()
	{
		static StereoRenderer instance;
		return instance;
	}

	bool StereoRenderer::InEyePass()
	{
		return s_inEyePass;
	}

	bool StereoRenderer::InNameplateDraw()
	{
		return s_inNameplateDraw;
	}

	void StereoRenderer::MarkSurfaceTexture( IDirect3DSurface9 *surface )
	{
		if( !surface )
		{
			return;
		}
		IDirect3DTexture9 *tex = nullptr;
		if( SUCCEEDED( surface->GetContainer( __uuidof( IDirect3DTexture9 ), reinterpret_cast< void ** >( &tex ) ) ) && tex )
		{
			s_writtenTextures.insert( tex );
			tex->Release();
		}
	}

	void StereoRenderer::BeginEyePass( IDirect3DSurface9 *eyeTarget, unsigned eyeW, unsigned eyeH,
		unsigned gameW, unsigned gameH )
	{
		s_writtenTextures.clear();
		s_eyeSurface = eyeTarget;
		s_eyeW = eyeW;
		s_eyeH = eyeH;
		s_gameW = gameW;
		s_gameH = gameH;
		MarkSurfaceTexture( eyeTarget );
		s_inEyePass = true;
		s_haveWorld = false;
		s_hideSkipped = 0;
		s_hideDraws = 0;
		s_hideSampleCount = 0;
	}

	void StereoRenderer::EndEyePass()
	{
		if( !s_hideLogged && VRCamera::FirstPersonActive() && s_hideDraws > 0 )
		{
			s_hideLogged = true;
			PSU_LOG( "VRHide: eye pass skipped %u of %u world draws as local player\n", s_hideSkipped, s_hideDraws );
			for( unsigned i = 0; i < s_hideSampleCount; ++i )
			{
				PSU_LOG( "VRHide:   near origin (%.1f, %.1f, %.1f) %s\n",
					s_hideSamples[ i ][ 0 ], s_hideSamples[ i ][ 1 ], s_hideSamples[ i ][ 2 ],
					s_hideSamples[ i ][ 3 ] != 0.0f ? "hidden" : "kept" );
			}
		}
		s_inEyePass = false;
		s_haveWorld = false;
		s_eyeSurface = nullptr;
		s_eyeW = 0;
		s_eyeH = 0;
		s_gameW = 0;
		s_gameH = 0;
		s_writtenTextures.clear();
	}

	bool StereoRenderer::EyeColorTargetIs( IDirect3DSurface9 *surface )
	{
		return s_inEyePass && surface && surface == s_eyeSurface && s_eyeW > 0 && s_eyeH > 0;
	}

	bool StereoRenderer::EyeColorTargetBound( IDirect3DDevice9 *device )
	{
		if( !s_inEyePass || !device || !s_eyeSurface || s_eyeW == 0 || s_eyeH == 0 )
		{
			return false;
		}
		IDirect3DSurface9 *rt = nullptr;
		if( FAILED( device->GetRenderTarget( 0, &rt ) ) || !rt )
		{
			return false;
		}
		const bool match = ( rt == s_eyeSurface );
		rt->Release();
		return match;
	}

	void StereoRenderer::GetEyeViewport( D3DVIEWPORT9 &out )
	{
		out = { 0, 0, s_eyeW, s_eyeH, 0.0f, 1.0f };
	}

	void StereoRenderer::GetEyeScissor( RECT &out )
	{
		out.left = 0;
		out.top = 0;
		out.right = static_cast< LONG >( s_eyeW );
		out.bottom = static_cast< LONG >( s_eyeH );
	}

	void StereoRenderer::OnSetRenderTarget( IDirect3DSurface9 *target )
	{
		if( !s_inEyePass )
		{
			return;
		}
		MarkSurfaceTexture( target );
	}

	void StereoRenderer::OnStretchRect( IDirect3DSurface9 *source, IDirect3DSurface9 *dest, void *ret )
	{
		if( !source || !dest )
		{
			return;
		}

		if( s_inEyePass )
		{
			MarkSurfaceTexture( dest );
		}

		if( s_stretchLogCount >= kMaxStretchLogs )
		{
			return;
		}

		D3DSURFACE_DESC srcDesc{};
		D3DSURFACE_DESC dstDesc{};
		source->GetDesc( &srcDesc );
		dest->GetDesc( &dstDesc );
		const bool srcRT = ( srcDesc.Usage & D3DUSAGE_RENDERTARGET ) != 0;
		const bool dstRT = ( dstDesc.Usage & D3DUSAGE_RENDERTARGET ) != 0;
		if( !srcRT && !dstRT )
		{
			return;
		}

		++s_stretchLogCount;
		PSU_LOG( "StereoStretch: eye=%d src=%ux%u usage=0x%X dst=%ux%u usage=0x%X ret=%p\n",
			s_inEyePass ? 1 : 0,
			srcDesc.Width, srcDesc.Height, srcDesc.Usage,
			dstDesc.Width, dstDesc.Height, dstDesc.Usage,
			ret );
	}

	void StereoRenderer::OnSetWorld( D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix )
	{
		if( !s_inEyePass || state != D3DTS_WORLD )
		{
			return;
		}
		s_haveWorld = matrix != nullptr;
		if( matrix )
		{
			s_worldT[ 0 ] = matrix->_41;
			s_worldT[ 1 ] = matrix->_42;
			s_worldT[ 2 ] = matrix->_43;
		}
	}

	bool StereoRenderer::ShouldSkipLocalPlayerDraw( IDirect3DDevice9 *device )
	{
		if( !s_inEyePass || !s_haveWorld || !device || !Config::Get().vr.m_fpHideBody )
		{
			return false;
		}
		float axisZ = 0.0f, feetY = 0.0f, headY = 0.0f;
		if( !VRCamera::LocalPlayerCameraSpace( axisZ, feetY, headY ) )
		{
			return false;
		}
		// Pre-transformed 2D ignores WORLD.
		DWORD fvf = 0;
		if( SUCCEEDED( device->GetFVF( &fvf ) ) && ( fvf & D3DFVF_POSITION_MASK ) == D3DFVF_XYZRHW )
		{
			return false;
		}
		++s_hideDraws;

		const float units = VRCamera::kGameUnitsPerMeter;
		const float dx = s_worldT[ 0 ];
		const float dz = s_worldT[ 2 ] - axisZ;
		const float horizontal = std::sqrt( dx * dx + dz * dz );
		const float radius = Config::Get().vr.m_fpHideRadius * units;
		const bool inside = horizontal < radius &&
			s_worldT[ 1 ] > feetY - 0.3f * units && s_worldT[ 1 ] < headY + 0.5f * units;
		if( inside )
		{
			++s_hideSkipped;
		}
		if( !s_hideLogged && s_hideSampleCount < 8 && horizontal < 2.0f * units )
		{
			float *sample = s_hideSamples[ s_hideSampleCount++ ];
			sample[ 0 ] = s_worldT[ 0 ];
			sample[ 1 ] = s_worldT[ 1 ];
			sample[ 2 ] = s_worldT[ 2 ];
			sample[ 3 ] = inside ? 1.0f : 0.0f;
		}
		return inside;
	}

	bool StereoRenderer::ShouldSkipScreenQuad( IDirect3DDevice9 *device, void *ret,
		unsigned primitiveCount, bool userPointer )
	{
		if( !s_inEyePass || !device || !Config::Get().vr.m_stereoSkipScreenQuads )
		{
			return false;
		}

		DWORD fvf = 0;
		if( FAILED( device->GetFVF( &fvf ) ) || ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
		{
			return false;
		}

		IDirect3DBaseTexture9 *stage0 = nullptr;
		if( FAILED( device->GetTexture( 0, &stage0 ) ) || !stage0 )
		{
			return false;
		}

		IDirect3DTexture9 *tex9 = nullptr;
		stage0->QueryInterface( __uuidof( IDirect3DTexture9 ), reinterpret_cast< void ** >( &tex9 ) );
		stage0->Release();
		if( !tex9 )
		{
			return false;
		}

		D3DSURFACE_DESC desc{};
		tex9->GetLevelDesc( 0, &desc );
		const bool isRT = ( desc.Usage & D3DUSAGE_RENDERTARGET ) != 0;
		const bool written = s_writtenTextures.find( tex9 ) != s_writtenTextures.end();
		const bool skip = isRT && !written;

		DWORD blendEnable = 0;
		device->GetRenderState( D3DRS_ALPHABLENDENABLE, &blendEnable );

		if( s_screenQuadLogCount < kMaxScreenQuadLogs )
		{
			++s_screenQuadLogCount;
			PSU_LOG( "StereoQuad: skip=%d up=%d prim=%u fvf=0x%X tex=%ux%u rt=%d written=%d blend=%u ret=%p\n",
				skip ? 1 : 0, userPointer ? 1 : 0, primitiveCount, fvf,
				desc.Width, desc.Height, isRT ? 1 : 0, written ? 1 : 0, blendEnable, ret );
		}

		tex9->Release();
		return skip;
	}

	const void *StereoRenderer::StretchScreenQuadIfNeeded( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride, void *ret )
	{
		if( !verts || stride < 16 || stride > kMaxStretchStride || !device )
		{
			return nullptr;
		}
		if( !s_inEyePass || s_gameW < 1 || s_gameH < 1 || s_eyeW < 1 || s_eyeH < 1 )
		{
			return nullptr;
		}
		if( s_gameW == s_eyeW && s_gameH == s_eyeH )
		{
			return nullptr;
		}
		if( !EyeColorTargetBound( device ) )
		{
			return nullptr;
		}

		DWORD fvf = 0;
		if( FAILED( device->GetFVF( &fvf ) ) || ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
		{
			return nullptr;
		}

		unsigned vertCount = 0;
		switch( static_cast< D3DPRIMITIVETYPE >( primitiveType ) )
		{
			case D3DPT_TRIANGLELIST: vertCount = primitiveCount * 3; break;
			case D3DPT_TRIANGLESTRIP:
			case D3DPT_TRIANGLEFAN: vertCount = primitiveCount + 2; break;
			case D3DPT_LINELIST: vertCount = primitiveCount * 2; break;
			case D3DPT_LINESTRIP: vertCount = primitiveCount + 1; break;
			case D3DPT_POINTLIST: vertCount = primitiveCount; break;
			default: return nullptr;
		}
		if( vertCount < 3 || vertCount > kMaxStretchVerts )
		{
			return nullptr;
		}
		if( vertCount * stride > sizeof( s_stretchQuadBuf ) )
		{
			return nullptr;
		}

		float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
		const auto *bytes = static_cast< const unsigned char * >( verts );
		for( unsigned i = 0; i < vertCount; ++i )
		{
			const float *xy = reinterpret_cast< const float * >( bytes + i * stride );
			minX = ( std::min )( minX, xy[ 0 ] );
			maxX = ( std::max )( maxX, xy[ 0 ] );
			minY = ( std::min )( minY, xy[ 1 ] );
			maxY = ( std::max )( maxY, xy[ 1 ] );
		}
		const float boxW = maxX - minX;
		const float boxH = maxY - minY;
		// Eye-pass tint: only near-fullscreen quads.
		if( boxW < 0.8f * static_cast< float >( s_gameW ) || boxH < 0.8f * static_cast< float >( s_gameH ) )
		{
			return nullptr;
		}

		const float sx = static_cast< float >( s_eyeW ) / static_cast< float >( s_gameW );
		const float sy = static_cast< float >( s_eyeH ) / static_cast< float >( s_gameH );
		std::memcpy( s_stretchQuadBuf, verts, vertCount * stride );
		for( unsigned i = 0; i < vertCount; ++i )
		{
			float *xy = reinterpret_cast< float * >( s_stretchQuadBuf + i * stride );
			xy[ 0 ] *= sx;
			xy[ 1 ] *= sy;
		}

		if( s_stretchQuadLogCount < 8 )
		{
			++s_stretchQuadLogCount;
			PSU_LOG( "StereoStretchQuad: game=%ux%u eye=%ux%u verts=%u box=%.0fx%.0f ret=%p\n",
				s_gameW, s_gameH, s_eyeW, s_eyeH, vertCount, boxW, boxH, ret );
		}
		return s_stretchQuadBuf;
	}

	void StereoRenderer::DrawNameplateToEyes( IDirect3DDevice9 *device, unsigned primitiveType,
		unsigned primitiveCount, const void *verts, unsigned stride )
	{
		if( s_inNameplateDraw || s_inEyePass || !device || !verts || !m_hasFrame || !m_haveEyeMatrices )
		{
			return;
		}
		if( stride < 16 || stride > kMaxStretchStride )
		{
			return;
		}
		const unsigned vertCount = VertCountForPrimitive( primitiveType, primitiveCount );
		if( vertCount < 3 || vertCount > kMaxStretchVerts || vertCount * stride > sizeof( s_nameplateVertBuf ) )
		{
			return;
		}
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge || m_lastEyeW < 1 || m_lastEyeH < 1 || m_lastGameW < 1 || m_lastGameH < 1 )
		{
			return;
		}

		DWORD fvf = 0;
		device->GetFVF( &fvf );
		if( ( fvf & D3DFVF_POSITION_MASK ) != D3DFVF_XYZRHW )
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

		device->SetRenderState( D3DRS_ZENABLE, FALSE );
		device->SetRenderState( D3DRS_ZWRITEENABLE, FALSE );
		device->SetDepthStencilSurface( nullptr );

		const float gw = static_cast< float >( m_lastGameW );
		const float gh = static_cast< float >( m_lastGameH );
		const float ew = static_cast< float >( m_lastEyeW );
		const float eh = static_cast< float >( m_lastEyeH );

		s_inNameplateDraw = true;
		for( int eye = 0; eye < 2; ++eye )
		{
			IDirect3DSurface9 *eyeSurf = bridge->EyeSurface( eye );
			if( !eyeSurf )
			{
				continue;
			}

			std::memcpy( s_nameplateVertBuf, verts, vertCount * stride );
			for( unsigned i = 0; i < vertCount; ++i )
			{
				float *xyzw = reinterpret_cast< float * >( s_nameplateVertBuf + i * stride );
				const float sx = xyzw[ 0 ];
				const float sy = xyzw[ 1 ];
				const float sz = xyzw[ 2 ];
				float rhw = xyzw[ 3 ];
				if( rhw > -1.0e-8f && rhw < 1.0e-8f )
				{
					continue;
				}
				const float w = 1.0f / rhw;
				const float ndcX = ( sx / gw ) * 2.0f - 1.0f;
				const float ndcY = 1.0f - ( sy / gh ) * 2.0f;
				const float clipIn[ 4 ] = { ndcX * w, ndcY * w, sz * w, w };
				float cam[ 4 ]{};
				MulVec4Mat( clipIn, m_invGameProj, cam );
				float viewClip[ 4 ]{};
				MulVec4Mat( cam, m_eyeView[ eye ], viewClip );
				float eyeClip[ 4 ]{};
				MulVec4Mat( viewClip, m_eyeProj[ eye ], eyeClip );
				if( eyeClip[ 3 ] > -1.0e-8f && eyeClip[ 3 ] < 1.0e-8f )
				{
					continue;
				}
				const float invW = 1.0f / eyeClip[ 3 ];
				xyzw[ 0 ] = ( eyeClip[ 0 ] * invW * 0.5f + 0.5f ) * ew;
				xyzw[ 1 ] = ( 0.5f - eyeClip[ 1 ] * invW * 0.5f ) * eh;
				xyzw[ 2 ] = eyeClip[ 2 ] * invW;
				xyzw[ 3 ] = invW;
			}

			// Grow around the remapped AABB center so tags stay above the avatar.
			float uiScale = Config::Get().vr.m_uiScale;
			if( uiScale < 1.0f )
			{
				uiScale = 1.0f;
			}
			else if( uiScale > 2.5f )
			{
				uiScale = 2.5f;
			}
			if( uiScale > 1.001f )
			{
				float minX = 1.0e30f, minY = 1.0e30f, maxX = -1.0e30f, maxY = -1.0e30f;
				for( unsigned i = 0; i < vertCount; ++i )
				{
					const float *xy = reinterpret_cast< const float * >( s_nameplateVertBuf + i * stride );
					minX = ( std::min )( minX, xy[ 0 ] );
					maxX = ( std::max )( maxX, xy[ 0 ] );
					minY = ( std::min )( minY, xy[ 1 ] );
					maxY = ( std::max )( maxY, xy[ 1 ] );
				}
				const float cx = ( minX + maxX ) * 0.5f;
				const float cy = ( minY + maxY ) * 0.5f;
				for( unsigned i = 0; i < vertCount; ++i )
				{
					float *xy = reinterpret_cast< float * >( s_nameplateVertBuf + i * stride );
					xy[ 0 ] = cx + ( xy[ 0 ] - cx ) * uiScale;
					xy[ 1 ] = cy + ( xy[ 1 ] - cy ) * uiScale;
				}
			}

			device->SetRenderTarget( 0, eyeSurf );
			D3DVIEWPORT9 eyeVp{ 0, 0, m_lastEyeW, m_lastEyeH, 0.0f, 1.0f };
			device->SetViewport( &eyeVp );
			RECT scissor{ 0, 0, static_cast< LONG >( m_lastEyeW ), static_cast< LONG >( m_lastEyeH ) };
			device->SetScissorRect( &scissor );
			device->DrawPrimitiveUP( static_cast< D3DPRIMITIVETYPE >( primitiveType ),
				primitiveCount, s_nameplateVertBuf, stride );
		}
		s_inNameplateDraw = false;

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

		if( !m_loggedNameplate )
		{
			m_loggedNameplate = true;
			PSU_LOG( "VR: Pass B nameplates/bubbles remapped to stereo eyes\n" );
		}
	}

	bool StereoRenderer::SceneAllowsStereo()
	{
		const auto &vr = Config::Get().vr;
		if( !vr.m_enabled || !vr.m_stereo )
		{
			return false;
		}
		const unsigned sceneMode = *reinterpret_cast< unsigned * >( 0x00B90EA8 );
		return vr.m_stereoMode == 0 || sceneMode == vr.m_stereoMode;
	}

	void StereoRenderer::RenderEyes( IDirect3DDevice9 *device, Pass pass )
	{
		const auto &vr = Config::Get().vr;
		if( !device || !pass || !SceneAllowsStereo() )
		{
			return;
		}

		EyePose views[ 2 ];
		EyePose viewSpace[ 2 ];
		D3D11Bridge *bridge = System::Get().StereoBridge();
		if( !bridge || !System::Get().NextViews( views, viewSpace ) )
		{
			return;
		}
		if( !VRCamera::HasReference() )
		{
			VRCamera::Recenter( views );
		}

		IDirect3DSurface9 *gameTarget = nullptr;
		IDirect3DSurface9 *gameDepth = nullptr;
		if( FAILED( device->GetRenderTarget( 0, &gameTarget ) ) || !gameTarget )
		{
			return;
		}
		device->GetDepthStencilSurface( &gameDepth );

		D3DSURFACE_DESC targetDesc{};
		gameTarget->GetDesc( &targetDesc );
		D3DFORMAT depthFormat = D3DFMT_D24S8;
		if( gameDepth )
		{
			D3DSURFACE_DESC depthDesc{};
			gameDepth->GetDesc( &depthDesc );
			depthFormat = depthDesc.Format;
		}

		// Prefer the OpenXR eye swapchain size so FOV maps 1:1 (stretching a 16:9
		// game RT into a near-square eye buffer warps stereo and looks like ghosts).
		UINT eyeW = targetDesc.Width;
		UINT eyeH = targetDesc.Height;
		uint32_t xrW = 0, xrH = 0;
		if( System::Get().EyeSwapchainSize( xrW, xrH ) )
		{
			eyeW = xrW;
			eyeH = xrH;
		}

		if( bridge->EnsureEyeTargets( device, eyeW, eyeH, depthFormat ) )
		{
			D3DVIEWPORT9 gameViewport{};
			D3DMATRIX gameView{};
			D3DMATRIX gameProj{};
			device->GetViewport( &gameViewport );
			device->GetTransform( D3DTS_VIEW, &gameView );
			device->GetTransform( D3DTS_PROJECTION, &gameProj );

			const float unitsPerMeter = VRCamera::kGameUnitsPerMeter * ( vr.m_worldScale > 0.01f ? vr.m_worldScale : 1.0f );
			const bool firstPerson = VRCamera::FirstPersonActive();
			const DWORD clearFlags = D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | ( HasStencil( depthFormat ) ? D3DCLEAR_STENCIL : 0 );

			m_gameProj = gameProj;
			m_haveEyeMatrices = InvertMatrix( gameProj, m_invGameProj );

			for( int eye = 0; eye < 2; ++eye )
			{
				D3DMATRIX eyeView{};
				D3DMATRIX eyeProj{};
				if( firstPerson )
				{
					VRCamera::BuildEyeFirstPerson( views[ eye ], unitsPerMeter, gameProj, eyeView, eyeProj );
				}
				else
				{
					VRCamera::BuildEye( views[ eye ], unitsPerMeter, gameProj, eyeView, eyeProj );
				}
				m_eyeView[ eye ] = eyeView;
				m_eyeProj[ eye ] = eyeProj;

				IDirect3DSurface9 *eyeSurface = bridge->EyeSurface( eye );
				device->SetRenderTarget( 0, eyeSurface );
				device->SetDepthStencilSurface( bridge->EyeDepth() );
				D3DVIEWPORT9 eyeViewport{ 0, 0, eyeW, eyeH, 0.0f, 1.0f };
				device->SetViewport( &eyeViewport );
				device->Clear( 0, nullptr, clearFlags, D3DCOLOR_XRGB( 0, 0, 0 ), 1.0f, 0 );
				device->SetTransform( D3DTS_VIEW, &eyeView );
				device->SetTransform( D3DTS_PROJECTION, &eyeProj );

				BeginEyePass( eyeSurface, eyeW, eyeH, targetDesc.Width, targetDesc.Height );
				pass();
				EndEyePass();

				m_poses[ eye ] = views[ eye ];
				m_viewSpacePoses[ eye ] = viewSpace[ eye ];
			}

			device->SetRenderTarget( 0, gameTarget );
			device->SetDepthStencilSurface( gameDepth );
			device->SetViewport( &gameViewport );
			device->SetTransform( D3DTS_VIEW, &gameView );
			device->SetTransform( D3DTS_PROJECTION, &gameProj );
			m_lastEyeW = eyeW;
			m_lastEyeH = eyeH;
			m_lastGameW = targetDesc.Width;
			m_lastGameH = targetDesc.Height;
			m_hasFrame = true;
			VRHud::Get().BeginStereoFrame( device, targetDesc.Width, targetDesc.Height );

			if( !m_loggedFirst )
			{
				m_loggedFirst = true;
				PSU_LOG( "Stereo: first eye pair %ux%u (game RT %ux%u), %.1f units/m, skipScreenQuads=%d, fovL=[%.2f %.2f %.2f %.2f]\n",
					eyeW, eyeH, targetDesc.Width, targetDesc.Height, unitsPerMeter,
					vr.m_stereoSkipScreenQuads ? 1 : 0,
					views[ 0 ].fovAngleLeft, views[ 0 ].fovAngleRight, views[ 0 ].fovAngleUp, views[ 0 ].fovAngleDown );
			}
		}

		gameTarget->Release();
		if( gameDepth )
		{
			gameDepth->Release();
		}
	}
}
