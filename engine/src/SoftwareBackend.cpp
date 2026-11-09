#include "epsilon/SoftwareBackend.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

#include "epsilon/FrameBuilder.h"

namespace eps {

namespace {

struct PixelPoint {
    float x, y;
};

// Misma operacion que el vertex shader: clip = P * vista; NDC = clip / w; NDC -> pixeles.
PixelPoint ToPixels(const FramePacket& f, const Vec3f& view) {
    const Vec4f clip = f.projection * Vec4f{view.x, view.y, view.z, 1.0f};
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    return {(ndcX + 1.0f) * 0.5f * static_cast<float>(f.width),
            (1.0f - ndcY) * 0.5f * static_cast<float>(f.height)};
}

void Put16(std::ofstream& o, std::uint16_t v) { o.write(reinterpret_cast<const char*>(&v), 2); }
void Put32(std::ofstream& o, std::uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); }

}  // namespace

void SoftwareBackend::Blend(int x, int y, const Color& c, float alpha, BlendMode mode) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    float* p = &rgb_[(static_cast<std::size_t>(y) * width_ + x) * 3];
    const float keep = mode == BlendMode::Additive ? 1.0f : 1.0f - alpha;
    p[0] = p[0] * keep + c.r * alpha;
    p[1] = p[1] * keep + c.g * alpha;
    p[2] = p[2] * keep + c.b * alpha;
}

void SoftwareBackend::DrawDisc(float cx, float cy, float radius, const Color& c, BlendMode mode) {
    const int x0 = static_cast<int>(std::floor(cx - radius));
    const int x1 = static_cast<int>(std::ceil(cx + radius));
    const int y0 = static_cast<int>(std::floor(cy - radius));
    const int y1 = static_cast<int>(std::ceil(cy + radius));
    for (int y = std::max(y0, 0); y <= std::min(y1, height_ - 1); ++y) {
        for (int x = std::max(x0, 0); x <= std::min(x1, width_ - 1); ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            const float d = std::sqrt(dx * dx + dy * dy) / radius;
            if (d >= 1.0f) continue;
            const float coverage = mode == BlendMode::Additive
                                       ? (1.0f - d) * (1.0f - d)                 // halo suave
                                       : std::clamp((1.0f - d) * radius, 0.0f, 1.0f);  // borde nitido
            Blend(x, y, c, c.a * coverage, mode);
        }
    }
}

void SoftwareBackend::DrawLine(const FramePacket& f, const LineInstance& line) {
    // Recorta contra el plano cercano antes de dividir entre w.
    Vec3f a = line.viewA, b = line.viewB;
    const float nearZ = f.nearZ + 1e-3f;
    if (a.z < nearZ || b.z < nearZ) {
        const float t = (nearZ - a.z) / (b.z - a.z);
        const Vec3f cut = a + (b - a) * t;
        (a.z < nearZ ? a : b) = cut;
    }
    const PixelPoint p0 = ToPixels(f, a);
    const PixelPoint p1 = ToPixels(f, b);
    const float dx = p1.x - p0.x, dy = p1.y - p0.y;
    const int steps = static_cast<int>(std::min(std::max(std::abs(dx), std::abs(dy)), 8000.0f));
    if (steps <= 0) return;
    for (int i = 0; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        Blend(static_cast<int>(p0.x + dx * t), static_cast<int>(p0.y + dy * t), line.color, line.color.a,
              BlendMode::Additive);
    }
}

