// audio_preview.cpp -- see audio_preview.h.

#include "audio_preview.h"

#include <SDL.h>

#include <cstdio>

#if SWOS_HAVE_SDL_MIXER
#    include <SDL_mixer.h>
#endif

namespace swos {

#if SWOS_HAVE_SDL_MIXER

AudioPreview::AudioPreview()
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        m_error = std::string("SDL audio: ") + SDL_GetError();
        return;
    }

    // MP3 for music and commentary, OGG as a courtesy; the packs ship mp3 and
    // wav, and wav needs no decoder flag.
    Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        m_error = std::string("Mix_OpenAudio: ") + Mix_GetError();
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }
    m_available = true;
}

AudioPreview::~AudioPreview()
{
    if (!m_available) return;
    stop();
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool AudioPreview::play(const std::string &path)
{
    if (!m_available) return false;
    stop();

    Mix_Music *music = Mix_LoadMUS(path.c_str());
    if (!music) {
        m_error = std::string("Cannot play ") + path + ": " + Mix_GetError();
        return false;
    }
    if (Mix_PlayMusic(music, 1) != 0) {
        m_error = std::string("Mix_PlayMusic: ") + Mix_GetError();
        Mix_FreeMusic(music);
        return false;
    }

    m_music = music;
    m_current = path;
    m_error.clear();
    return true;
}

void AudioPreview::stop()
{
    if (!m_available) return;
    if (Mix_PlayingMusic()) Mix_HaltMusic();
    if (m_music) {
        Mix_FreeMusic(static_cast<Mix_Music *>(m_music));
        m_music = nullptr;
    }
    m_current.clear();
}

bool AudioPreview::playing() const
{
    return m_available && Mix_PlayingMusic() != 0;
}

#else // no SDL2_mixer

AudioPreview::AudioPreview()
{
    m_error = "Built without SDL2_mixer; audio preview is unavailable.";
}
AudioPreview::~AudioPreview() = default;
bool AudioPreview::play(const std::string &) { return false; }
void AudioPreview::stop() { }
bool AudioPreview::playing() const { return false; }

#endif

} // namespace swos
