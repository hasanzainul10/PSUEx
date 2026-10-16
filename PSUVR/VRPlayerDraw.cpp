#include "VRPlayerDraw.h"

#include "StereoRenderer.h"
#include "VRCamera.h"
#include "VRHands.h"
#include "../Misc/Log.h"
#include "../PSUExConfig.h"
#include "../PSUMemory/Memory.hpp"
#include "../PSUStructs/PSUPlayerData.h"

#include <d3d9.h>

#include <cstdint>

namespace PSUVR
{
	namespace
	{
		// PSU_DrawLayer 0x00560830: *(0x00B9DB38) -> 64 entries x 8 bytes (+0 first node).
		// Node: +0x08 next, +0x0C object, +0x10 cdecl void callback( object ).
		constexpr uintptr_t kLayerTablePtr = 0x00B9DB38;
		constexpr unsigned kLayerCount = 0x40;
		// Pass A (3D world) draws layers 0x02..0x29.
		constexpr unsigned kFirstWorldLayer = 0x02;
		constexpr unsigned kLastWorldLayer = 0x29;
		constexpr unsigned kLayerStride = 8;
		constexpr uintptr_t kNodeNext = 0x08;
		constexpr uintptr_t kNodeObject = 0x0C;
		constexpr uintptr_t kNodeCallback = 0x10;
		constexpr unsigned kMaxNodesPerLayer = 4096;

		typedef void( __cdecl *DrawCallback )( void *object );

		struct Patch
		{
			uint8_t *node;
			DrawCallback original;
		};

		constexpr unsigned kMaxPatches = 8;
		Patch s_patches[ kMaxPatches ]{};
		unsigned s_patchCount = 0;
		bool s_drawingLocalPlayer = false;
		bool s_forcingVisible = false;
		void *s_playerObject = nullptr;
		bool s_loggedPatch = false;
		bool s_loggedDiscovery = false;
		unsigned s_frame = 0;
		unsigned s_nextScanFrame = 0;

		// Full layer walks are the expensive part (hundreds of nodes). Once patched, only
		// the patched nodes are re-checked; a full walk runs on a miss or as a safety net.
		constexpr unsigned kRescanFrames = 300;      // ~3 s at 90 Hz while patched
		constexpr unsigned kRetryScanFrames = 45;    // while the player has no node yet

		// Small structs never straddle a page boundary in practice; check the end only
		// for larger spans (VirtualQuery per node adds up at 90 Hz).
		bool Readable( const void *p, size_t bytes )
		{
			if( !p || !PSUMemory::IsValidPointer( p ) )
			{
				return false;
			}
			return bytes <= 0x40 || PSUMemory::IsValidPointer( static_cast< const uint8_t * >( p ) + bytes - 1 );
		}

		// The loaded player character: TDivModel at render object +0x04 (render object at draw
		// object +0x160). VRHands edits its bone pose when the game builds it (0x0074B760).
		const uint8_t *s_divModel = nullptr;

		void NotePlayerModel( void *drawObject )
		{
			__try
			{
				const uint8_t *render = *reinterpret_cast< const uint8_t * const * >( static_cast< const uint8_t * >( drawObject ) + 0x160 );
				const uint8_t *div = Readable( render, 0x30 ) ? *reinterpret_cast< const uint8_t * const * >( render + 0x04 ) : nullptr;
				s_divModel = Readable( div, 0x70 ) ? div : nullptr;
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				s_divModel = nullptr;
			}
		}