void SoftwareBackend::Render(const FramePacket& f) {
    width_ = f.width;
    height_ = f.height;
    rgb_.resize(static_cast<std::size_t>(width_) * height_ * 3);
    for (std::size_t i = 0; i < rgb_.size(); i += 3) {
        rgb_[i] = f.clearColor.r;
        rgb_[i + 1] = f.clearColor.g;
        rgb_[i + 2] = f.clearColor.b;
    }

    if (f.sky) {
        for (const SkyStar& s : *f.sky) {
            const Vec3f v = f.viewRotation.TransformDirection(s.direction);
            if (v.z <= 0.01f) continue;
            const PixelPoint p = ToPixels(f, v);
            const float b = SkyStarBrightness(s, f.time);
            DrawDisc(p.x, p.y, s.size + 0.5f, Color{b, b, b, 1.0f}, BlendMode::Additive);
        }
    }

    for (const LineInstance& l : f.lines) DrawLine(f, l);

    // Referencia simple: disco con luz difusa, sin la superficie procedural del shader.
    for (const SphereInstance& s : f.spheres) {
        if (s.viewCenter.z <= f.nearZ) continue;
        const PixelPoint c = ToPixels(f, s.viewCenter);
        const Color base = f.surfaces ? (*f.surfaces)[s.surface].b : Color{};
        const int r = static_cast<int>(std::ceil(s.pixelRadius));
        for (int y = std::max(static_cast<int>(c.y) - r, 0); y <= std::min(static_cast<int>(c.y) + r, height_ - 1); ++y) {
            for (int x = std::max(static_cast<int>(c.x) - r, 0); x <= std::min(static_cast<int>(c.x) + r, width_ - 1); ++x) {
                const float nx = (static_cast<float>(x) + 0.5f - c.x) / s.pixelRadius;
                const float ny = (static_cast<float>(y) + 0.5f - c.y) / s.pixelRadius;
                const float d2 = nx * nx + ny * ny;
                if (d2 >= 1.0f) continue;
                const float light = Dot(Vec3f{nx, ny, -std::sqrt(1.0f - d2)}, s.lightDirection);
                const float shade = Length(s.lightDirection) > 0.0f ? 0.08f + 0.92f * std::max(light, 0.0f) : 1.0f;
                Blend(x, y, Color{base.r * shade, base.g * shade, base.b * shade, 1.0f}, 1.0f, BlendMode::Alpha);
            }
        }
    }

    for (const SpriteBatch& batch : f.batches) {
        for (std::uint32_t i = batch.first; i < batch.first + batch.count; ++i) {
            const SpriteInstance& s = f.sprites[i];
            const PixelPoint p = ToPixels(f, s.viewPosition);
            DrawDisc(p.x, p.y, s.pixelRadius, s.tint, batch.blend);
        }
    }

    if (f.screenTint.a > 0.0f) {
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) Blend(x, y, f.screenTint, f.screenTint.a, BlendMode::Alpha);
    }
}

bool SoftwareBackend::SaveBmp(const std::string& path) const {
    std::ofstream o(path, std::ios::binary);
    if (!o || width_ <= 0 || height_ <= 0) return false;

    const std::uint32_t rowBytes = (static_cast<std::uint32_t>(width_) * 3 + 3) & ~3u;
    const std::uint32_t imageBytes = rowBytes * static_cast<std::uint32_t>(height_);

    o.write("BM", 2);
    Put32(o, 54 + imageBytes);
    Put32(o, 0);
    Put32(o, 54);
    Put32(o, 40);
    Put32(o, static_cast<std::uint32_t>(width_));
    Put32(o, static_cast<std::uint32_t>(height_));
    Put16(o, 1);
    Put16(o, 24);
    Put32(o, 0);
    Put32(o, imageBytes);
    Put32(o, 2835);
    Put32(o, 2835);
    Put32(o, 0);
    Put32(o, 0);

    std::vector<char> row(rowBytes, 0);
    const auto toByte = [](float v) { return static_cast<char>(static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f)); };
    for (int y = height_ - 1; y >= 0; --y) {
        const float* p = &rgb_[static_cast<std::size_t>(y) * width_ * 3];
        for (int x = 0; x < width_; ++x) {
            row[static_cast<std::size_t>(x) * 3 + 0] = toByte(p[x * 3 + 2]);
            row[static_cast<std::size_t>(x) * 3 + 1] = toByte(p[x * 3 + 1]);
            row[static_cast<std::size_t>(x) * 3 + 2] = toByte(p[x * 3 + 0]);
        }
        o.write(row.data(), static_cast<std::streamsize>(row.size()));
    }
    return static_cast<bool>(o);
}

}  // namespace eps
