#include "audio_manager.hpp"

#include <3ds.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace asteria {
namespace {

constexpr int kMusicChannel = 0;
constexpr int kSfxChannel = 1;
constexpr float kMusicVolume = 0.35f;
constexpr float kSfxVolume = 0.90f;
constexpr std::uint64_t kMinSfxGapMs = 90;

struct ParsedWav {
    int channels = 0;
    int sampleRate = 0;
    std::size_t dataOffset = 0;
    std::size_t dataSize = 0;
};

std::size_t roundUp(std::size_t value, std::size_t alignment) {
    if (alignment == 0) return value;
    const std::size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

std::string cuePath(Cue cue) {
    switch (cue) {
        case Cue::Boot:
            return "romfs:/audio/boot.wav";
        case Cue::Theme:
            return "romfs:/audio/theme.wav";
        case Cue::Tab:
            return "romfs:/audio/tab.wav";
        case Cue::Capture:
            return "romfs:/audio/capture.wav";
        case Cue::Send:
        case Cue::Receive:
        case Cue::Error:
        case Cue::Claim:
        case Cue::Release:
        case Cue::Stop:
            return "romfs:/audio/press.wav";
        default:
            return "";
    }
}

bool fileExists(const std::string& path) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::fclose(file);
    return true;
}

std::uint16_t readLe16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

std::uint32_t readLe32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(
        bytes[offset] |
        (bytes[offset + 1] << 8) |
        (bytes[offset + 2] << 16) |
        (bytes[offset + 3] << 24));
}

bool parseWavBytes(const std::vector<std::uint8_t>& bytes, ParsedWav& parsed) {
    if (bytes.size() < 44) return false;
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;
    std::size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const char* chunkId = reinterpret_cast<const char*>(bytes.data() + offset);
        const std::uint32_t chunkSize = readLe32(bytes, offset + 4);
        const std::size_t chunkData = offset + 8;
        if (chunkData + chunkSize > bytes.size()) {
            return false;
        }

        if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            if (chunkSize < 16) return false;
            const std::uint16_t audioFormat = readLe16(bytes, chunkData + 0);
            parsed.channels = static_cast<int>(readLe16(bytes, chunkData + 2));
            parsed.sampleRate = static_cast<int>(readLe32(bytes, chunkData + 4));
            const std::uint16_t bitsPerSample = readLe16(bytes, chunkData + 14);
            if (audioFormat != 1 || bitsPerSample != 16) {
                return false;
            }
            if (parsed.channels < 1 || parsed.channels > 2 || parsed.sampleRate <= 0) {
                return false;
            }
            foundFmt = true;
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            parsed.dataOffset = chunkData;
            parsed.dataSize = static_cast<std::size_t>(chunkSize);
            foundData = true;
        }

        offset = chunkData + chunkSize + (chunkSize & 1u);
    }

    return foundFmt && foundData && parsed.dataSize > 0;
}

std::vector<std::uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.good()) return {};
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void setStereoMix(int channel, float volume) {
    std::array<float, 12> mix{};
    mix[0] = volume;
    mix[1] = volume;
    ndspChnSetMix(channel, mix.data());
}

std::size_t silenceTailBytes(Cue cue, const ParsedWav& parsed) {
    if (parsed.sampleRate <= 0 || parsed.channels <= 0) {
        return 0;
    }
    if (cue == Cue::Theme) {
        return 0;
    }

    const std::size_t bytesPerFrame = static_cast<std::size_t>(parsed.channels) * sizeof(std::int16_t);
    const std::size_t tailFrames = cue == Cue::Boot ? static_cast<std::size_t>(parsed.sampleRate / 12) : static_cast<std::size_t>(parsed.sampleRate / 18);
    return tailFrames * bytesPerFrame;
}

std::size_t guardPaddingBytes() {
    return 512;
}

}  // namespace