		void __cdecl LocalPlayerDrawThunk( void *object )
		{
			DrawCallback original = nullptr;
			unsigned patchIndex = kMaxPatches;
			for( unsigned i = 0; i < s_patchCount; ++i )
			{
				if( *reinterpret_cast< void ** >( s_patches[ i ].node + kNodeObject ) == object )
				{
					original = s_patches[ i ].original;
					patchIndex = i;
					break;
				}
			}
#ifdef _DEBUG
			{
				// Diagnostics: draw callback calls per patched node and camera state.
				static unsigned s_calls[ kMaxPatches + 1 ][ 8 ]{};
				static DWORD s_lastLog = 0;
				++s_calls[ patchIndex ][ VRCamera::CameraState() & 7 ];
				if( GetTickCount() - s_lastLog >= 3000 )
				{
					s_lastLog = GetTickCount();
					for( unsigned i = 0; i <= kMaxPatches; ++i )
					{
						const unsigned *c = s_calls[ i ];
						if( c[ 1 ] + c[ 3 ] + c[ 4 ] )
						{
							PSU_LOG( "VRPlayerDraw: node %u calls state1 %u state3 %u state4 %u\n", i, c[ 1 ], c[ 3 ], c[ 4 ] );
						}
					}
				}
			}
#endif
			if( !original && s_patchCount > 0 )
			{
				original = s_patches[ 0 ].original;
			}
			if( !original )
			{
				return;
			}
			NotePlayerModel( object );
			// Free aim hides the player (draw object +9 bit 0x04 skips the node, +0x390 bit 0x10
			// skips TPlayerPad's draw 0x00526A80). With the arms on the controllers, draw it
			// anyway: clear those bits for this call only.
			uint8_t *flags = static_cast< uint8_t * >( object );
			uint8_t saved9 = 0, saved390 = 0, saved1BC = 0;
			const bool forceShow = VRCamera::CameraState() == 4 && Config::Get().vr.m_handsFollow && Readable( flags + 0x390, 4 );
			if( forceShow )
			{
				saved9 = flags[ 9 ];
				saved390 = flags[ 0x390 ];
				saved1BC = flags[ 0x1BC ];
				static DWORD s_lastLog = 0;
				if( GetTickCount() - s_lastLog >= 2000 )
				{
					s_lastLog = GetTickCount();
					const uint8_t *render = *reinterpret_cast< const uint8_t * const * >( flags + 0x160 );
#ifdef _DEBUG
					PSU_LOG( "VRPlayerDraw: free aim draw flags +9=%02X +390=%02X +1BC=%02X [B91140]=%08lX mode=%d render type %d +2C=%02X\n",
						saved9, saved390, saved1BC, *reinterpret_cast< const DWORD * >( 0x00B91140 ),
						*reinterpret_cast< const int * >( 0x009946C8 ),
						Readable( render, 0x30 ) ? *reinterpret_cast< const int * >( render + 0x28 ) : -1,
						Readable( render, 0x30 ) ? render[ 0x2C ] : 0 );
#endif
				}
				flags[ 9 ] = static_cast< uint8_t >( saved9 & ~0x04 );
				flags[ 0x390 ] = static_cast< uint8_t >( saved390 & ~0x10 );
				// +0x1BC bit 0x10 with [0x00B91140] set: 0x00541EA0 skips the draw (and the update
				// 0x00541CD0 marks the node visible anyway).
				flags[ 0x1BC ] = static_cast< uint8_t >( saved1BC & ~0x10 );
			}
			// The models (body, weapons and other attachments) draw only with +0x14 bit 0x10.
			constexpr unsigned kMaxModels = 32;
			uint32_t *modelFlags[ kMaxModels ];
			uint32_t savedModelFlags[ kMaxModels ];
			unsigned modelCount = 0;
			if( forceShow )
			{
				__try
				{
					const uint8_t *render = *reinterpret_cast< const uint8_t * const * >( flags + 0x160 );
					auto add = [ & ]( const void *model )
					{
						if( modelCount < kMaxModels && Readable( model, 0x18 ) )
						{
							uint32_t *f = reinterpret_cast< uint32_t * >( static_cast< uint8_t * >( const_cast< void * >( model ) ) + 0x14 );
							modelFlags[ modelCount ] = f;
							savedModelFlags[ modelCount ] = *f;
							++modelCount;
						}
					};
					if( Readable( render, 0x4C ) )
					{
						add( *reinterpret_cast< void * const * >( render + 0x04 ) );
						add( *reinterpret_cast< void * const * >( render + 0x20 ) );
						add( *reinterpret_cast< void * const * >( render + 0x24 ) );
						// Attachment lists (render +0x30, 7 heads): node +0 model, +8 next.
						for( unsigned list = 0; list < 7; ++list )
						{
							const uint8_t *node = *reinterpret_cast< const uint8_t * const * >( render + 0x30 + list * 4 );
							for( unsigned n = 0; node && n < 16 && Readable( node, 0x10 ); ++n )
							{
								add( *reinterpret_cast< void * const * >( node ) );
								node = *reinterpret_cast< const uint8_t * const * >( node + 8 );
							}
						}
					}
					unsigned hidden = 0;
					for( unsigned i = 0; i < modelCount; ++i )
					{
						hidden += ( *modelFlags[ i ] & 0x10 ) ? 0 : 1;
						*modelFlags[ i ] |= 0x10;
					}
					static DWORD s_lastModelLog = 0;
					if( GetTickCount() - s_lastModelLog >= 2000 )
					{
						s_lastModelLog = GetTickCount();
#ifdef _DEBUG
						PSU_LOG( "VRPlayerDraw: free aim models %u, %u without the draw bit (body +14=%08lX)\n", modelCount, hidden,
							modelCount ? static_cast< unsigned long >( savedModelFlags[ 0 ] ) : 0ul );
#endif
					}
				}
				__except( EXCEPTION_EXECUTE_HANDLER )
				{
					modelCount = 0;
				}
			}
			s_drawingLocalPlayer = true;
			s_forcingVisible = forceShow;
			original( object );
			s_forcingVisible = false;
			s_drawingLocalPlayer = false;
			if( forceShow )
			{
				flags[ 9 ] = static_cast< uint8_t >( ( flags[ 9 ] & ~0x04 ) | ( saved9 & 0x04 ) );
				flags[ 0x390 ] = static_cast< uint8_t >( ( flags[ 0x390 ] & ~0x10 ) | ( saved390 & 0x10 ) );
				flags[ 0x1BC ] = static_cast< uint8_t >( ( flags[ 0x1BC ] & ~0x10 ) | ( saved1BC & 0x10 ) );
				for( unsigned i = 0; i < modelCount; ++i )
				{
					*modelFlags[ i ] = ( *modelFlags[ i ] & ~0x10u ) | ( savedModelFlags[ i ] & 0x10u );
				}
			}
			if( s_divModel )
			{
				VRHands::AfterPlayerDraw( const_cast< uint8_t * >( s_divModel ) );
			}
		}

