#ifndef __AUDIO_SFML_
#define __AUDIO_SFML_

#include "cfg.h"

class Audio_Stub
{
public:
	virtual size_t add_sound(const char* filename)  { return 0; }
	virtual void   play_sound(size_t ndx)  {}
	virtual bool   play_music(const char* filename) { return false; }
	virtual void   toggle_music()  {}
};

#ifdef AUDIO_ENABLE // If disabled, only the stub class will be available!

#include <SFML/Audio/SoundBuffer.hpp>
#include <SFML/Audio/Music.hpp>

#include <vector>

class Audio_SFML : public Audio_Stub
{
	struct SndBuf_NoCopy_Wrapper_thanks_std_vector : public sf::SoundBuffer {
		SndBuf_NoCopy_Wrapper_thanks_std_vector(int) {}
		SndBuf_NoCopy_Wrapper_thanks_std_vector() {}
		SndBuf_NoCopy_Wrapper_thanks_std_vector(const SndBuf_NoCopy_Wrapper_thanks_std_vector&) { /* cerr << "SFML SndBuf wrapper WAS COPIED!\n"; */ }
	};
	std::vector<SndBuf_NoCopy_Wrapper_thanks_std_vector> sounds;

public:
	size_t add_sound(const char* filename) override;
	void play_sound(size_t ndx) override;
	bool play_music(const char* filename) override;
	void toggle_music() override;

	Audio_SFML()
	{
		// Add an empty element so if add() returns 0 for errors it could still
		// be used as an index, pointing to a safe & silent spot.
		sounds.resize(1); //! Remember the implicit copy ctor call here (no matter what)! :-o (-> also: add_sound!)
	}
private:
//!! If a default-constructed item works as expected, then no need for this:
//!!	sf::SoundBuffer default_dummy_no_sound;

	sf::Music _music; //!! only this one player yet!
};

#endif // AUDIO_ENABLED

#endif // __AUDIO_SFML_
