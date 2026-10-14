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

static void
LoadPSUEx()
{
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

extern "C" HRESULT WINAPI
DirectInput8Create( HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID *ppvOut, LPUNKNOWN punkOuter )
{
	if( !LoadRealDInput8() )
	{
		return DIERR_GENERIC;
	}
	return g_realCreate( hinst, dwVersion, riidltf, ppvOut, punkOuter );
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
