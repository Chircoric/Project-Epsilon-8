// SciFiHud.h - Interfaz de estilo ciencia ficcion dibujada con Dear ImGui.
//
// Solo usa ImGui como lienzo y para las fuentes: no hay ventanas ni botones, asi que no
// recibe eventos de SDL. Se dibuja en el hilo principal.
#pragma once

#include "epsilon/HudModel.h"
#include "epsilon/SdlGpuBackend.h"

namespace eps {

class SciFiHud final : public IGpuOverlay {
public:
    ~SciFiHud() override;

    bool Initialize(SdlGpuBackend& backend);
    void Shutdown();

    // Copia lo que se dibujara en el proximo Render.
    void SetFrame(const HudFrame& frame) { frame_ = &frame; }

    void Prepare(SDL_GPUCommandBuffer* cmd) override;
    void Draw(SDL_GPUCommandBuffer* cmd, SDL_GPURenderPass* pass) override;

private:
    void BuildFrame();

    const HudFrame* frame_ = nullptr;
    bool initialized_ = false;
    bool hasDrawData_ = false;
    double clock_ = 0;
};

}  // namespace eps