		DrawCallback NodeCallback( uint8_t *node )
		{
			return *reinterpret_cast< DrawCallback * >( node + kNodeCallback );
		}

		void RestoreAll()
		{
			for( unsigned i = 0; i < s_patchCount; ++i )
			{
				uint8_t *node = s_patches[ i ].node;
				if( Readable( node, kNodeCallback + 4 ) && NodeCallback( node ) == &LocalPlayerDrawThunk )
				{
					*reinterpret_cast< DrawCallback * >( node + kNodeCallback ) = s_patches[ i ].original;
				}
			}
			s_patchCount = 0;
			s_divModel = nullptr;
		}

		template< typename Fn >
		void ForEachNode( Fn &&fn )
		{
			uint8_t *const *tablePtr = reinterpret_cast< uint8_t *const * >( kLayerTablePtr );
			if( !Readable( tablePtr, 4 ) || !Readable( *tablePtr, kLayerCount * kLayerStride ) )
			{
				return;
			}
			uint8_t *table = *tablePtr;
			for( unsigned layer = kFirstWorldLayer; layer <= kLastWorldLayer; ++layer )
			{
				uint8_t *node = *reinterpret_cast< uint8_t ** >( table + layer * kLayerStride );
				for( unsigned n = 0; node && n < kMaxNodesPerLayer; ++n )
				{
					if( !Readable( node, kNodeCallback + 4 ) )
					{
						break;
					}
					fn( layer, node );
					node = *reinterpret_cast< uint8_t ** >( node + kNodeNext );
				}
			}
		}

		struct FoundNode
		{
			uint8_t *node;
			unsigned layer;
		};

		// Walks the pass-A layers and collects nodes drawing `player`. Reads are guarded
		// by SEH instead of VirtualQuery per node; returns false if a read faulted.
		bool FindPlayerNodes( void *player, FoundNode *out, unsigned maxOut, unsigned &count )
		{
			count = 0;
			__try
			{
				uint8_t *table = *reinterpret_cast< uint8_t *const * >( kLayerTablePtr );
				if( !table )
				{
					return true;
				}
				for( unsigned layer = kFirstWorldLayer; layer <= kLastWorldLayer; ++layer )
				{
					uint8_t *node = *reinterpret_cast< uint8_t ** >( table + layer * kLayerStride );
					for( unsigned n = 0; node && n < kMaxNodesPerLayer; ++n )
					{
						if( *reinterpret_cast< void ** >( node + kNodeObject ) == player && count < maxOut )
						{
							out[ count++ ] = { node, layer };
						}
						node = *reinterpret_cast< uint8_t ** >( node + kNodeNext );
					}
				}
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				count = 0;
				return false;
			}
			return true;
		}

