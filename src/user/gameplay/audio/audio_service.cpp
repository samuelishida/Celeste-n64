#include "gameplay/audio/audio_service.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <libdragon.h>
#include <mixer.h>

namespace madeline_cube {

namespace {

bool CopyField(char* dst, size_t capacity, const char* src) {
    const size_t length = std::strlen(src);
    if (length == 0 || length >= capacity || std::strchr(src, '\t') ||
        std::strchr(src, '\n') || std::strchr(src, '\r')) return false;
    std::memcpy(dst, src, length + 1);
    return true;
}

const char* EventName(AudioEventId event) {
    switch (event) {
        case AudioEventId::PlayerDash: return "player_dash";
        case AudioEventId::ForsakenCityTheme: return "forsaken_city_theme";
    }
    return "";
}

}  // namespace

bool AudioService::Init(const char* events_path) {
    Shutdown();
    // Keep channel count bounded: one music channel plus one SFX channel for
    // first audio milestone, avoiding an unbounded mixer buffer reservation.
    audio_init(44100, AUDIO_DEFAULT_LATENCY);
    mixer_init(2);
    mixer_ready_ = true;
    if (!LoadEvents(events_path)) {
        debugf("[audio] malformed event manifest: %s\n", events_path);
        Shutdown();
        return false;
    }
    debugf("[audio] ready events=%d\n", count_);
    return true;
}

void AudioService::Shutdown() {
    StopMusic();
    for (int i = 0; i < count_; ++i) {
        if (events_[i].wav) {
            wav64_close(events_[i].wav);
            events_[i].wav = nullptr;
        }
        events_[i] = Event{};
    }
    count_ = 0;
    if (mixer_ready_) {
        mixer_close();
        audio_close();
        mixer_ready_ = false;
    }
}

bool AudioService::LoadEvents(const char* events_path) {
    FILE* f = std::fopen(events_path, "r");
    if (!f) return false;
    char line[256];
    int declared = 0;
    if (!std::fgets(line, sizeof(line), f) ||
        std::sscanf(line, "AUDIOEVENTS\t1\t%d", &declared) != 1 ||
        declared < 0 || declared > kMaxEvents) {
        std::fclose(f);
        return false;
    }
    count_ = 0;
    for (int row = 0; row < declared; ++row) {
        if (!std::fgets(line, sizeof(line), f)) { std::fclose(f); return false; }
        int length = static_cast<int>(std::strlen(line));
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = '\0';
        char* fields[5] = {};
        char* cursor = line;
        for (int field = 0; field < 5; ++field) {
            fields[field] = cursor;
            char* tab = std::strchr(cursor, '\t');
            if (field < 4) {
                if (!tab) { std::fclose(f); return false; }
                *tab = '\0';
                cursor = tab + 1;
            } else if (tab) {
                std::fclose(f);
                return false;
            }
        }
        Event& event = events_[count_];
        float volume = -1.0f;
        int loop = -1;
        if (!CopyField(event.id, sizeof(event.id), fields[0]) ||
            !CopyField(event.rom_path, sizeof(event.rom_path), fields[1]) ||
            std::strncmp(event.rom_path, "rom:/audio/", 11) != 0 ||
            (std::strcmp(fields[2], "sfx") != 0 && std::strcmp(fields[2], "music") != 0) ||
            std::sscanf(fields[3], "%f", &volume) != 1 || !std::isfinite(volume) ||
            volume < 0.0f || volume > 1.0f || std::sscanf(fields[4], "%d", &loop) != 1 ||
            (loop != 0 && loop != 1)) {
            std::fclose(f);
            return false;
        }
        event.music = std::strcmp(fields[2], "music") == 0;
        event.volume = volume;
        event.loop = loop != 0;
        for (int prior = 0; prior < count_; ++prior) {
            if (std::strcmp(events_[prior].id, event.id) == 0 ||
                std::strcmp(events_[prior].rom_path, event.rom_path) == 0) {
                std::fclose(f);
                return false;
            }
        }
        event.wav = wav64_load(event.rom_path, nullptr);
        if (!event.wav) {
            debugf("[audio] missing event=%s path=%s (disabled)\n", event.id, event.rom_path);
        } else {
            wav64_set_loop(event.wav, event.loop);
        }
        ++count_;
    }
    if (std::fgets(line, sizeof(line), f)) {
        int extra_length = static_cast<int>(std::strlen(line));
        while (extra_length > 0 && (line[extra_length - 1] == '\n' || line[extra_length - 1] == '\r')) {
            line[--extra_length] = '\0';
        }
        if (extra_length != 0) { std::fclose(f); return false; }
    }
    std::fclose(f);
    return count_ == declared;
}

AudioService::Event* AudioService::Find(AudioEventId event) {
    const char* name = EventName(event);
    for (int i = 0; i < count_; ++i) {
        if (std::strcmp(events_[i].id, name) == 0) return &events_[i];
    }
    return nullptr;
}

bool AudioService::Play(Event* event, int channel) {
    if (!mixer_ready_ || !event || !event->wav) return false;
    if (mixer_ch_playing(channel)) {
        ++channel_exhaustions_;
        debugf("[audio] channel exhausted event=%s ch=%d\n", event->id, channel);
        return false;
    }
    mixer_ch_set_vol(channel, event->volume, event->volume);
    wav64_play(event->wav, channel);
    return true;
}

bool AudioService::PlaySfx(AudioEventId event) {
    Event* record = Find(event);
    return record && !record->music && Play(record, kSfxChannel);
}

bool AudioService::PlayMusic(AudioEventId event) {
    Event* record = Find(event);
    if (!record || !record->music) return false;
    if (mixer_ch_playing(kMusicChannel)) mixer_ch_stop(kMusicChannel);
    return Play(record, kMusicChannel);
}

void AudioService::StopMusic() {
    if (mixer_ready_) mixer_ch_stop(kMusicChannel);
}

}  // namespace madeline_cube