bool AudioManager::init(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) return true;

    romfsReady_ = R_SUCCEEDED(romfsInit());
    if (!romfsReady_) {
        enabled_ = false;
        return false;
    }

    if (R_FAILED(ndspInit())) {
        romfsExit();
        romfsReady_ = false;
        enabled_ = false;
        return false;
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspSetMasterVol(0.85f);

    ndspChnReset(kMusicChannel);
    ndspChnReset(kSfxChannel);
    ndspChnInitParams(kMusicChannel);
    ndspChnInitParams(kSfxChannel);

    loadCue(Cue::Boot, cuePath(Cue::Boot));
    loadCue(Cue::Theme, cuePath(Cue::Theme));
    loadCue(Cue::Tab, cuePath(Cue::Tab));
    loadCue(Cue::Send, cuePath(Cue::Send));
    loadCue(Cue::Receive, cuePath(Cue::Receive));
    loadCue(Cue::Error, cuePath(Cue::Error));
    loadCue(Cue::Claim, cuePath(Cue::Claim));
    loadCue(Cue::Release, cuePath(Cue::Release));
    loadCue(Cue::Capture, cuePath(Cue::Capture));
    loadCue(Cue::Stop, cuePath(Cue::Stop));
    return true;
}

void AudioManager::shutdown() {
    if (enabled_) {
        ndspChnWaveBufClear(kMusicChannel);
        ndspChnWaveBufClear(kSfxChannel);
        ndspChnReset(kMusicChannel);
        ndspChnReset(kSfxChannel);
    }

    for (auto& [cue, clip] : clips_) {
        (void)cue;
        if (clip.linearData != nullptr) {
            linearFree(clip.linearData);
            clip.linearData = nullptr;
        }
    }
    clips_.clear();
    bootSequencePending_ = false;
    themeLooping_ = false;

    if (enabled_) ndspExit();
    if (romfsReady_) {
        romfsExit();
        romfsReady_ = false;
    }
    enabled_ = false;
}

bool AudioManager::loadCue(Cue cue, const std::string& path) {
    auto& clip = clips_[cue];
    if (clip.ready) return true;
    if (!fileExists(path)) return false;

    const std::vector<std::uint8_t> bytes = readBinaryFile(path);
    ParsedWav parsed;
    if (!parseWavBytes(bytes, parsed)) {
        return false;
    }

    unloadCue(cue);

    const std::size_t tailBytes = silenceTailBytes(cue, parsed);
    const std::size_t playbackBytes = parsed.dataSize + tailBytes;
    const std::size_t allocationBytes = roundUp(playbackBytes + guardPaddingBytes(), 64);

    clip.linearData = static_cast<std::uint8_t*>(linearAlloc(allocationBytes));
    if (clip.linearData == nullptr) {
        return false;
    }

    std::memset(clip.linearData, 0, allocationBytes);
    std::memcpy(clip.linearData, bytes.data() + parsed.dataOffset, parsed.dataSize);

    if (tailBytes > 0 && parsed.dataSize > 0) {
        constexpr std::size_t kFadeFrames = 64;
        auto* pcm = reinterpret_cast<std::int16_t*>(clip.linearData);
        const std::size_t chans = static_cast<std::size_t>(parsed.channels);
        const std::size_t totalFrames = parsed.dataSize / (chans * sizeof(std::int16_t));
        const std::size_t fadeLen = std::min(totalFrames, kFadeFrames);
        const std::size_t fadeStart = totalFrames - fadeLen;
        for (std::size_t f = 0; f < fadeLen; ++f) {
            const float gain = 1.0f - static_cast<float>(f) / static_cast<float>(fadeLen);
            for (std::size_t ch = 0; ch < chans; ++ch) {
                const std::size_t idx = (fadeStart + f) * chans + ch;
                pcm[idx] = static_cast<std::int16_t>(static_cast<float>(pcm[idx]) * gain);
            }
        }
    }

    DSP_FlushDataCache(clip.linearData, static_cast<u32>(allocationBytes));

    clip.byteCount = playbackBytes;
    clip.sampleRate = parsed.sampleRate;
    clip.channels = parsed.channels;
    clip.waveBuf.data_pcm16 = reinterpret_cast<std::int16_t*>(clip.linearData);
    clip.waveBuf.nsamples = static_cast<u32>(parsed.dataSize / (static_cast<std::size_t>(parsed.channels) * sizeof(std::int16_t)));
    clip.waveBuf.offset = 0;
    clip.waveBuf.looping = false;
    clip.waveBuf.status = NDSP_WBUF_FREE;
    clip.waveBuf.sequence_id = 0;
    clip.waveBuf.next = nullptr;
    clip.ready = true;
    return true;
}

