#include "game/UiSounds.h"

#include <algorithm>
#include <cstdio>

namespace game {

namespace {

// Orden igual que en UiSound.
constexpr const char* kFiles[] = {"ui_click.wav", "ui_hover.wav", "ui_confirmar.wav", "ui_error.wav", "ui_panel.wav", "ui_radar_barrido.wav"};
static_assert(sizeof(kFiles) / sizeof(kFiles[0]) == static_cast<std::size_t>(UiSound::Count));

}  // namespace

UiSounds::~UiSounds() { Shutdown(); }

bool UiSounds::Initialize(const std::string& folder) {
    Shutdown();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::printf("Audio: %s\n", SDL_GetError());
        return false;
    }
    bool any = false;
    for (std::size_t i = 0; i < clips_.size(); ++i) {
        const std::string path = folder + "/" + kFiles[i];
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 length = 0;
        if (!SDL_LoadWAV(path.c_str(), &spec, &data, &length)) {
            std::printf("Sonido %s: %s\n", kFiles[i], SDL_GetError());
            continue;
        }
        // El flujo convierte del formato del WAV al del dispositivo. Se abre pausado.
        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (!stream || !SDL_ResumeAudioStreamDevice(stream)) {
            std::printf("Audio %s: %s\n", kFiles[i], SDL_GetError());
            if (stream) SDL_DestroyAudioStream(stream);
            SDL_free(data);
            continue;
        }
        SDL_SetAudioStreamGain(stream, volume_);
        clips_[i] = Clip{stream, data, length};
        any = true;
    }
    return any;
}

void UiSounds::Shutdown() {
    for (Clip& clip : clips_) {
        if (clip.stream) SDL_DestroyAudioStream(clip.stream);
        if (clip.data) SDL_free(clip.data);
        clip = Clip{};
    }
}

void UiSounds::SetVolume(float volume) {
    volume_ = std::clamp(volume, 0.0f, 1.0f);
    for (const Clip& clip : clips_)
        if (clip.stream) SDL_SetAudioStreamGain(clip.stream, volume_);
}

void UiSounds::Stop(UiSound sound) {
    const Clip& clip = clips_[static_cast<std::size_t>(sound)];
    if (clip.stream) SDL_ClearAudioStream(clip.stream);
}

void UiSounds::Play(UiSound sound) {
    Clip& clip = clips_[static_cast<std::size_t>(sound)];
    if (!clip.stream) return;
    SDL_ClearAudioStream(clip.stream);
    SDL_PutAudioStreamData(clip.stream, clip.data, static_cast<int>(clip.length));
}

}  // namespace game
