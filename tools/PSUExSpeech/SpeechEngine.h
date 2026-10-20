#pragma once

#include <cstddef>
#include <memory>
#include <string>

// A speech recognizer the helper can drive. One utterance at a time: Begin, AddAudio while the
// button is held, End for the text. Moonshine is the first engine; an online API can be added
// as another implementation without touching the protocol or the game side.
class ISpeechEngine
{
public:
	virtual ~ISpeechEngine() = default;

	// Loads the model. Returns false with a message on failure.
	virtual bool Load( const std::string &modelDir, const std::string &size, std::string &error ) = 0;
	// Words to bias towards (technique names), comma separated. Empty = none.
	virtual void SetKeyterms( const std::string &keyterms ) = 0;
	virtual void Begin() = 0;
	// Mono float PCM, -1..1. Called from the capture thread.
	virtual void AddAudio( const float *samples, size_t count, int sampleRate ) = 0;
	// Text so far while speaking (may be empty). False when nothing changed.
	virtual bool Partial( std::string &text ) = 0;
	// Ends the utterance and returns its text (empty when nothing was understood).
	virtual std::string End() = 0;
	virtual void Cancel() = 0;
	virtual const char *Name() const = 0;
};

std::unique_ptr< ISpeechEngine > CreateMoonshineEngine();
