#pragma once

#include <cstdint>

#include <wav64.h>

namespace madeline_cube {

enum class AudioEventId : uint8_t {
    PlayerDash,
    ForsakenCityTheme,
};

class AudioService {
public:
    bool Init(const char* events_path);
    void Shutdown();
    bool PlaySfx(AudioEventId event);
    bool PlayMusic(AudioEventId event);
    void StopMusic();
    uint32_t ChannelExhaustions() const { return channel_exhaustions_; }

private:
    struct Event {
        char id[48] = {};
        char rom_path[96] = {};
        bool music = false;
        bool loop = false;
        float volume = 1.0f;
        wav64_t* wav = nullptr;
    };

    static constexpr int kMaxEvents = 16;
    static constexpr int kSfxChannel = 1;
    static constexpr int kMusicChannel = 0;
    Event events_[kMaxEvents] = {};
    int count_ = 0;
    bool mixer_ready_ = false;
    uint32_t channel_exhaustions_ = 0;

    Event* Find(AudioEventId event);
    bool LoadEvents(const char* events_path);
    bool Play(Event* event, int channel);
};

}  // namespace madeline_cube
