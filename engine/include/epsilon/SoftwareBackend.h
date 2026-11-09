// SoftwareBackend.h - Backend de referencia por CPU.
//
// Aplica a cada FramePacket las mismas matrices que aplicara el vertex shader y guarda el
// resultado en un BMP. Sirve para probar el motor sin ventana ni GPU. No esta pensado para jugar.
#pragma once

#include <string>
#include <vector>

#include "epsilon/RenderTypes.h"

namespace eps {

class SoftwareBackend final : public IRenderBackend {
public:
    void Render(const FramePacket& frame) override;
    bool SaveBmp(const std::string& path) const;

private:
    void Blend(int x, int y, const Color& c, float alpha, BlendMode mode);
    void DrawDisc(float cx, float cy, float radius, const Color& c, BlendMode mode);
    void DrawLine(const FramePacket& frame, const LineInstance& line);

    int width_ = 0, height_ = 0;
    std::vector<float> rgb_;
};

}  // namespace eps
