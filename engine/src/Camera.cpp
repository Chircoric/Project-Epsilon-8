#include "epsilon/Camera.h"

namespace eps {

void Camera::SetViewport(int width, int height, float focalPx, float nearZ) {
    width_ = width;
    height_ = height;
    focalPx_ = focalPx;
    nearZ_ = nearZ;
}

void Camera::SetYawPitchRoll(float yaw, float pitch, float roll) {
    const Quatf qYaw = Quatf::FromAxisAngle({0, 1, 0}, yaw);
    const Quatf qPitch = Quatf::FromAxisAngle({1, 0, 0}, -pitch);
    const Quatf qRoll = Quatf::FromAxisAngle({0, 0, 1}, roll);
    SetOrientation(qYaw * qPitch * qRoll);
}

ScreenPoint Camera::WorldToScreen(const Vec3d& world) const {
    const Vec3f v = ToViewSpace(world);
    ScreenPoint s;
    s.depth = v.z;
    if (v.z <= nearZ_) return s;
    s.x = v.x / v.z * focalPx_ + static_cast<float>(width_) * 0.5f;
    s.y = v.y / v.z * focalPx_ + static_cast<float>(height_) * 0.5f;
    s.visible = true;
    return s;
}

Frustum Camera::CameraRelativeFrustum() const {
    // Metodo de Gribb-Hartmann: cada plano sale de combinar filas de P * V.
    const Mat4f vp = Projection() * ViewRotation();
    const auto row = [&vp](int f) { return Vec4f{vp.m[0][f], vp.m[1][f], vp.m[2][f], vp.m[3][f]}; };
    const Vec4f r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

    const auto plane = [](const Vec4f& a, const Vec4f& b, float sign) {
        const Vec3f n{a.x + sign * b.x, a.y + sign * b.y, a.z + sign * b.z};
        const float len = Length(n);
        return Frustum::Plane{n / len, (a.w + sign * b.w) / len};
    };

    Frustum f;
    f.planes[0] = plane(r3, r0, 1.0f);
    f.planes[1] = plane(r3, r0, -1.0f);
    f.planes[2] = plane(r3, r1, 1.0f);
    f.planes[3] = plane(r3, r1, -1.0f);
    f.planes[4] = plane(r3, r2, -1.0f);  // z_clip <= w_clip equivale a z >= near
    return f;
}

int PickNearestToCenter(const Camera& camera, const Vec3d* positions, std::size_t count,
                        float maxPixels, float marginPixels) {
    const float w = static_cast<float>(camera.Width());
    const float h = static_cast<float>(camera.Height());
    int best = -1;
    float bestDist = maxPixels;
    for (std::size_t i = 0; i < count; ++i) {
        const ScreenPoint p = camera.WorldToScreen(positions[i]);
        if (!p.visible || p.x < -marginPixels || p.x > w + marginPixels ||
            p.y < -marginPixels || p.y > h + marginPixels) {
            continue;
        }
        const float dx = p.x - w * 0.5f;
        const float dy = p.y - h * 0.5f;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < bestDist) {
            bestDist = dist;
            best = static_cast<int>(i);
        }
    }
    return best;
}

}  // namespace eps
