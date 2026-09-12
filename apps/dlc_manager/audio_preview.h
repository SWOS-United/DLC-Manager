// audio_preview.h -- play a DLC commentary / music / sound-effect sample.
//
// Replaces preview_audio_commentary_u.pas and preview_audio_fx_u.pas, which
// drove the Windows MCI device directly. SDL2_mixer covers the mp3 and wav the
// packs ship on all three platforms.
//
// Optional at build time: without SDL2_mixer the whole class degrades to
// available() == false and the UI hides its transport, so the manager still
// browses, downloads and installs.

#pragma once

#include <string>

namespace swos {

class AudioPreview {
public:
    AudioPreview();
    ~AudioPreview();
    AudioPreview(const AudioPreview &) = delete;
    AudioPreview &operator=(const AudioPreview &) = delete;

    // False when the build has no SDL2_mixer, or the audio device would not
    // open. Every other method is then a no-op.
    bool available() const { return m_available; }

    // Starts `path`, replacing anything already playing. Returns false and sets
    // last_error() if the file cannot be opened.
    bool play(const std::string &path);
    void stop();
    bool playing() const;

    const std::string &current_path() const { return m_current; }
    const std::string &last_error() const { return m_error; }

private:
    bool m_available = false;
    std::string m_current;
    std::string m_error;
    void *m_music = nullptr;   // Mix_Music*, kept opaque so the header stays clean
};

} // namespace swos
