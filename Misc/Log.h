#pragma once

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <share.h>

#include <Windows.h>

namespace PSULog
{
	inline FILE *g_file = nullptr;
	inline std::mutex g_mutex;

	inline void Init()
	{
		std::lock_guard lock( g_mutex );
		if( g_file )
		{
			return;
		}

		char path[ MAX_PATH ]{};
		GetModuleFileNameA( nullptr, path, MAX_PATH );
		if( char *slash = strrchr( path, '\\' ) )
		{
			slash[ 1 ] = '\0';
		}
		strcat_s( path, "psuex.log" );
		// Share read/write so tools can copy the log while the game runs.
		g_file = _fsopen( path, "a", _SH_DENYNO );
		if( g_file )
		{
			fprintf( g_file, "\n==== PSUEx attach ====\n" );
			fflush( g_file );
		}
	}

	inline void Shutdown()
	{
		std::lock_guard lock( g_mutex );
		if( g_file )
		{
			fprintf( g_file, "==== PSUEx detach ====\n" );
			fclose( g_file );
			g_file = nullptr;
		}
	}

	inline void Write( const char *fmt, ... )
	{
		char buffer[ 1024 ];
		va_list args;
		va_start( args, fmt );
		vsnprintf( buffer, sizeof( buffer ), fmt, args );
		va_end( args );

		printf( "%s", buffer );

		std::lock_guard lock( g_mutex );
		if( g_file )
		{
			fputs( buffer, g_file );
			fflush( g_file );
		}
	}
}

#define PSU_LOG( ... ) ::PSULog::Write( __VA_ARGS__ )
