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
		// Hands test (Debug): the local player's TModel (render object +0x20) owns the pose
		// buffer at model +0x64 that 0x0074D1D0 fills. Its bone names come from the model's
		// vtable +0x28 object (+0x28 names, 0x00757500 lookup).
		typedef void *( __thiscall *GetSkeletonFn )( void *model );
		typedef int( __cdecl *FindBoneFn )( void *names, const char *name );

		float *s_playerPose = nullptr;
		const uint8_t *s_playerModel = nullptr;
		const uint8_t *s_divModel = nullptr;  // render object +0x04 (TDivModel, the loaded character)
		const uint8_t *s_playerRecords = nullptr;
		int s_armBones[ 3 ] = { -1, -1, -1 };  // L_upperarm, L_forearm, L_hand

		void NotePlayerModel( void *drawObject )
		{
			__try
			{
				const uint8_t *render = *reinterpret_cast< const uint8_t * const * >( static_cast< const uint8_t * >( drawObject ) + 0x160 );
				if( !Readable( render, 0x30 ) )
				{
					return;
				}
				void *model = *reinterpret_cast< void * const * >( render + 0x20 );
				if( !Readable( model, 0x70 ) )
				{
					return;
				}
				s_playerPose = *reinterpret_cast< float * const * >( static_cast< const uint8_t * >( model ) + 0x64 );
				s_playerModel = static_cast< const uint8_t * >( model );
				s_divModel = *reinterpret_cast< const uint8_t * const * >( render + 0x04 );
				if( s_armBones[ 2 ] >= 0 )
				{
					return;
				}
				void *const *vt = *static_cast< void *const *const * >( model );
				const uint8_t *skeleton = static_cast< const uint8_t * >( reinterpret_cast< GetSkeletonFn >( vt[ 10 ] )( model ) );
				if( !Readable( skeleton, 0x2C ) )
				{
					return;
				}
				void *names = *reinterpret_cast< void * const * >( skeleton + 0x28 );
				if( !Readable( names, 0x0C ) )
				{
					return;
				}
				static const char *const kArm[] = { "L_upperarm", "L_forearm", "L_hand" };
				const auto findBone = reinterpret_cast< FindBoneFn >( 0x00757500 );
				for( unsigned i = 0; i < 3; ++i )
				{
					s_armBones[ i ] = findBone( names, kArm[ i ] );
				}
				PSU_LOG( "HandsTest: model %p pose %p arm bones %d %d %d\n", model, s_playerPose, s_armBones[ 0 ], s_armBones[ 1 ],
					s_armBones[ 2 ] );
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				s_playerPose = nullptr;
			}
		}

		void ArmTest( float *out, const uint8_t *records )
		{
			static DWORD s_lastLog = 0;
			static unsigned s_lifts = 0;
			// Record: +0 flags, +4 palette slot, +6 parent, +8 first child, +0xA next sibling,
			// +0x30 inverse bind. The player skeleton has 28 bones.
			constexpr int count = 28;
			__try
			{
				if( !Readable( records, count * 0x90 ) )
				{
					return;
				}
				s_playerRecords = records;
				// Lift the left arm 30 cm (model space Y) to see whether the skin follows.
				int slots[ 3 ]{ -1, -1, -1 };
				for( unsigned i = 0; i < 3; ++i )
				{
					const int bone = s_armBones[ i ];
					if( bone >= 0 && bone < count )
					{
						slots[ i ] = *reinterpret_cast< const short * >( records + bone * 0x90 + 4 );
						if( slots[ i ] >= 0 && slots[ i ] < count )
						{
							out[ slots[ i ] * 16 + 13 ] += 3.0f;
						}
					}
				}
				++s_lifts;
				if( GetTickCount() - s_lastLog >= 2000 )
				{
					s_lastLog = GetTickCount();
					PSU_LOG( "HandsTest: lifted slots %d %d %d, %u lifts (in player draw %d)\n", slots[ 0 ], slots[ 1 ], slots[ 2 ],
						s_lifts, s_drawingLocalPlayer ? 1 : 0 );
				}
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				PSU_LOG( "HandsTest: exception\n" );
				s_playerPose = nullptr;
			}
		}

		// Loaded character (TDivModel): bone-indexed model-space matrices from 0x0074B760.
		// Lift one bone and everything under it (parent at record +6) by 30 cm.
		constexpr int kLiftBone = 15;

		void BonePoseTest( float *out, const uint8_t *skeleton )
		{
			static DWORD s_lastLog = 0;
			static unsigned s_lifts = 0;
			static bool s_dumped = false;
			__try
			{
				const int count = *reinterpret_cast< const int * >( skeleton + 0x28 );
				const uint8_t *records = *reinterpret_cast< const uint8_t * const * >( skeleton + 0x30 );
				if( count <= 0 || count > 128 || !Readable( records, count * 0x90 ) )
				{
					return;
				}
				if( !s_dumped )
				{
					s_dumped = true;
					PSU_LOG( "HandsDiv: skeleton %p, %d bones\n", skeleton, count );
					const DWORD *sk = reinterpret_cast< const DWORD * >( skeleton );
					for( unsigned n = 0; n < 0x40 / 4; ++n )
					{
						void *names = reinterpret_cast< void * >( sk[ n ] );
						int hand = -2;
						if( Readable( names, 0x0C ) )
						{
							__try
							{
								hand = reinterpret_cast< FindBoneFn >( 0x00757500 )( names, "L_hand" );
							}
							__except( EXCEPTION_EXECUTE_HANDLER )
							{
								hand = -3;
							}
						}
						PSU_LOG( "HandsDiv:   skeleton+0x%02X %08lX L_hand lookup %d\n", n * 4, sk[ n ], hand );
					}
					for( int b = 0; b < count; ++b )
					{
						const uint8_t *rec = records + b * 0x90;
						const short *links = reinterpret_cast< const short * >( rec + 4 );
						const float *m = out + b * 16;
						PSU_LOG( "HandsDiv:   #%d flags %08lX shorts %d %d %d %d pos (%.2f %.2f %.2f)\n", b,
							*reinterpret_cast< const DWORD * >( rec ), links[ 0 ], links[ 1 ], links[ 2 ], links[ 3 ], m[ 12 ], m[ 13 ],
							m[ 14 ] );
					}
				}
				if( kLiftBone >= count )
				{
					return;
				}
				for( int b = 0; b < count; ++b )
				{
					// Is b kLiftBone or below it?
					int walk = b;
					for( int guard = 0; walk >= 0 && walk != kLiftBone && guard < count; ++guard )
					{
						walk = *reinterpret_cast< const short * >( records + walk * 0x90 + 6 );
					}
					if( walk == kLiftBone )
					{
						out[ b * 16 + 13 ] += 3.0f;
					}
				}
				++s_lifts;
				if( GetTickCount() - s_lastLog >= 2000 )
				{
					s_lastLog = GetTickCount();
					PSU_LOG( "HandsDiv: lifted bone %d subtree, %u lifts (in player draw %d)\n", kLiftBone, s_lifts,
						s_drawingLocalPlayer ? 1 : 0 );
				}
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				PSU_LOG( "HandsDiv: exception\n" );
				s_divModel = nullptr;
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
#ifdef _DEBUG
			NotePlayerModel( object );
#endif
			s_drawingLocalPlayer = true;
			original( object );
			s_drawingLocalPlayer = false;
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

		void OnBonePose( float *out, const uint8_t *skeleton )
		{
#ifdef _DEBUG
			__try
			{
				if( out && skeleton && s_divModel && out == *reinterpret_cast< float * const * >( s_divModel + 0x64 ) )
				{
					BonePoseTest( out, skeleton );
				}
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				s_divModel = nullptr;
			}
#else
			( void )out;
			( void )skeleton;
#endif
		}

		void OnPoseBuilt( float *out, const uint8_t *records )
		{
#ifdef _DEBUG
			if( out && out == s_playerPose && s_armBones[ 2 ] >= 0 && records )
			{
				ArmTest( out, records );
			}
#else
			( void )out;
			( void )records;
#endif
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
