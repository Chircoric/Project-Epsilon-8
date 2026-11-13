// UiSounds.h - Efectos de sonido de la interfaz (clic, hover, confirmar, error, panel).
//
// Los WAV se cargan en memoria al empezar. Cada efecto tiene su propio flujo hacia el dispositivo
// de audio por defecto, asi que pueden sonar a la vez. Si no hay audio, el juego sigue sin sonido.
#pragma once

#include <array>
#include <cstddef>
#include <string>

#include <SDL3/SDL.h>

namespace game {

enum class UiSound : int { Click, Hover, Confirm, Error, Panel, RadarScan, Count };

class UiSounds {
public:
    UiSounds() = default;
    ~UiSounds();
    UiSounds(const UiSounds&) = delete;
    UiSounds& operator=(const UiSounds&) = delete;

    // Carga los WAV de la carpeta. Devuelve false si no hay ningun sonido disponible.
    bool Initialize(const std::string& folder);
    void Shutdown();

    // Reinicia el efecto desde el principio.
    void Play(UiSound sound);

    // Corta el efecto que este sonando (el del barrido dura 30 s, igual que el barrido, y se corta si este se cancela o se pierde).
    void Stop(UiSound sound);

    // Volumen de todos los efectos, de 0 a 1. Se aplica a los flujos ya abiertos y a los que se abran.
    void SetVolume(float volume);

private:
    float volume_ = 0.5f;

    struct Clip {
        SDL_AudioStream* stream = nullptr;
        Uint8* data = nullptr;
        Uint32 length = 0;
    };
    std::array<Clip, static_cast<std::size_t>(UiSound::Count)> clips_{};
};

}  // namespace game
