// BlackHole.h - Formulas de los dos tipos de agujero negro.
//
//   Schwarzschild: no rota. Horizonte, esfera de fotones y sombra son circulos exactos.
//   Kerr:          rota. El horizonte se encoge, el disco llega mas cerca y la sombra se
//                  desplaza y se aplana por el lado que gira hacia el observador.
//
// La masa es libre: la misma formula sirve para uno estelar y para uno supermasivo.
// Unidad de longitud: el radio de Schwarzschild rs = 2GM/c^2. spin es a/M, de 0 a menos de 1.
//
// TraceLensRay es la misma integracion que hace blackhole.hlsl por pixel. Existe aqui para
// medirla contra las soluciones exactas; ver TestBlackHoles y docs/astrofisica.md.
#pragma once

#include "epsilon/Math3D.h"

namespace eps::astro {

inline constexpr double kLightSpeed = 299792.458;  // km/s

double SchwarzschildRadius(double massKg);         // km

// Todas en radios de Schwarzschild.
double KerrHorizon(double spin);                   // 1 sin rotacion; 0.5 en el limite
double InnermostStableOrbit(double spin);          // 3 sin rotacion; 0.5 en el limite, orbita directa
double PhotonSphere(double spin, bool prograde);   // 1.5 sin rotacion

// Bordes exactos de la sombra sobre el ecuador, vista de canto, por el lado que se aleja
// y el que se acerca. Sin rotacion son -+2.598. Bardeen (1973).
void KerrShadowEdges(double spin, double& left, double& right);

struct LensResult {
    bool captured = false;
    Vec3d direction;      // al salir
    double closest = 0;   // menor distancia al centro
    int steps = 0;
};

// Origen y direccion en radios de Schwarzschild, con el agujero en el origen y el eje en +Y.
LensResult TraceLensRay(const Vec3d& origin, const Vec3d& direction, double spin, int maxSteps = 600, double reach = 60.0, double frameDragging = -1.0);

}  // namespace eps::astro
