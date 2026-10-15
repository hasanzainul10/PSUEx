#include "VRProbe.h"

#include "../Misc/Log.h"
#include "../PSUMemory/Memory.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace PSUVR
{
#ifdef _DEBUG
	namespace
	{
		struct Region
		{
			const char *name;
			uintptr_t address;
			size_t size;
			std::vector< uint8_t > last;
			bool valid = false;
		};

		// Input state: pad slots (0x00A8DCB0 + i*0x84), DirectInput button -> logical bit
		// maps (0x00A8DE00 + pad*0x70), the input manager's player 0 block (0x00B9CC98,
		// 0x1F8 bytes; +4 is the 0x75-slot table copied from 0x0089D830) and its globals.
		Region s_watch[] = {
			// Per-pad logical state written by the poll at 0x00786C90 (0x24 bytes per pad):
			// +0 held bits, +4 ~held, +8 pressed, +0xC released, +0x10..+0x1C sticks (float).
			{ "padState", 0x00A8DDB8, 0x48 },
			{ "padSlots", 0x00A8DCB0, 0x108 },
			{ "padMaps", 0x00A8DE00, 0xE0 },
			{ "input0", 0x00B9CC98, 0x1F8 },
			{ "inputGlobals", 0x00B9CC78, 0x20 },
		};
		bool s_watching = false;
		unsigned s_watchLines = 0;
		constexpr unsigned kMaxWatchLines = 600;

		std::vector< uint8_t > s_playerSnapshot;
		uintptr_t s_playerSnapshotAddress = 0;
		constexpr size_t kPlayerBytes = 0x1000;

		bool s_prevF11 = false;
		bool s_prevF12 = false;

		bool Copy( uintptr_t address, size_t size, std::vector< uint8_t > &out )
		{
			out.resize( size );
			if( !PSUMemory::IsValidPointer( reinterpret_cast< const void * >( address ) ) ||
				!PSUMemory::IsValidPointer( reinterpret_cast< const void * >( address + size - 1 ) ) )
			{
				return false;
			}
			__try
			{
				std::memcpy( out.data(), reinterpret_cast< const void * >( address ), size );
			}
			__except( EXCEPTION_EXECUTE_HANDLER )
			{
				return false;
			}
			return true;
		}

		uint32_t Dword( const std::vector< uint8_t > &bytes, size_t offset )
		{
			uint32_t value = 0;
			std::memcpy( &value, bytes.data() + offset, sizeof( value ) );
			return value;
		}

		float AsFloat( uint32_t value )
		{
			float f = 0.0f;
			std::memcpy( &f, &value, sizeof( f ) );
			return f;
		}

		bool KeyEdge( int key, bool &previous )
		{
			const bool ctrl = ( GetAsyncKeyState( VK_CONTROL ) & 0x8000 ) != 0;
			const bool down = ctrl && ( GetAsyncKeyState( key ) & 0x8000 ) != 0;
			const bool edge = down && !previous;
			previous = down;
			return edge;
		}

		uintptr_t LocalPlayer()
		{
			const uintptr_t table = 0x009CA8AC;
			if( !PSUMemory::IsValidPointer( reinterpret_cast< const void * >( table ) ) )
			{
				return 0;
			}
			return *reinterpret_cast< const uintptr_t * >( table );
		}

		void WatchFrame()
		{
			const DWORD tick = GetTickCount();
			for( Region &region : s_watch )
			{
				std::vector< uint8_t > now;
				if( !Copy( region.address, region.size, now ) )
				{
					region.valid = false;
					continue;
				}
				if( region.valid )
				{
					for( size_t offset = 0; offset + 4 <= region.size; offset += 4 )
					{
						const uint32_t before = Dword( region.last, offset );
						const uint32_t after = Dword( now, offset );
						if( before != after && s_watchLines < kMaxWatchLines )
						{
							++s_watchLines;
							PSU_LOG( "Probe %lu %s+0x%03X (0x%08X): %08X -> %08X\n", tick, region.name,
								static_cast< unsigned >( offset ), static_cast< unsigned >( region.address + offset ), before, after );
						}
					}
				}
				region.last = std::move( now );
				region.valid = true;
			}
			if( s_watchLines == kMaxWatchLines )
			{
				++s_watchLines;
				PSU_LOG( "Probe: watch line limit reached (Ctrl+F11 twice to restart)\n" );
			}
		}

		void PlayerSnapshot()
		{
			const uintptr_t player = LocalPlayer();
			if( !player )
			{
				PSU_LOG( "Probe: no local player\n" );
				return;
			}
			std::vector< uint8_t > now;
			if( !Copy( player, kPlayerBytes, now ) )
			{
				PSU_LOG( "Probe: player struct at 0x%08X not readable\n", static_cast< unsigned >( player ) );
				return;
			}
			if( s_playerSnapshotAddress == player && s_playerSnapshot.size() == now.size() )
			{
				unsigned changes = 0;
				PSU_LOG( "Probe: player 0x%08X changes since the last snapshot:\n", static_cast< unsigned >( player ) );
				for( size_t offset = 0; offset + 4 <= now.size(); offset += 4 )
				{
					const uint32_t before = Dword( s_playerSnapshot, offset );
					const uint32_t after = Dword( now, offset );
					if( before != after )
					{
						if( ++changes <= 300 )
						{
							PSU_LOG( "  +0x%03X: %08X -> %08X  (float %.3f -> %.3f)\n", static_cast< unsigned >( offset ),
								before, after, AsFloat( before ), AsFloat( after ) );
						}
					}
				}
				PSU_LOG( "Probe: %u changed dwords\n", changes );
			}
			else
			{
				PSU_LOG( "Probe: player snapshot taken at 0x%08X (press Ctrl+F12 again to diff)\n", static_cast< unsigned >( player ) );
			}
			s_playerSnapshot = std::move( now );
			s_playerSnapshotAddress = player;
		}
	}
#endif

	namespace VRProbe
	{
		void Update()
		{
#ifdef _DEBUG
			if( KeyEdge( VK_F11, s_prevF11 ) )
			{
				s_watching = !s_watching;
				s_watchLines = 0;
				for( Region &region : s_watch )
				{
					region.valid = false;
				}
				PSU_LOG( "Probe: input watch %s\n", s_watching ? "on" : "off" );
			}
			if( KeyEdge( VK_F12, s_prevF12 ) )
			{
				PlayerSnapshot();
			}
			if( s_watching )
			{
				WatchFrame();
			}
#endif
		}
	}
}
