#include "SpeechEngine.h"

#include "moonshine-c-api.h"

#include <mutex>
#include <vector>

namespace
{
	uint32_t ArchFor( const std::string &size )
	{
		if( size == "tiny" )
		{
			return MOONSHINE_MODEL_ARCH_TINY_STREAMING;
		}
		if( size == "medium" )
		{
			return MOONSHINE_MODEL_ARCH_MEDIUM_STREAMING;
		}
		return MOONSHINE_MODEL_ARCH_SMALL_STREAMING;
	}

	std::string Trim( const std::string &text )
	{
		const size_t first = text.find_first_not_of( " \t\r\n" );
		if( first == std::string::npos )
		{
			return {};
		}
		const size_t last = text.find_last_not_of( " \t\r\n" );
		return text.substr( first, last - first + 1 );
	}

	std::string Join( const transcript_t *transcript )
	{
		std::string out;
		if( !transcript )
		{
			return out;
		}
		for( uint64_t i = 0; i < transcript->line_count; ++i )
		{
			const char *line = transcript->lines[ i ].text;
			if( !line || !*line )
			{
				continue;
			}
			if( !out.empty() )
			{
				out += ' ';
			}
			out += line;
		}
		return Trim( out );
	}

	class MoonshineEngine final : public ISpeechEngine
	{
	public:
		~MoonshineEngine() override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			FreeStream();
			if( m_transcriber >= 0 )
			{
				moonshine_free_transcriber( m_transcriber );
			}
		}

		bool Load( const std::string &modelDir, const std::string &size, std::string &error ) override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			m_transcriber = moonshine_load_transcriber_from_files( modelDir.c_str(), ArchFor( size ), nullptr, 0,
				MOONSHINE_HEADER_VERSION );
			if( m_transcriber < 0 )
			{
				error = moonshine_error_to_string( m_transcriber );
				return false;
			}
			return true;
		}

		void SetKeyterms( const std::string &keyterms ) override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			if( m_transcriber >= 0 )
			{
				moonshine_transcriber_set_keyterms( m_transcriber, keyterms.c_str() );
			}
		}

		void Begin() override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			// A fresh stream per utterance: lines are never removed from a stream's transcript.
			FreeStream();
			if( m_transcriber < 0 )
			{
				return;
			}
			m_stream = moonshine_create_stream( m_transcriber, 0 );
			if( m_stream >= 0 && moonshine_start_stream( m_transcriber, m_stream ) != MOONSHINE_ERROR_NONE )
			{
				FreeStream();
			}
			m_partial.clear();
		}

		void AddAudio( const float *samples, size_t count, int sampleRate ) override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			if( m_stream >= 0 )
			{
				moonshine_transcribe_add_audio_to_stream( m_transcriber, m_stream, samples, count, sampleRate, 0 );
			}
		}

		bool Partial( std::string &text ) override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			if( m_stream < 0 )
			{
				return false;
			}
			transcript_t *transcript = nullptr;
			if( moonshine_transcribe_stream( m_transcriber, m_stream, 0, &transcript ) != MOONSHINE_ERROR_NONE )
			{
				return false;
			}
			std::string now = Join( transcript );
			if( now == m_partial )
			{
				return false;
			}
			m_partial = now;
			text = now;
			return true;
		}

		std::string End() override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			if( m_stream < 0 )
			{
				return {};
			}
			std::string text;
			if( moonshine_stop_stream( m_transcriber, m_stream ) == MOONSHINE_ERROR_NONE )
			{
				transcript_t *transcript = nullptr;
				if( moonshine_transcribe_stream( m_transcriber, m_stream, 0, &transcript ) == MOONSHINE_ERROR_NONE )
				{
					text = Join( transcript );
				}
			}
			FreeStream();
			return text;
		}

		void Cancel() override
		{
			std::lock_guard< std::mutex > lock( m_mutex );
			FreeStream();
		}

		const char *Name() const override
		{
			return "moonshine";
		}

	private:
		void FreeStream()
		{
			if( m_stream >= 0 )
			{
				moonshine_free_stream( m_transcriber, m_stream );
				m_stream = -1;
			}
		}

		std::mutex m_mutex;
		int32_t m_transcriber = -1;
		int32_t m_stream = -1;
		std::string m_partial;
	};
}

std::unique_ptr< ISpeechEngine > CreateMoonshineEngine()
{
	return std::make_unique< MoonshineEngine >();
}
