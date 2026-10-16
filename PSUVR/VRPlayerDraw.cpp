#include "VRPlayerDraw.h"

#include "StereoRenderer.h"
#include "VRCamera.h"
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

#ifdef _DEBUG
		// Hands RE (Debug): find the per-frame writer of the pose buffer. The player's TModel
		// (render object +0x20) keeps one 64-byte matrix per bone at model+0x64; L_hand is #17.
		// A hardware write watchpoint on that matrix (this thread only) logs the writing code
		// and the code addresses on its stack.
		constexpr DWORD kArmWatchException = 0xE0505357;
		constexpr unsigned kWatchBone = 17;
		constexpr unsigned kMaxWatchSites = 16;
		constexpr unsigned kWatchStack = 10;
		constexpr unsigned kMaxWatchHits = 2000;

		struct WatchSite
		{
			DWORD eip;
			unsigned count;
			DWORD stack[ kWatchStack ];
		};

		WatchSite s_sites[ kMaxWatchSites ]{};
		volatile LONG s_siteCount = 0;
		volatile LONG s_watchHits = 0;
		DWORD s_watchAddr = 0;
		bool s_watchArmed = false;
		bool s_watchDone = false;

		LONG CALLBACK WatchHandler( EXCEPTION_POINTERS *info )
		{
			CONTEXT *ctx = info->ContextRecord;
			if( info->ExceptionRecord->ExceptionCode == kArmWatchException )
			{
				// Dr0 on the matrix's element 3, Dr1 on element 12 (either translation layout).
				// Write-only, 4 bytes: RW = 01, LEN = 11.
				ctx->Dr0 = s_watchAddr + 3 * 4;
				ctx->Dr1 = s_watchAddr + 12 * 4;
				ctx->Dr7 = 0x1 | 0x4 | ( 0xD << 16 ) | ( 0xD << 20 );
				return EXCEPTION_CONTINUE_EXECUTION;
			}
			if( info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || ( ctx->Dr6 & 0x3 ) == 0 )
			{
				return EXCEPTION_CONTINUE_SEARCH;
			}
			ctx->Dr6 = 0;
			if( InterlockedIncrement( &s_watchHits ) >= static_cast< LONG >( kMaxWatchHits ) )
			{
				ctx->Dr7 = 0;
			}
			for( LONG i = 0; i < s_siteCount; ++i )
			{
				if( s_sites[ i ].eip == ctx->Eip )
				{
					++s_sites[ i ].count;
					return EXCEPTION_CONTINUE_EXECUTION;
				}
			}
			if( s_siteCount < static_cast< LONG >( kMaxWatchSites ) )
			{
				WatchSite &site = s_sites[ s_siteCount ];
				site.eip = ctx->Eip;
				site.count = 1;
				const DWORD *stack = reinterpret_cast< const DWORD * >( ctx->Esp );
				unsigned found = 0;
				for( unsigned n = 0; n < 96 && found < kWatchStack; ++n )
				{
					const DWORD value = stack[ n ];
					if( value >= 0x00401000 && value < 0x00880000 )
					{
						site.stack[ found++ ] = value;
					}
				}
				++s_siteCount;
			}
			return EXCEPTION_CONTINUE_EXECUTION;
		}

		void ProbeHandBones( void *drawObject )
		{
			static DWORD s_lastTick = 0;
			static unsigned s_reports = 0;
			if( s_watchDone || GetTickCount() - s_lastTick < 1000 )
			{
				return;
			}
			s_lastTick = GetTickCount();
			if( s_watchArmed )
			{
				PSU_LOG( "HandsWatch: %ld hits, %ld sites (thread %lu)\n", s_watchHits, s_siteCount, GetCurrentThreadId() );
				for( LONG i = 0; i < s_siteCount; ++i )
				{
					const WatchSite &site = s_sites[ i ];
					PSU_LOG( "HandsWatch:   eip %08lX x%u stack %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX\n",
						site.eip, site.count, site.stack[ 0 ], site.stack[ 1 ], site.stack[ 2 ], site.stack[ 3 ], site.stack[ 4 ],
						site.stack[ 5 ], site.stack[ 6 ], site.stack[ 7 ], site.stack[ 8 ], site.stack[ 9 ] );
				}
				if( ++s_reports >= 5 || s_watchHits >= static_cast< LONG >( kMaxWatchHits ) )
				{
					s_watchDone = true;
				}
				return;
			}
			__try
			{
				const uint8_t *render = *reinterpret_cast< const uint8_t * const * >( static_cast< const uint8_t * >( drawObject ) + 0x160 );
				if( !Readable( render, 0x30 ) )
				{
					return;
				}
				const uint8_t *model = *reinterpret_cast< const uint8_t * const * >( render + 0x20 );
				if( !Readable( model, 0x70 ) )
				{
					return;
				}
				const uint8_t *pose = *reinterpret_cast< const uint8_t * const * >( model + 0x64 );
				PSU_LOG( "HandsWatch: model %p vt %08lX flags14 %08lX flags18 %08lX local1C %p pose64 %p count68 %ld (thread %lu)\n",
					model, *reinterpret_cast< const DWORD * >( model ), *reinterpret_cast< const DWORD * >( model + 0x14 ),
					*reinterpret_cast< const DWORD * >( model + 0x18 ), *reinterpret_cast< void * const * >( model + 0x1C ), pose,
					*reinterpret_cast< const LONG * >( model + 0x68 ), GetCurrentThreadId() );
				if( !Readable( pose, ( kWatchBone + 1 ) * 0x40 ) )
				{
					return;
				}
				const float *m = reinterpret_cast< const float * >( pose + kWatchBone * 0x40 );
				PSU_LOG( "HandsWatch: L_hand pose [%.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f | %.2f %.2f %.2f %.2f]\n",
					m[ 0 ], m[ 1 ], m[ 2 ], m[ 3 ], m[ 4 ], m[ 5 ], m[ 6 ], m[ 7 ], m[ 8 ], m[ 9 ], m[ 10 ], m[ 11 ], m[ 12 ], m[ 13 ],
					m[ 14 ], m[ 15 ] );
				s_watchAddr = reinterpret_cast< DWORD >( m );
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				PSU_LOG( "HandsWatch: exception\n" );
				s_watchDone = true;
				return;
			}
			if( s_watchAddr )
			{
				AddVectoredExceptionHandler( 1, &WatchHandler );
				RaiseException( kArmWatchException, 0, 0, nullptr );
				s_watchArmed = true;
				PSU_LOG( "HandsWatch: armed on %08lX\n", s_watchAddr );
			}
		}
#endif

		void __cdecl LocalPlayerDrawThunk( void *object )
		{
			DrawCallback original = nullptr;
			for( unsigned i = 0; i < s_patchCount; ++i )
			{
				if( *reinterpret_cast< void ** >( s_patches[ i ].node + kNodeObject ) == object )
				{
					original = s_patches[ i ].original;
					break;
				}
			}
			if( !original && s_patchCount > 0 )
			{
				original = s_patches[ 0 ].original;
			}
			if( !original )
			{
				return;
			}
			s_drawingLocalPlayer = true;
			original( object );
			s_drawingLocalPlayer = false;
#ifdef _DEBUG
			// After the draw: the pose is built while drawing.
			ProbeHandBones( object );
#endif
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
	}

	LocalPlayerDrawScope::LocalPlayerDrawScope( IDirect3DDevice9 *device )
	{
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
		if( vr.m_fpBody != "headless" )
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