		// Patched nodes still draw `player` (and our thunk is still in place).
		bool PatchesIntact( void *player )
		{
			__try
			{
				for( unsigned i = 0; i < s_patchCount; ++i )
				{
					uint8_t *node = s_patches[ i ].node;
					if( *reinterpret_cast< void ** >( node + kNodeObject ) != player ||
						*reinterpret_cast< DrawCallback * >( node + kNodeCallback ) != &LocalPlayerDrawThunk )
					{
						return false;
					}
				}
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
			return s_patchCount > 0;
		}

		// One-time dump when no node holds the player object directly.
		void LogDiscovery( void *player )
		{
			if( s_loggedDiscovery )
			{
				return;
			}
			s_loggedDiscovery = true;
			PSU_LOG( "VRPlayerDraw: no draw node holds the player object %p; dumping pass-A layers\n", player );
			unsigned lines = 0;
			ForEachNode( [ & ]( unsigned layer, uint8_t *node )
			{
				if( lines >= 200 )
				{
					return;
				}
				void *object = *reinterpret_cast< void ** >( node + kNodeObject );
				int ref = -1;
				if( Readable( object, 0x400 ) )
				{
					for( unsigned off = 0; off < 0x400; off += 4 )
					{
						if( *reinterpret_cast< void ** >( static_cast< uint8_t * >( object ) + off ) == player )
						{
							ref = static_cast< int >( off );
							break;
						}
					}
				}
				++lines;
				PSU_LOG( "VRPlayerDraw:   layer=0x%02X node=%p object=%p callback=%p%s\n",
					layer, node, object, reinterpret_cast< void * >( NodeCallback( node ) ),
					ref >= 0 ? " <- holds player" : "" );
				if( ref >= 0 )
				{
					PSU_LOG( "VRPlayerDraw:     player pointer at object+0x%X\n", ref );
				}
			} );
		}
	}

