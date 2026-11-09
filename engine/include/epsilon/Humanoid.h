// Humanoid.h - Cuerpos de tamano humano: andar, correr, saltar y chocar.
//
// El universo va en kilometros; esto va en metros, con Y hacia arriba, dentro de un recinto. Quien lo
// use decide donde esta ese recinto en el mundo. Asi una persona no depende del origen flotante,
// y sus milimetros no se pierden al estar a muchas unidades astronomicas del centro.
//
// El cuerpo es una caja vertical del alto y el ancho de una persona. Los escalones bajos se suben
// solos, sin saltar. Con gravedad 0 flota: no toca el suelo y se mueve solo con lo que ya llevaba.
#pragma once

#include <vector>

#include "epsilon/Math3D.h"

namespace eps {

struct HumanoidRules {
    double height = 1.8;       // m
    double radius = 0.3;       // m
    double eyeHeight = 1.68;   // m
    double walkSpeed = 1.6;    // m/s
    double runSpeed = 4.5;     // m/s
    double jumpSpeed = 3.2;    // m/s: unos 50 cm a 1 g
    double groundAccel = 18.0; // m/s2
    double airAccel = 2.5;     // m/s2
    double stepHeight = 0.4;   // m: lo que sube sin saltar
};

struct HumanoidState {
    Vec3d position;            // m: los pies
    Vec3d velocity;            // m/s
    double yaw = 0;            // rad: 0 mira hacia +Z
    double pitch = 0;          // rad: positivo, hacia abajo
    bool grounded = false;
};

struct HumanoidInput {
    double forward = 0, strafe = 0;  // -1 a 1
    bool run = false, jump = false;
};

struct SolidBox {
    Vec3d min, max;            // m
};

struct WalkArea {
    std::vector<SolidBox> solids;
};

Vec3d HumanoidForward(const HumanoidState& h);  // adonde mira, sin contar la inclinacion

// Avanza dt segundos. gravity en m/s2, hacia -Y.
void StepHumanoid(HumanoidState& h, const HumanoidInput& input, const WalkArea& area, const HumanoidRules& rules, double gravity, double dt);

// El cuerpo cabe en esa posicion sin tocar nada.
bool HumanoidFits(const Vec3d& position, const WalkArea& area, const HumanoidRules& rules);

// Cuanto se puede avanzar desde from hacia to sin entrar en un solido, de 0 a 1. Para la camara.
double ClearRay(const Vec3d& from, const Vec3d& to, const WalkArea& area, double margin);

}  // namespace eps
