/*
 * Win32 dinput8.dll proxy for Clementine PSUC.exe.
 * Forwards DirectInput8Create to SysWOW64\dinput8.dll and LoadLibrary's PSUEx.
 */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dinput.h>

#include <cstdio>

using DirectInput8Create_t = HRESULT( WINAPI * )( HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN );

static HMODULE g_realDInput8 = nullptr;
static DirectInput8Create_t g_realCreate = nullptr;
static HMODULE g_psuex = nullptr;

static void
Log( const char *msg )
{
	char path[ MAX_PATH ]{};
	GetModuleFileNameA( nullptr, path, MAX_PATH );
	if( char *slash = strrchr( path, '\\' ) )
	{
		slash[ 1 ] = '\0';
	}
	strcat_s( path, "dinput8_proxy.log" );
	FILE *f = nullptr;
	if( fopen_s( &f, path, "a" ) == 0 && f )
	{
		fputs( msg, f );
		fputc( '\n', f );
		fclose( f );
	}
}

static bool
HostIsPSUC()
{
	char exe[ MAX_PATH ]{};
	GetModuleFileNameA( nullptr, exe, MAX_PATH );
	const char *base = strrchr( exe, '\\' );
	base = base ? base + 1 : exe;
	// online.exe / option.exe also load this folder's dinput8 — do not inject into them.
	return _stricmp( base, "PSUC.exe" ) == 0
		|| _stricmp( base, "PSUC_Uncompressed.exe" ) == 0;
}

static void
LoadPSUEx()
{
	if( !HostIsPSUC() )
	{
		char exe[ MAX_PATH ]{};
		GetModuleFileNameA( nullptr, exe, MAX_PATH );
		char msg[ 320 ];
		sprintf_s( msg, "Skip PSUEx (host is not PSUC): %s", exe );
		Log( msg );
		return;
	}

	char dir[ MAX_PATH ]{};
	GetModuleFileNameA( nullptr, dir, MAX_PATH );
	if( char *slash = strrchr( dir, '\\' ) )
	{
		slash[ 1 ] = '\0';
	}

	const char *candidates[] = { "PSUExD.dll", "PSUEx.dll" };
	for( const char *name : candidates )
	{
		char path[ MAX_PATH ]{};
		strcpy_s( path, dir );
		strcat_s( path, name );
		g_psuex = LoadLibraryA( path );
		if( g_psuex )
		{
			char msg[ 256 ];
			sprintf_s( msg, "Loaded %s", name );
			Log( msg );
			return;
		}
	}
	Log( "Failed to load PSUExD.dll / PSUEx.dll" );
}

static bool
LoadRealDInput8()
{
	if( g_realCreate )
	{
		return true;
	}

	char path[ MAX_PATH ]{};
	GetSystemDirectoryA( path, MAX_PATH );
	strcat_s( path, "\\dinput8.dll" );
	g_realDInput8 = LoadLibraryA( path );
	if( !g_realDInput8 )
	{
		Log( "Failed to load system dinput8.dll" );
		return false;
	}
	g_realCreate = reinterpret_cast< DirectInput8Create_t >(
		GetProcAddress( g_realDInput8, "DirectInput8Create" ) );
	if( !g_realCreate )
	{
		Log( "DirectInput8Create missing from system dinput8.dll" );
		return false;
	}
	return true;
}

// PSU binds [gamepad1] to the first enumerated controller; flight sticks and
// throttles often enumerate ahead of the actual gamepad, so list gamepads first.
static int
DevicePriority( DWORD devType )
{
	switch( GET_DIDEVICE_TYPE( devType ) )
	{
		case DI8DEVTYPE_GAMEPAD: return 0;
		case DI8DEVTYPE_JOYSTICK: return 1;
		default: return 2;
	}
}

template< typename Instance >
struct DeviceList
{
	Instance items[ 32 ];
	int count = 0;
};

template< typename Instance >
static BOOL CALLBACK
CollectDevice( const Instance *instance, LPVOID context )
{
	auto *list = static_cast< DeviceList< Instance > * >( context );
	if( list->count < 32 )
	{
		list->items[ list->count++ ] = *instance;
	}
	return DIENUM_CONTINUE;
}