	namespace VRPlayerDraw
	{
		void Update( bool enable )
		{
			++s_frame;
			if( !enable || Config::Get().vr.m_fpBody == "full" )
			{
				RestoreAll();
				return;
			}
			const PSUPlayer &player = PSUPlayer::Get();
			if( !PSUPlayer::IsReady() )
			{
				RestoreAll();
				return;
			}
			void *playerObject = const_cast< PSUPlayer * >( &player );
			if( playerObject != s_playerObject )
			{
				// New area or character: old nodes may be gone, so drop them without writing.
				s_patchCount = 0;
				s_divModel = nullptr;
			s_divModel = nullptr;
				s_playerObject = playerObject;
				s_nextScanFrame = s_frame;
			}

			// Cheap path: the patched nodes still draw the player.
			const bool intact = PatchesIntact( playerObject );
			if( intact && s_frame < s_nextScanFrame )
			{
				return;
			}
			if( !intact && s_patchCount == 0 && s_frame < s_nextScanFrame )
			{
				return;
			}

			FoundNode found[ kMaxPatches ];
			unsigned foundCount = 0;
			FindPlayerNodes( playerObject, found, kMaxPatches, foundCount );
			s_nextScanFrame = s_frame + ( foundCount > 0 ? kRescanFrames : kRetryScanFrames );

			// Keep only patches whose node is still in the list.
			unsigned kept = 0;
			for( unsigned i = 0; i < s_patchCount; ++i )
			{
				for( unsigned f = 0; f < foundCount; ++f )
				{
					if( found[ f ].node == s_patches[ i ].node )
					{
						s_patches[ kept++ ] = s_patches[ i ];
						break;
					}
				}
			}
			s_patchCount = kept;

			for( unsigned f = 0; f < foundCount; ++f )
			{
				uint8_t *node = found[ f ].node;
				const DrawCallback callback = NodeCallback( node );
				if( callback == &LocalPlayerDrawThunk || !callback )
				{
					continue;
				}
				unsigned slot = s_patchCount;
				for( unsigned i = 0; i < s_patchCount; ++i )
				{
					if( s_patches[ i ].node == node )
					{
						slot = i;
						break;
					}
				}
				if( slot >= kMaxPatches )
				{
					continue;
				}
				if( slot == s_patchCount )
				{
					++s_patchCount;
				}
				s_patches[ slot ] = { node, callback };
				*reinterpret_cast< DrawCallback * >( node + kNodeCallback ) = &LocalPlayerDrawThunk;
				if( !s_loggedPatch )
				{
					s_loggedPatch = true;
					PSU_LOG( "VRPlayerDraw: patched local player draw (layer=0x%02X node=%p object=%p callback=%p)\n",
						found[ f ].layer, node, playerObject, reinterpret_cast< void * >( callback ) );
					// The callback calls the node object's vtable +0x0C (update) and +0x10 (draw):
					// log them and the MSVC RTTI class name (vtable[-1] = complete object locator).
					void *drawObject = *reinterpret_cast< void ** >( node + kNodeObject );
					if( Readable( drawObject, 4 ) )
					{
						void **vtable = *static_cast< void *** >( drawObject );
						if( Readable( vtable - 1, 0x20 ) )
						{
							const char *className = "?";
							const uint8_t *locator = static_cast< const uint8_t * >( vtable[ -1 ] );
							if( Readable( locator, 0x14 ) )
							{
								const uint8_t *typeDescriptor = *reinterpret_cast< const uint8_t * const * >( locator + 0x0C );
								if( Readable( typeDescriptor, 0x20 ) )
								{
									className = reinterpret_cast< const char * >( typeDescriptor + 8 );
								}
							}
							PSU_LOG( "VRPlayerDraw: draw object %p vtable=%p [0]=%p [1]=%p [2]=%p [3]=%p [4]=%p [5]=%p class=%.60s\n",
								drawObject, vtable, vtable[ 0 ], vtable[ 1 ], vtable[ 2 ], vtable[ 3 ], vtable[ 4 ], vtable[ 5 ], className );
							// Hands RE: the render object at +0x160 holds the models at +0x04 / +0x20 / +0x24
							// (bone matrix by index at vtable +0x18, skeleton at vtable +0x28).
							const auto logObject = [ & ]( const char *label, const void *object )
							{
								if( !Readable( object, 4 ) )
								{
									return;
								}
								void *const *vt = *static_cast< void *const *const * >( object );
								if( !Readable( vt - 1, 0x40 ) )
								{
									return;
								}
								const char *name = "?";
								const uint8_t *col = static_cast< const uint8_t * >( vt[ -1 ] );
								if( Readable( col, 0x14 ) )
								{
									const uint8_t *td = *reinterpret_cast< const uint8_t * const * >( col + 0x0C );
									if( Readable( td, 0x20 ) )
									{
										name = reinterpret_cast< const char * >( td + 8 );
									}
								}
								PSU_LOG( "VRPlayerDraw:   %s %p vtable=%p [4]=%p [6]=%p [10]=%p class=%.60s\n",
									label, object, vt, vt[ 4 ], vt[ 6 ], vt[ 10 ], name );
							};
							const uint8_t *renderObject = *reinterpret_cast< const uint8_t * const * >(
								static_cast< const uint8_t * >( drawObject ) + 0x160 );
							if( Readable( renderObject, 0x30 ) )
							{
								logObject( "render+0x04", *reinterpret_cast< void * const * >( renderObject + 0x04 ) );
								logObject( "render+0x20", *reinterpret_cast< void * const * >( renderObject + 0x20 ) );
								logObject( "render+0x24", *reinterpret_cast< void * const * >( renderObject + 0x24 ) );
							}
						}
					}
				}
			}
			if( foundCount == 0 )
			{
				LogDiscovery( playerObject );
			}
		}

		bool DrawingLocalPlayer()
		{
			return s_drawingLocalPlayer;
		}

		bool ForcingVisible()
		{
			return s_forcingVisible;
		}

		void OnBonePose( float *out, const uint8_t *skeleton )
		{
			static unsigned s_poses = 0, s_noModel = 0;
			static DWORD s_lastLog = 0;
			++s_poses;
			if( GetTickCount() - s_lastLog >= 3000 )
			{
				s_lastLog = GetTickCount();
#ifdef _DEBUG
				PSU_LOG( "VRPlayerDraw: %u bone poses, %u without a player model (model %p)\n", s_poses, s_noModel, s_divModel );
#endif
			}
			const uint8_t *div = s_divModel;
			if( !out || !skeleton || !div )
			{
				++s_noModel;
				return;
			}
			bool player = false;
			__try
			{
				player = *reinterpret_cast< float * const * >( div + 0x64 ) == out;
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				s_divModel = nullptr;
			}
			if( player )
			{
				VRHands::Apply( const_cast< uint8_t * >( div ), out, skeleton );
			}
		}
	}

