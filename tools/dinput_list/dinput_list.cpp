/*
 * Lists attached DirectInput game controllers in enumeration order.
 * PSU binds [gamepad1] to the first one.
 */
#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>
#include <cstdio>

#pragma comment( lib, "dinput8.lib" )
#pragma comment( lib, "dxguid.lib" )

static BOOL CALLBACK
OnDevice( LPCDIDEVICEINSTANCEA instance, LPVOID context )
{
	int &index = *static_cast< int * >( context );
	printf( "%d: %s  [product: %s]  type=0x%08lX\n", ++index,
		instance->tszInstanceName, instance->tszProductName, instance->dwDevType );
	return DIENUM_CONTINUE;
}

int
main()
{
	IDirectInput8A *input = nullptr;
	if( FAILED( DirectInput8Create( GetModuleHandleA( nullptr ), DIRECTINPUT_VERSION, IID_IDirectInput8A,
		reinterpret_cast< void ** >( &input ), nullptr ) ) )
	{
		printf( "DirectInput8Create failed\n" );
		return 1;
	}
	int index = 0;
	input->EnumDevices( DI8DEVCLASS_GAMECTRL, OnDevice, &index, DIEDFL_ATTACHEDONLY );
	if( index == 0 )
	{
		printf( "No game controllers\n" );
	}
	input->Release();
	return 0;
}
