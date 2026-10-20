#pragma once

#include <Windows.h>

#include <filesystem>
#include <string>

// Where the speech helper and its models live (shared by the game DLL and the configurator,
// which both run from the game folder): <game>\PSUExSpeech\PSUExSpeech.exe and
// <game>\PSUExSpeech\models\<size>-streaming-en\.
namespace PSUVR::VRSpeechFiles
{
	inline std::filesystem::path SpeechDir()
	{
		wchar_t self[ MAX_PATH ]{};
		GetModuleFileNameW( nullptr, self, MAX_PATH );
		return std::filesystem::path( self ).parent_path() / L"PSUExSpeech";
	}

	inline std::filesystem::path HelperExe()
	{
		return SpeechDir() / L"PSUExSpeech.exe";
	}

	inline std::filesystem::path ModelDir( const std::string &size )
	{
		return SpeechDir() / L"models" / ( size + "-streaming-en" );
	}

	inline bool HelperInstalled()
	{
		std::error_code ec;
		return std::filesystem::exists( HelperExe(), ec );
	}

	// The last model file the helper writes; the download renames each file into place when complete.
	inline bool ModelInstalled( const std::string &size )
	{
		std::error_code ec;
		return std::filesystem::exists( ModelDir( size ) / L"decoder_kv.ort", ec ) &&
			std::filesystem::exists( ModelDir( size ) / L"tokenizer.bin", ec ) &&
			std::filesystem::exists( ModelDir( size ) / L"encoder.ort", ec );
	}

	// Starts the helper's download mode in its own console window (progress shown there).
	inline bool LaunchDownload( const std::string &size )
	{
		std::wstring command = L"\"" + HelperExe().wstring() + L"\" --download " + std::wstring( size.begin(), size.end() );
		STARTUPINFOW startup{ sizeof( startup ) };
		PROCESS_INFORMATION process{};
		if( !CreateProcessW( nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE, nullptr,
			SpeechDir().c_str(), &startup, &process ) )
		{
			return false;
		}
		CloseHandle( process.hThread );
		CloseHandle( process.hProcess );
		return true;
	}
}
