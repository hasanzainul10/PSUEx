/*
 * Win32 dinput8.dll proxy for Clementine PSUC.exe.
 * Forwards DirectInput8Create to SysWOW64\dinput8.dll and LoadLibrary's PSUEx.
 * With VR on, also keeps the gamepad (and optionally the keyboard) working while the
 * game window is not focused (psuex_vr.json: backgroundInput / backgroundKeyboard).
 */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dinput.h>

#include <cstdio>
#include <cstring>

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

// --- Background input -------------------------------------------------------------

// Minimal reader for top-level booleans in psuex_vr.json (no JSON library here).
static bool
ReadJsonBool( const char *text, const char *key, bool fallback )
{
	char quoted[ 64 ];
	sprintf_s( quoted, "\"%s\"", key );
	const char *at = strstr( text, quoted );
	if( !at )
	{
		return fallback;
	}
	at = strchr( at + strlen( quoted ), ':' );
	if( !at )
	{
		return fallback;
	}
	++at;
	while( *at == ' ' || *at == '\t' || *at == '\r' || *at == '\n' )
	{
		++at;
	}
	if( strncmp( at, "true", 4 ) == 0 )
	{
		return true;
	}
	if( strncmp( at, "false", 5 ) == 0 )
	{
		return false;
	}
	return fallback;
}

static bool g_backgroundPad = false;
static bool g_backgroundKeyboard = false;

static void
LoadBackgroundSettings()
{
	char path[ MAX_PATH ]{};
	GetModuleFileNameA( nullptr, path, MAX_PATH );
	if( char *slash = strrchr( path, '\\' ) )
	{
		slash[ 1 ] = '\0';
	}
	strcat_s( path, "psuex_vr.json" );
	FILE *f = nullptr;
	if( fopen_s( &f, path, "rb" ) != 0 || !f )
	{
		return;
	}
	static char text[ 64 * 1024 ];
	const size_t size = fread( text, 1, sizeof( text ) - 1, f );
	fclose( f );
	text[ size ] = '\0';
	// Default on when VR is enabled: in the headset the game window is rarely focused.
	const bool vr = ReadJsonBool( text, "enabled", false );
	g_backgroundPad = ReadJsonBool( text, "backgroundInput", vr );
	g_backgroundKeyboard = g_backgroundPad && ReadJsonBool( text, "backgroundKeyboard", false );
	char msg[ 160 ];
	sprintf_s( msg, "Background input: gamepad=%d keyboard=%d (vr=%d)", g_backgroundPad ? 1 : 0,
		g_backgroundKeyboard ? 1 : 0, vr ? 1 : 0 );
	Log( msg );
}

template< typename Device, typename Caps >
static bool
WantsBackground( Device *device )
{
	Caps caps{};
	caps.dwSize = sizeof( caps );
	if( FAILED( device->GetCapabilities( &caps ) ) )
	{
		return false;
	}
	switch( GET_DIDEVICE_TYPE( caps.dwDevType ) )
	{
		case DI8DEVTYPE_KEYBOARD: return g_backgroundKeyboard;
		case DI8DEVTYPE_MOUSE: return false;  // the mouse belongs to whatever window you click
		default: return g_backgroundPad;      // gamepads, joysticks, wheels
	}
}

using SetCoopA_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInputDevice8A *, HWND, DWORD );
using SetCoopW_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInputDevice8W *, HWND, DWORD );
using UnacquireA_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInputDevice8A * );
using UnacquireW_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInputDevice8W * );
static SetCoopA_t g_realSetCoopA = nullptr;
static SetCoopW_t g_realSetCoopW = nullptr;
static UnacquireA_t g_realUnacquireA = nullptr;
static UnacquireW_t g_realUnacquireW = nullptr;

static DWORD
BackgroundFlags( DWORD flags )
{
	// Exclusive + background isn't allowed for keyboards; non-exclusive is enough here.
	return ( flags & ~( DISCL_FOREGROUND | DISCL_EXCLUSIVE ) ) | DISCL_BACKGROUND | DISCL_NONEXCLUSIVE;
}

template< typename Device, typename Caps, typename Fn >
static HRESULT
SetCooperativeLevelBackground( Fn original, Device *self, HWND hwnd, DWORD flags )
{
	if( WantsBackground< Device, Caps >( self ) )
	{
		const DWORD changed = BackgroundFlags( flags );
		char msg[ 128 ];
		sprintf_s( msg, "SetCooperativeLevel 0x%lX -> 0x%lX (background)", flags, changed );
		Log( msg );
		flags = changed;
	}
	return original( self, hwnd, flags );
}

static HRESULT STDMETHODCALLTYPE
SetCooperativeLevelA_Hook( IDirectInputDevice8A *self, HWND hwnd, DWORD flags )
{
	return SetCooperativeLevelBackground< IDirectInputDevice8A, DIDEVCAPS >( g_realSetCoopA, self, hwnd, flags );
}

