#include "LogDisplay.h"
#include "../../PSUVR/VRBindings.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace
{
	constexpr size_t kMaxEntries = 300;
	constexpr unsigned long kPollMs = 1000;

	// The game writes the log in the system code page (Japanese client: Shift-JIS).
	std::string ToUtf8( const std::string &text )
	{
		if( text.empty() )
		{
			return text;
		}
		const int wide = MultiByteToWideChar( 932, 0, text.data(), static_cast< int >( text.size() ), nullptr, 0 );
		if( wide <= 0 )
		{
			return text;
		}
		std::wstring w( static_cast< size_t >( wide ), L'\0' );
		MultiByteToWideChar( 932, 0, text.data(), static_cast< int >( text.size() ), w.data(), wide );
		const int bytes = WideCharToMultiByte( CP_UTF8, 0, w.data(), wide, nullptr, 0, nullptr, nullptr );
		std::string out( static_cast< size_t >( bytes > 0 ? bytes : 0 ), '\0' );
		if( bytes > 0 )
		{
			WideCharToMultiByte( CP_UTF8, 0, w.data(), wide, out.data(), bytes, nullptr, nullptr );
		}
		return out;
	}

	// The newest LOG\ChatLog_*.txt next to the game.
	std::wstring NewestChatLog()
	{
		wchar_t exe[ MAX_PATH ]{};
		GetModuleFileNameW( nullptr, exe, MAX_PATH );
		const std::filesystem::path dir = std::filesystem::path( exe ).parent_path() / L"LOG";
		std::error_code error;
		std::filesystem::path best;
		std::filesystem::file_time_type bestTime{};
		for( const auto &item : std::filesystem::directory_iterator( dir, error ) )
		{
			const std::wstring name = item.path().filename().wstring();
			if( name.rfind( L"ChatLog_", 0 ) != 0 )
			{
				continue;
			}
			const auto time = item.last_write_time( error );
			if( best.empty() || time > bestTime )
			{
				best = item.path();
				bestTime = time;
			}
		}
		return best.wstring();
	}
}

LogDisplay::LogDisplay() : IFWinCtrl( "LogDisplay", false )
{
}

void LogDisplay::Poll()
{
	const unsigned long now = GetTickCount();
	if( m_lastPoll && now - m_lastPoll < kPollMs )
	{
		return;
	}
	m_lastPoll = now;

	const std::wstring newest = NewestChatLog();
	if( newest.empty() )
	{
		return;
	}
	if( newest != m_file )
	{
		// A new day's file (or the first look): start over from its beginning.
		m_file = newest;
		m_offset = 0;
		m_pending.clear();
	}
	std::ifstream in( std::filesystem::path( m_file ), std::ios::binary );
	if( !in )
	{
		return;
	}
	in.seekg( 0, std::ios::end );
	const uint64_t size = static_cast< uint64_t >( in.tellg() );
	if( size < m_offset )
	{
		m_offset = 0;  // rewritten
		m_pending.clear();
	}
	if( size == m_offset )
	{
		return;
	}
	in.seekg( static_cast< std::streamoff >( m_offset ) );
	std::string chunk( static_cast< size_t >( size - m_offset ), '\0' );
	in.read( chunk.data(), static_cast< std::streamsize >( chunk.size() ) );
	m_offset = size;
	Parse( chunk );
}

// Entries: "HH:MM:SS\t<Channel>\tName(id)", then the message line(s), then a blank line.
void LogDisplay::Parse( const std::string &text )
{
	m_pending += text;
	size_t start = 0;
	for( ;; )
	{
		// A complete entry ends with an empty line.
		size_t end = m_pending.find( "\r\n\r\n", start );
		size_t endLength = 4;
		if( end == std::string::npos )
		{
			end = m_pending.find( "\n\n", start );
			endLength = 2;
		}
		if( end == std::string::npos )
		{
			break;
		}
		std::string block = m_pending.substr( start, end - start );
		start = end + endLength;

		const size_t lineEnd = block.find( '\n' );
		std::string header = block.substr( 0, lineEnd );
		std::string message = lineEnd == std::string::npos ? std::string() : block.substr( lineEnd + 1 );
		auto trimCr = []( std::string &s )
		{
			s.erase( std::remove( s.begin(), s.end(), '\r' ), s.end() );
		};
		trimCr( header );
		trimCr( message );

		Entry entry;
		const size_t tab1 = header.find( '\t' );
		const size_t tab2 = tab1 == std::string::npos ? std::string::npos : header.find( '\t', tab1 + 1 );
		if( tab1 == std::string::npos || tab2 == std::string::npos )
		{
			entry.message = ToUtf8( header + ( message.empty() ? "" : " " + message ) );
		}
		else
		{
			entry.time = header.substr( 0, tab1 );
			entry.channel = header.substr( tab1 + 1, tab2 - tab1 - 1 );
			if( entry.channel.size() >= 2 && entry.channel.front() == '<' && entry.channel.back() == '>' )
			{
				entry.channel = entry.channel.substr( 1, entry.channel.size() - 2 );
			}
			std::string name = header.substr( tab2 + 1 );
			const size_t paren = name.rfind( '(' );
			if( paren != std::string::npos && paren > 0 )
			{
				name = name.substr( 0, paren );  // drop the guardian id
			}
			entry.name = ToUtf8( name );
			entry.message = ToUtf8( message );
		}
		m_entries.push_back( std::move( entry ) );
		m_scrollToEnd = true;
	}
	m_pending.erase( 0, start );
	if( m_entries.size() > kMaxEntries )
	{
		m_entries.erase( m_entries.begin(), m_entries.begin() + static_cast< long >( m_entries.size() - kMaxEntries ) );
	}
}

void LogDisplay::Render()
{
	if( PSUVR::VRBindings::ConsumeChatLogRequest() )
	{
		Toggle();
		if( m_bEnabled )
		{
			m_scrollToEnd = true;
		}
	}
	if( !m_bEnabled )
	{
		return;
	}
	Poll();

	ImGui::SetNextWindowSize( ImVec2( 720.0f, 360.0f ), ImGuiCond_FirstUseEver );
	ImGui::SetNextWindowPos( ImVec2( 40.0f, 400.0f ), ImGuiCond_FirstUseEver );
	if( ImGui::Begin( "Chat log", &m_bEnabled ) )
	{
		ImGui::BeginChild( "ChatScroll", ImVec2( 0, 0 ), true );
		if( m_entries.empty() )
		{
			ImGui::TextDisabled( m_file.empty() ? "No chat log yet (the game writes LOG\\ChatLog_<date>.txt when CHATLOG = 1)."
				: "No messages yet." );
		}
		for( const Entry &entry : m_entries )
		{
			const bool party = entry.channel == "Party";
			ImGui::TextDisabled( "%s", entry.time.c_str() );
			ImGui::SameLine();
			if( !entry.channel.empty() )
			{
				ImGui::TextColored( party ? ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ) : ImVec4( 0.8f, 0.8f, 0.8f, 1.0f ), "[%s]",
					entry.channel.c_str() );
				ImGui::SameLine();
			}
			if( !entry.name.empty() )
			{
				ImGui::TextColored( ImVec4( 1.0f, 0.9f, 0.3f, 1.0f ), "%s:", entry.name.c_str() );
				ImGui::SameLine();
			}
			ImGui::TextWrapped( "%s", entry.message.c_str() );
		}
		if( m_scrollToEnd )
		{
			ImGui::SetScrollHereY( 1.0f );
			m_scrollToEnd = false;
		}
		ImGui::EndChild();
	}
	ImGui::End();
}