	LocalPlayerDrawScope::LocalPlayerDrawScope( IDirect3DDevice9 *device )
	{
#ifdef _DEBUG
		// Diagnostics (free aim hides the player): the local player's draw state in the eye
		// passes, once per 2 s per camera state.
		if( device && s_drawingLocalPlayer && StereoRenderer::InEyePass() )
		{
			static DWORD s_lastLog[ 8 ]{};
			static unsigned s_draws[ 8 ]{};
			const int state = VRCamera::CameraState() & 7;
			++s_draws[ state ];
			if( GetTickCount() - s_lastLog[ state ] >= 2000 )
			{
				s_lastLog[ state ] = GetTickCount();
				DWORD blend = 0, src = 0, dst = 0, test = 0, ref = 0, func = 0, factor = 0, z = 0, zwrite = 0, cull = 0, clip = 0;
				device->GetRenderState( D3DRS_ALPHABLENDENABLE, &blend );
				device->GetRenderState( D3DRS_SRCBLEND, &src );
				device->GetRenderState( D3DRS_DESTBLEND, &dst );
				device->GetRenderState( D3DRS_ALPHATESTENABLE, &test );
				device->GetRenderState( D3DRS_ALPHAREF, &ref );
				device->GetRenderState( D3DRS_ALPHAFUNC, &func );
				device->GetRenderState( D3DRS_TEXTUREFACTOR, &factor );
				device->GetRenderState( D3DRS_ZENABLE, &z );
				device->GetRenderState( D3DRS_ZWRITEENABLE, &zwrite );
				device->GetRenderState( D3DRS_CULLMODE, &cull );
				device->GetRenderState( D3DRS_CLIPPLANEENABLE, &clip );
				D3DMATERIAL9 material{};
				device->GetMaterial( &material );
				IDirect3DVertexShader9 *vs = nullptr;
				device->GetVertexShader( &vs );
				PSU_LOG( "VRPlayerDraw: state %d draws %u: blend %lu %lu/%lu test %lu ref %lu func %lu factor %08lX mat a %.2f z %lu/%lu cull %lu clip %lu vs %d\n",
					state, s_draws[ state ], blend, src, dst, test, ref, func, factor, material.Diffuse.a, z, zwrite, cull, clip,
					vs ? 1 : 0 );
				if( vs )
				{
					vs->Release();
				}
				s_draws[ state ] = 0;
			}
		}
#endif
		if( !device || !s_drawingLocalPlayer || !StereoRenderer::InEyePass() || !VRCamera::FirstPersonActive() )
		{
			return;
		}
		const auto &vr = Config::Get().vr;
		if( vr.m_fpBody == "hidden" )
		{
			m_skip = true;
			return;
		}
		if( ( vr.m_fpBody != "headless" && vr.m_fpBody != "hands" ) || VRHands::HeadHidden() )
		{
			return;
		}
		// Fixed-function clip planes live in the space after WORLD. WORLD here already
		// holds the game camera, which first person keeps level at eye height, so
		// "y below the neck" is a plane in that space: -y + neckY >= 0.
		DWORD enable = 0;
		device->GetRenderState( D3DRS_CLIPPLANEENABLE, &enable );
		device->GetClipPlane( 0, m_savedPlane );
		m_savedClipEnable = enable;
		const float neckY = -vr.m_fpNeckDrop * VRCamera::kGameUnitsPerMeter;
		const float plane[ 4 ] = { 0.0f, -1.0f, 0.0f, neckY };
		device->SetClipPlane( 0, plane );
		device->SetRenderState( D3DRS_CLIPPLANEENABLE, enable | D3DCLIPPLANE0 );
		m_device = device;
	}

	LocalPlayerDrawScope::~LocalPlayerDrawScope()
	{
		if( !m_device )
		{
			return;
		}
		m_device->SetClipPlane( 0, m_savedPlane );
		m_device->SetRenderState( D3DRS_CLIPPLANEENABLE, m_savedClipEnable );
	}
}