template< typename Interface, typename Instance, typename Callback >
static HRESULT
EnumGamepadsFirst( HRESULT( STDMETHODCALLTYPE *original )( Interface *, DWORD, Callback, LPVOID, DWORD ),
	Interface *self, DWORD devType, Callback callback, LPVOID ref, DWORD flags )
{
	// Without this the game would never see the gamepad when it asks for joysticks only.
	const DWORD queryType = ( devType == DI8DEVTYPE_JOYSTICK ) ? DI8DEVCLASS_GAMECTRL : devType;

	DeviceList< Instance > list;
	const HRESULT hr = original( self, queryType, &CollectDevice< Instance >, &list, flags );
	if( FAILED( hr ) )
	{
		return hr;
	}

	char msg[ 160 ];
	sprintf_s( msg, "EnumDevices type=0x%lX (queried 0x%lX) flags=0x%lX -> %d devices, gamepads first",
		devType, queryType, flags, list.count );
	Log( msg );

	for( int priority = 0; priority <= 2; ++priority )
	{
		for( int i = 0; i < list.count; ++i )
		{
			if( DevicePriority( list.items[ i ].dwDevType ) != priority )
			{
				continue;
			}
			if( callback( &list.items[ i ], ref ) == DIENUM_STOP )
			{
				return hr;
			}
		}
	}
	return hr;
}

using EnumDevicesA_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInput8A *, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD );
using EnumDevicesW_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInput8W *, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD );
static EnumDevicesA_t g_realEnumA = nullptr;
static EnumDevicesW_t g_realEnumW = nullptr;

static HRESULT STDMETHODCALLTYPE
EnumDevicesA_Hook( IDirectInput8A *self, DWORD devType, LPDIENUMDEVICESCALLBACKA callback, LPVOID ref, DWORD flags )
{
	return EnumGamepadsFirst< IDirectInput8A, DIDEVICEINSTANCEA >( g_realEnumA, self, devType, callback, ref, flags );
}

static HRESULT STDMETHODCALLTYPE
EnumDevicesW_Hook( IDirectInput8W *self, DWORD devType, LPDIENUMDEVICESCALLBACKW callback, LPVOID ref, DWORD flags )
{
	return EnumGamepadsFirst< IDirectInput8W, DIDEVICEINSTANCEW >( g_realEnumW, self, devType, callback, ref, flags );
}

template< typename Fn >
static void
PatchEnumDevices( void *object, Fn hook, Fn &original )
{
	constexpr int kEnumDevicesSlot = 4;
	void **vtable = *static_cast< void *** >( object );
	if( vtable[ kEnumDevicesSlot ] == reinterpret_cast< void * >( hook ) )
	{
		return;
	}
	DWORD oldProtect = 0;
	if( VirtualProtect( &vtable[ kEnumDevicesSlot ], sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
	{
		original = reinterpret_cast< Fn >( vtable[ kEnumDevicesSlot ] );
		vtable[ kEnumDevicesSlot ] = reinterpret_cast< void * >( hook );
		VirtualProtect( &vtable[ kEnumDevicesSlot ], sizeof( void * ), oldProtect, &oldProtect );
	}
}

extern "C" HRESULT WINAPI
DirectInput8Create( HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter )
{
	if( !LoadRealDInput8() )
	{
		return DIERR_GENERIC;
	}
	const HRESULT hr = g_realCreate( hinst, dwVersion, riidltf, ppvOut, punkOuter );
	if( SUCCEEDED( hr ) && ppvOut && *ppvOut )
	{
		if( IsEqualIID( riidltf, IID_IDirectInput8A ) )
		{
			PatchEnumDevices< EnumDevicesA_t >( *ppvOut, &EnumDevicesA_Hook, g_realEnumA );
		}
		else if( IsEqualIID( riidltf, IID_IDirectInput8W ) )
		{
			PatchEnumDevices< EnumDevicesW_t >( *ppvOut, &EnumDevicesW_Hook, g_realEnumW );
		}
	}
	return hr;
}

BOOL APIENTRY
DllMain( HMODULE hModule, DWORD reason, LPVOID )
{
	if( reason == DLL_PROCESS_ATTACH )
	{
		DisableThreadLibraryCalls( hModule );
		Log( "dinput8 proxy attach" );
		LoadPSUEx();
	}
	return TRUE;
}
