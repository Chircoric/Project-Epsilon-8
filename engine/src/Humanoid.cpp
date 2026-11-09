#include "epsilon/Humanoid.h"

#include <algorithm>
#include <cmath>

namespace eps {

namespace {

constexpr double kSkin = 1.0e-4;  // m: para no quedarse pegado a lo que se acaba de tocar

bool Touches(const Vec3d& feet, const SolidBox& box, const HumanoidRules& r) {
    return feet.x + r.radius > box.min.x && feet.x - r.radius < box.max.x && feet.z + r.radius > box.min.z && feet.z - r.radius < box.max.z &&
           feet.y + r.height > box.min.y && feet.y < box.max.y;
}

const SolidBox* FirstTouch(const Vec3d& feet, const WalkArea& area, const HumanoidRules& r) {
    for (const SolidBox& box : area.solids)
        if (Touches(feet, box, r)) return &box;
    return nullptr;
}

// Mueve en horizontal por un eje. Lo bajo se sube; lo alto para el cuerpo en seco.
void MoveFlat(HumanoidState& h, double Vec3d::*axis, double delta, const WalkArea& area, const HumanoidRules& r) {
    if (delta == 0.0) return;
    h.position.*axis += delta;
    const SolidBox* box = FirstTouch(h.position, area, r);
    if (!box) return;
    const double rise = box->max.y - h.position.y;
    if (h.grounded && rise > 0.0 && rise <= r.stepHeight) {
        Vec3d up = h.position;
        up.y = box->max.y + kSkin;
        if (!FirstTouch(up, area, r)) {
            h.position = up;
            return;
        }
    }
    h.position.*axis = delta > 0.0 ? box->min.*axis - r.radius - kSkin : box->max.*axis + r.radius + kSkin;
    h.velocity.*axis = 0.0;
}

void MoveUp(HumanoidState& h, double delta, const WalkArea& area, const HumanoidRules& r) {
    h.position.y += delta;
    const SolidBox* box = FirstTouch(h.position, area, r);
    if (!box) return;
    if (delta <= 0.0) {
        h.position.y = box->max.y + kSkin;
        h.grounded = true;
    } else {
        h.position.y = box->min.y - r.height - kSkin;
    }
    h.velocity.y = 0.0;
}

}  // namespace

Vec3d HumanoidForward(const HumanoidState& h) { return Vec3d{std::sin(h.yaw), 0.0, std::cos(h.yaw)}; }

void StepHumanoid(HumanoidState& h, const HumanoidInput& in, const WalkArea& area, const HumanoidRules& r, double gravity, double dt) {
    constexpr double kMaxStep = 1.0 / 120.0;  // ni a tirones se atraviesa una pared
    const int parts = std::clamp(static_cast<int>(std::ceil(dt / kMaxStep)), 1, 32);
    const double step = dt / parts;
    const Vec3d forward = HumanoidForward(h);
    const Vec3d right{forward.z, 0.0, -forward.x};
    Vec3d wish = forward * std::clamp(in.forward, -1.0, 1.0) + right * std::clamp(in.strafe, -1.0, 1.0);
    const double amount = Length(wish);
    if (amount > 1.0) wish = wish * (1.0 / amount);
    const Vec3d wanted = wish * (in.run ? r.runSpeed : r.walkSpeed);

    for (int i = 0; i < parts; ++i) {
        // En el suelo se manda; en el aire, apenas. Sin gravedad, ademas, no hay de que empujarse.
        if (h.grounded || gravity > 0.0) {
            const double accel = (h.grounded ? r.groundAccel : r.airAccel) * step;
            const Vec3d change{wanted.x - h.velocity.x, 0.0, wanted.z - h.velocity.z};
            const double size = Length(change);
            const Vec3d applied = size > accel ? change * (accel / size) : change;
            h.velocity.x += applied.x;
            h.velocity.z += applied.z;
        }
        if (in.jump && h.grounded) {
            h.velocity.y = r.jumpSpeed;
            h.grounded = false;
        }
        h.velocity.y -= gravity * step;

        MoveFlat(h, &Vec3d::x, h.velocity.x * step, area, r);
        MoveFlat(h, &Vec3d::z, h.velocity.z * step, area, r);
        const bool stood = h.grounded;
        h.grounded = false;
        MoveUp(h, h.velocity.y * step, area, r);
        // Bajando un escalon no se sale volando: se pega al suelo si esta justo debajo.
        if (stood && !h.grounded && h.velocity.y <= 0.0 && gravity > 0.0) {
            Vec3d below = h.position;
            below.y -= r.stepHeight;
            for (const SolidBox& box : area.solids) {
                if (!Touches(below, box, r) || box.max.y > h.position.y) continue;
                h.position.y = box.max.y + kSkin;
                h.velocity.y = 0.0;
                h.grounded = true;
                break;
            }
        }
    }
}

bool HumanoidFits(const Vec3d& position, const WalkArea& area, const HumanoidRules& r) { return FirstTouch(position, area, r) == nullptr; }

double ClearRay(const Vec3d& from, const Vec3d& to, const WalkArea& area, double margin) {
    constexpr int kSteps = 24;
    const Vec3d line = to - from;
    for (int i = 1; i <= kSteps; ++i) {
        const double t = static_cast<double>(i) / kSteps;
        const Vec3d p = from + line * t;
        for (const SolidBox& box : area.solids) {
            if (p.x > box.min.x - margin && p.x < box.max.x + margin && p.y > box.min.y - margin && p.y < box.max.y + margin && p.z > box.min.z - margin && p.z < box.max.z + margin)
                return static_cast<double>(i - 1) / kSteps;
        }
    }
    return 1.0;
}

}  // namespace eps