void AudioManager::unloadCue(Cue cue) {
    auto it = clips_.find(cue);
    if (it == clips_.end()) return;
    if (it->second.linearData != nullptr) {
        linearFree(it->second.linearData);
        it->second.linearData = nullptr;
    }
    it->second.byteCount = 0;
    it->second.waveBuf = {};
    it->second.ready = false;
}

bool AudioManager::configureChannel(int channel, const WavClip& clip, float volume) {
    if (!clip.ready) return false;
    ndspChnWaveBufClear(channel);
    ndspChnSetInterp(channel, NDSP_INTERP_LINEAR);
    ndspChnSetRate(channel, static_cast<float>(clip.sampleRate));
    ndspChnSetFormat(channel, clip.channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    setStereoMix(channel, volume);
    return true;
}

void AudioManager::resetWaveBuf(WavClip& clip, bool looping) {
    clip.waveBuf.data_pcm16 = reinterpret_cast<std::int16_t*>(clip.linearData);
    clip.waveBuf.nsamples = static_cast<u32>(clip.byteCount / (static_cast<std::size_t>(clip.channels) * sizeof(std::int16_t)));
    clip.waveBuf.offset = 0;
    clip.waveBuf.looping = looping;
    clip.waveBuf.status = NDSP_WBUF_FREE;
    clip.waveBuf.sequence_id = 0;
    clip.waveBuf.next = nullptr;
}

bool AudioManager::playOnChannel(int channel, Cue cue, bool looping, float volume) {
    auto it = clips_.find(cue);
    if (it == clips_.end() || !it->second.ready) {
        if (!loadCue(cue, cuePath(cue))) {
            return false;
        }
        it = clips_.find(cue);
        if (it == clips_.end() || !it->second.ready) {
            return false;
        }
    }

    auto& clip = it->second;
    if (!configureChannel(channel, clip, volume)) {
        return false;
    }

    resetWaveBuf(clip, looping);
    ndspChnWaveBufAdd(channel, &clip.waveBuf);
    return true;
}

void AudioManager::play(Cue cue) {
    if (!enabled_) return;

    if (cue == Cue::Boot) {
        bootSequencePending_ = false;
        themeLooping_ = false;
        if (playOnChannel(kMusicChannel, Cue::Boot, false, kMusicVolume)) {
            bootSequencePending_ = true;
        } else {
            startThemeLoop();
        }
        return;
    }

    if (cue == Cue::Theme) {
        startThemeLoop();
        return;
    }

    const std::uint64_t nowMs = osGetTime();
    if (lastSfxPlayMs_ != 0 && nowMs - lastSfxPlayMs_ < kMinSfxGapMs) {
        return;
    }
    lastSfxPlayMs_ = nowMs;
    playOnChannel(kSfxChannel, cue, false, kSfxVolume);
}

void AudioManager::update() {
    if (!enabled_) return;

    if (bootSequencePending_) {
        auto it = clips_.find(Cue::Boot);
        if (it == clips_.end() || !it->second.ready) {
            startThemeLoop();
            return;
        }

        const auto status = it->second.waveBuf.status;
        if (status == NDSP_WBUF_DONE || status == NDSP_WBUF_FREE) {
            startThemeLoop();
        }
        return;
    }

    if (themeLooping_) {
        auto it = clips_.find(Cue::Theme);
        if (it == clips_.end() || !it->second.ready) {
            themeLooping_ = false;
            return;
        }

        const auto status = it->second.waveBuf.status;
        if (status == NDSP_WBUF_DONE || status == NDSP_WBUF_FREE) {
            playOnChannel(kMusicChannel, Cue::Theme, false, kMusicVolume);
        }
    }
}

void AudioManager::startThemeLoop() {
    bootSequencePending_ = false;
    if (themeLooping_) return;
    if (playOnChannel(kMusicChannel, Cue::Theme, false, kMusicVolume)) {
        themeLooping_ = true;
    }
}

}  // namespace asteria
