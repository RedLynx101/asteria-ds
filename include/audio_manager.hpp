#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include <3ds.h>

namespace asteria {

enum class Cue {
    Boot,
    Theme,
    Tab,
    Send,
    Receive,
    Error,
    Claim,
    Release,
    Capture,
    Stop,
};

class AudioManager {
  public:
    struct WavClip {
        std::uint8_t* linearData = nullptr;
        std::size_t byteCount = 0;
        ndspWaveBuf waveBuf{};
        int sampleRate = 16000;
        int channels = 1;
        bool ready = false;
    };

    bool init(bool enabled);
    void shutdown();
    void play(Cue cue);
    void update();
    bool enabled() const { return enabled_; }

  private:
    bool loadCue(Cue cue, const std::string& path);
    void unloadCue(Cue cue);
    bool configureChannel(int channel, const WavClip& clip, float volume);
    void resetWaveBuf(WavClip& clip, bool looping);
    bool playOnChannel(int channel, Cue cue, bool looping, float volume);
    void startThemeLoop();

    bool enabled_ = false;
    bool romfsReady_ = false;
    bool bootSequencePending_ = false;
    bool themeLooping_ = false;
    std::uint64_t lastSfxPlayMs_ = 0;
    std::map<Cue, WavClip> clips_;
};

}  // namespace asteria
