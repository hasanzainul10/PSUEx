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
			// 0x00B9CC8C (inputGlobals+0x14) is an idle timer that changes every frame: left out.
			{ "inputGlobals", 0x00B9CC78, 0x14 },
		};
		bool s_watching = false;
		unsigned s_watchLines = 0;
		constexpr unsigned kMaxWatchLines = 3000;

		std::vector< uint8_t > s_playerSnapshot;
		uintptr_t s_playerSnapshotAddress = 0;
		constexpr size_t kPlayerBytes = 0x2000;

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

		// Logical pad bits (0x00870518) as names, for the padState held words.
		void BitNames( uint32_t bits, char *out, size_t size )
		{
			static const struct { uint32_t bit; const char *name; } kBits[] = {
				{ 0x1, "A" }, { 0x2, "B" }, { 0x4, "X" }, { 0x8, "Y" }, { 0x10, "L1" }, { 0x20, "R1" },
				{ 0x40, "SELECT" }, { 0x80, "START" }, { 0x100, "L2" }, { 0x200, "R2" }, { 0x400, "L3" },
				{ 0x800, "R3" }, { 0x10000000, "U" }, { 0x20000000, "D" }, { 0x40000000, "L" }, { 0x80000000, "R" } };
			out[ 0 ] = '\0';
			for( const auto &entry : kBits )
			{
				if( bits & entry.bit )
				{
					strcat_s( out, size, entry.name );
					strcat_s( out, size, " " );
				}
			}
			if( bits & ~0xF0000FFFu )
			{
				char extra[ 24 ];
				sprintf_s( extra, "other=%08X", bits & ~0xF0000FFFu );
				strcat_s( out, size, extra );
			}
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

		// Live player struct watch: offsets that change during the first kCalibrationFrames
		// (stand still) are muted, so what is left is what an action changes, frame by frame.
		std::vector< uint8_t > s_playerLast;
		std::vector< bool > s_playerMuted;
		uintptr_t s_playerWatchAddress = 0;
		unsigned s_calibrationFrames = 0;
		constexpr unsigned kCalibrationFrames = 120;

		void ResetPlayerWatch()
		{
			s_playerLast.clear();
			s_playerMuted.assign( kPlayerBytes / 4, false );
			s_playerWatchAddress = 0;
			s_calibrationFrames = 0;
		}

		bool AlwaysMuted( size_t offset )
		{
			// Rotation matrices (+0x40..+0xBF), position (+0x114..+0x11F), facing (+0x124, +0xD0C).
			return ( offset >= 0x40 && offset < 0xC0 ) || ( offset >= 0x114 && offset < 0x120 ) ||
				offset == 0x124 || offset == 0xD0C;
		}

		// player+0x87C switches to a new object on each attack press and back to an idle one
		// afterwards: log each new object's first 0x80 bytes (vtable first), then its changes.
		uintptr_t s_actionObject = 0;
		std::vector< uint8_t > s_actionLast;
		constexpr size_t kActionBytes = 0x200;

		void WatchActionObject( DWORD tick, uintptr_t object )
		{
			std::vector< uint8_t > now;
			if( !object || !Copy( object, kActionBytes, now ) )
			{
				s_actionObject = 0;
				return;
			}
			if( object != s_actionObject )
			{
				PSU_LOG( "Probe %lu action object 0x%08X -> 0x%08X:\n", tick, static_cast< unsigned >( s_actionObject ),
					static_cast< unsigned >( object ) );
				for( size_t row = 0; row < 0x80 && s_watchLines < kMaxWatchLines; row += 0x20 )
				{
					++s_watchLines;
					PSU_LOG( "  +0x%02X: %08X %08X %08X %08X %08X %08X %08X %08X\n", static_cast< unsigned >( row ),
						Dword( now, row ), Dword( now, row + 4 ), Dword( now, row + 8 ), Dword( now, row + 12 ),
						Dword( now, row + 16 ), Dword( now, row + 20 ), Dword( now, row + 24 ), Dword( now, row + 28 ) );
				}
				s_actionObject = object;
				s_actionLast = std::move( now );
				return;
			}
			for( size_t offset = 0; offset + 4 <= now.size(); offset += 4 )
			{
				const uint32_t before = Dword( s_actionLast, offset );
				const uint32_t after = Dword( now, offset );
				if( before != after && s_watchLines < kMaxWatchLines )
				{
					++s_watchLines;
					PSU_LOG( "Probe %lu action+0x%03X: %08X -> %08X (float %.3f)\n", tick,
						static_cast< unsigned >( offset ), before, after, AsFloat( after ) );
				}
			}
			s_actionLast = std::move( now );
		}

		void WatchPlayer( DWORD tick )
		{
			const uintptr_t player = LocalPlayer();
			std::vector< uint8_t > now;
			if( !player || !Copy( player, kPlayerBytes, now ) )
			{
				return;
			}
			if( player != s_playerWatchAddress || s_playerLast.size() != now.size() )
			{
				ResetPlayerWatch();
				s_playerWatchAddress = player;
				s_playerLast = std::move( now );
				return;
			}
			const bool calibrating = s_calibrationFrames < kCalibrationFrames;
			for( size_t offset = 0; offset + 4 <= now.size(); offset += 4 )
			{
				const uint32_t before = Dword( s_playerLast, offset );
				const uint32_t after = Dword( now, offset );
				if( before == after || AlwaysMuted( offset ) )
				{
					continue;
				}
				if( calibrating )
				{
					s_playerMuted[ offset / 4 ] = true;
				}
				else if( !s_playerMuted[ offset / 4 ] && s_watchLines < kMaxWatchLines )
				{
					++s_watchLines;
					PSU_LOG( "Probe %lu player+0x%03X: %08X -> %08X (float %.3f)\n", tick,
						static_cast< unsigned >( offset ), before, after, AsFloat( after ) );
				}
			}
			WatchActionObject( tick, Dword( now, 0x87C ) );
			if( calibrating && ++s_calibrationFrames == kCalibrationFrames )
			{
				unsigned muted = 0;
				for( bool m : s_playerMuted )
				{
					muted += m ? 1 : 0;
				}
				PSU_LOG( "Probe: player watch ready (%u noisy dwords muted) - do the actions now\n", muted );
			}
			s_playerLast = std::move( now );
		}

		// Camera task object (TCameraTaskPlay `this`, from the camera hook), watched like the
		// player struct: what changes while standing still is muted during calibration.
		uintptr_t s_cameraTask = 0;
		uintptr_t s_cameraWatchAddress = 0;
		std::vector< uint8_t > s_cameraLast;
		std::vector< bool > s_cameraMuted;
		constexpr size_t kCameraBytes = 0x400;

		void WatchCamera( DWORD tick )
		{
			std::vector< uint8_t > now;
			if( !s_cameraTask || !Copy( s_cameraTask, kCameraBytes, now ) )
			{
				return;
			}
			if( s_cameraTask != s_cameraWatchAddress || s_cameraLast.size() != now.size() || s_calibrationFrames == 0 )
			{
				s_cameraWatchAddress = s_cameraTask;
				s_cameraMuted.assign( kCameraBytes / 4, false );
				s_cameraLast = std::move( now );
				return;
			}
			const bool calibrating = s_calibrationFrames < kCalibrationFrames;
			for( size_t offset = 0; offset + 4 <= now.size(); offset += 4 )
			{
				const uint32_t before = Dword( s_cameraLast, offset );
				const uint32_t after = Dword( now, offset );
				if( before == after )
				{
					continue;
				}
				if( calibrating )
				{
					s_cameraMuted[ offset / 4 ] = true;
				}
				else if( !s_cameraMuted[ offset / 4 ] && s_watchLines < kMaxWatchLines )
				{
					++s_watchLines;
					PSU_LOG( "Probe %lu camera+0x%03X: %08X -> %08X (float %.3f)\n", tick,
						static_cast< unsigned >( offset ), before, after, AsFloat( after ) );
				}
			}
			s_cameraLast = std::move( now );
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
							char names[ 96 ] = "";
							// padState: the held word of each pad (+0, +0x24).
							if( region.address == 0x00A8DDB8 && offset % 0x24 == 0 )
							{
								BitNames( after, names, sizeof( names ) );
							}
							PSU_LOG( "Probe %lu %s+0x%03X (0x%08X): %08X -> %08X %s\n", tick, region.name,
								static_cast< unsigned >( offset ), static_cast< unsigned >( region.address + offset ), before, after, names );
						}
					}
				}
				region.last = std::move( now );
				region.valid = true;
			}
			WatchPlayer( tick );
			WatchCamera( tick );
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

		// Always on (Debug): weapon candidates logged when they change, and what each pad
		// press led to 200 ms later (hand, attack state, PA). Feeds the weapon / button maps.
		constexpr uint32_t kWeaponOffsets[] = { 0x1DC, 0x1F0, 0x1F4, 0x2D4, 0x2D8, 0x308, 0x30C, 0x854 };
		uint32_t s_weaponLast[ std::size( kWeaponOffsets ) ]{};
		uintptr_t s_weaponPlayer = 0;
		uint32_t s_heldLast = 0;
		uint32_t s_pressBits = 0;
		DWORD s_pressTick = 0;

		uint32_t PlayerDword( uintptr_t player, uint32_t offset )
		{
			return *reinterpret_cast< const uint32_t * >( player + offset );
		}

		void ContextWatch()
		{
			// Input context candidates (menus vs. field): the input manager globals (0x00B9CC78..,
			// without the idle timer at +0x14) and the scene mode (0x00B90EA8).
			{
				static uint32_t s_context[ 6 ]{};
				static bool s_contextValid = false;
				const uint32_t now[ 6 ] = {
					PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B9CC78 ), PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B9CC7C ),
					PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B9CC80 ), PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B9CC84 ),
					PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B9CC88 ), PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00B90EA8 ) };
				if( !s_contextValid || std::memcmp( now, s_context, sizeof( now ) ) != 0 )
				{
					PSU_LOG( "Probe context: CC78=%08X CC7C=%08X CC80=%08X CC84=%08X CC88=%08X mode=%u\n",
						now[ 0 ], now[ 1 ], now[ 2 ], now[ 3 ], now[ 4 ], now[ 5 ] );
					std::memcpy( s_context, now, sizeof( now ) );
					s_contextValid = true;
				}
			}

		}

		void WeaponAndPressWatch()
		{
			const uintptr_t player = LocalPlayer();
			if( !player || !PSUMemory::IsValidPointer( reinterpret_cast< const void * >( player ) ) ||
				!PSUMemory::IsValidPointer( reinterpret_cast< const void * >( player + 0xFFF ) ) )
			{
				s_weaponPlayer = 0;
				return;
			}
			char line[ 320 ];
			int length = 0;
			bool changed = false;
			for( size_t i = 0; i < std::size( kWeaponOffsets ); ++i )
			{
				const uint32_t value = PlayerDword( player, kWeaponOffsets[ i ] );
				changed |= value != s_weaponLast[ i ] || player != s_weaponPlayer;
				s_weaponLast[ i ] = value;
			}
			if( changed )
			{
				length = sprintf_s( line, "Probe weapon:" );
				for( size_t i = 0; i < std::size( kWeaponOffsets ); ++i )
				{
					length += sprintf_s( line + length, sizeof( line ) - length, " +0x%03X=%X",
						kWeaponOffsets[ i ], s_weaponLast[ i ] );
				}
				PSU_LOG( "%s\n", line );
				s_weaponPlayer = player;
			}

			// Pad 1 held bits: note new presses, report the player state 200 ms later.
			const uint32_t held = PSUMemory::ReadAbsoluteMemory< uint32_t >( 0x00A8DDB8 );
			const uint32_t pressed = held & ~s_heldLast;
			s_heldLast = held;
			if( pressed && !s_pressBits )
			{
				s_pressBits = pressed;
				s_pressTick = GetTickCount();
			}
			if( s_pressBits && GetTickCount() - s_pressTick >= 200 )
			{
				char names[ 96 ];
				BitNames( s_pressBits, names, sizeof( names ) );
				PSU_LOG( "Probe press %s: state=%08X step=%X pa=%X/%X/%X hit0C=%08X hit10=%08X +14=%08X +18=%08X +1C=%08X +20=%08X facing=%04X\n",
					names, PlayerDword( player, 0x710 ), PlayerDword( player, 0x1E4 ), PlayerDword( player, 0x620 ),
					PlayerDword( player, 0x624 ), PlayerDword( player, 0x628 ), PlayerDword( player, 0x00C ),
					PlayerDword( player, 0x010 ), PlayerDword( player, 0x014 ), PlayerDword( player, 0x018 ),
					PlayerDword( player, 0x01C ), PlayerDword( player, 0x020 ), PlayerDword( player, 0x124 ) & 0xFFFF );
				s_pressBits = 0;
			}
		}
	}
#endif

	namespace VRProbe
	{
		void NoteCameraTask( const void *task )
		{
#ifdef _DEBUG
			s_cameraTask = reinterpret_cast< uintptr_t >( task );
#else
			( void )task;
#endif
		}

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
				ResetPlayerWatch();
				PSU_LOG( "Probe: input watch %s%s\n", s_watching ? "on" : "off",
					s_watching ? " (stand still ~2 s while the player watch calibrates)" : "" );
			}
			if( KeyEdge( VK_F12, s_prevF12 ) )
			{
				PlayerSnapshot();
			}
			if( s_watching )
			{
				WatchFrame();
			}
			ContextWatch();
			WeaponAndPressWatch();
#endif
		}
	}
}
