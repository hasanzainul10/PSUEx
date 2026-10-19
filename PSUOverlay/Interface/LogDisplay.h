#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "IFWinCtrl.h"

// Chat log window: follows the game's own chat log file (LOG\ChatLog_YYYYMMDD.txt, written
// when [system] CHATLOG = 1). Shown / hidden from the menu bar or the VR left Triangle.
class LogDisplay : public IFWinCtrl
{
private:
	struct Entry
	{
		std::string time;
		std::string channel;  // Public, Party, ...
		std::string name;
		std::string message;
	};

	std::vector< Entry > m_entries;
	std::wstring m_file;        // the chat log being followed
	uint64_t m_offset = 0;      // bytes of it already read
	std::string m_pending;      // a partly written entry
	unsigned long m_lastPoll = 0;
	bool m_scrollToEnd = false;

	void Poll();
	void Parse( const std::string &text );

public:
	LogDisplay( const LogDisplay & ) = delete;
	LogDisplay &operator=( const LogDisplay & ) = delete;

	LogDisplay();
	~LogDisplay() = default;

	static std::shared_ptr< LogDisplay > Create()
	{
		return std::make_shared< LogDisplay >();
	}

	void Render() override;
};