static HRESULT STDMETHODCALLTYPE
SetCooperativeLevelW_Hook( IDirectInputDevice8W *self, HWND hwnd, DWORD flags )
{
	return SetCooperativeLevelBackground< IDirectInputDevice8W, DIDEVCAPS >( g_realSetCoopW, self, hwnd, flags );
}

// Games often unacquire on focus loss; keep background devices acquired.
static HRESULT STDMETHODCALLTYPE
UnacquireA_Hook( IDirectInputDevice8A *self )
{
	return WantsBackground< IDirectInputDevice8A, DIDEVCAPS >( self ) ? DI_OK : g_realUnacquireA( self );
}

static HRESULT STDMETHODCALLTYPE
UnacquireW_Hook( IDirectInputDevice8W *self )
{
	return WantsBackground< IDirectInputDevice8W, DIDEVCAPS >( self ) ? DI_OK : g_realUnacquireW( self );
}

template< typename Fn >
static void
PatchSlot( void *object, int slot, Fn hook, Fn &original )
{
	void **vtable = *static_cast< void *** >( object );
	if( vtable[ slot ] == reinterpret_cast< void * >( hook ) )
	{
		return;
	}
	DWORD oldProtect = 0;
	if( VirtualProtect( &vtable[ slot ], sizeof( void * ), PAGE_EXECUTE_READWRITE, &oldProtect ) )
	{
		original = reinterpret_cast< Fn >( vtable[ slot ] );
		vtable[ slot ] = reinterpret_cast< void * >( hook );
		VirtualProtect( &vtable[ slot ], sizeof( void * ), oldProtect, &oldProtect );
	}
}

// IDirectInputDevice8 vtable: 8 = Unacquire, 13 = SetCooperativeLevel.
constexpr int kUnacquireSlot = 8;
constexpr int kSetCooperativeLevelSlot = 13;
// IDirectInput8 vtable: 3 = CreateDevice.
constexpr int kCreateDeviceSlot = 3;

using CreateDeviceA_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInput8A *, REFGUID, LPDIRECTINPUTDEVICE8A *, LPUNKNOWN );
using CreateDeviceW_t = HRESULT( STDMETHODCALLTYPE * )( IDirectInput8W *, REFGUID, LPDIRECTINPUTDEVICE8W *, LPUNKNOWN );
static CreateDeviceA_t g_realCreateDeviceA = nullptr;
static CreateDeviceW_t g_realCreateDeviceW = nullptr;

static HRESULT STDMETHODCALLTYPE
CreateDeviceA_Hook( IDirectInput8A *self, REFGUID guid, LPDIRECTINPUTDEVICE8A *device, LPUNKNOWN outer )
{
	const HRESULT hr = g_realCreateDeviceA( self, guid, device, outer );
	if( SUCCEEDED( hr ) && device && *device )
	{
		PatchSlot< SetCoopA_t >( *device, kSetCooperativeLevelSlot, &SetCooperativeLevelA_Hook, g_realSetCoopA );
		PatchSlot< UnacquireA_t >( *device, kUnacquireSlot, &UnacquireA_Hook, g_realUnacquireA );
	}
	return hr;
}

static HRESULT STDMETHODCALLTYPE
CreateDeviceW_Hook( IDirectInput8W *self, REFGUID guid, LPDIRECTINPUTDEVICE8W *device, LPUNKNOWN outer )
{
	const HRESULT hr = g_realCreateDeviceW( self, guid, device, outer );
	if( SUCCEEDED( hr ) && device && *device )
	{
		PatchSlot< SetCoopW_t >( *device, kSetCooperativeLevelSlot, &SetCooperativeLevelW_Hook, g_realSetCoopW );
		PatchSlot< UnacquireW_t >( *device, kUnacquireSlot, &UnacquireW_Hook, g_realUnacquireW );
	}
	return hr;
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
			if( g_backgroundPad || g_backgroundKeyboard )
			{
				PatchSlot< CreateDeviceA_t >( *ppvOut, kCreateDeviceSlot, &CreateDeviceA_Hook, g_realCreateDeviceA );
			}
		}
		else if( IsEqualIID( riidltf, IID_IDirectInput8W ) )
		{
			PatchEnumDevices< EnumDevicesW_t >( *ppvOut, &EnumDevicesW_Hook, g_realEnumW );
			if( g_backgroundPad || g_backgroundKeyboard )
			{
				PatchSlot< CreateDeviceW_t >( *ppvOut, kCreateDeviceSlot, &CreateDeviceW_Hook, g_realCreateDeviceW );
			}
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
		if( HostIsPSUC() )
		{
			LoadBackgroundSettings();
		}
		LoadPSUEx();
	}
	return TRUE;
}
