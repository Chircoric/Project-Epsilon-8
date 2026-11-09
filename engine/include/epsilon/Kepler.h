// Kepler.h - Orbitas elipticas como funcion pura del tiempo.
//
// Unidades: kilometros, segundos y radianes. Masa como mu = G * M, en km^3/s^2.
//
// La posicion se calcula directamente desde t, sin integrar paso a paso. Por eso no acumula
// error, da igual el avance del tiempo y dos hilos obtienen exactamente lo mismo:
//
//   M(t) = M0 + n * t                     anomalia media, con n = sqrt(mu / a^3)
//   M = E - e * sin(E)                    ecuacion de Kepler
//   r(t) = a * (cos(E) - e) * P + b * sin(E) * Q
//
// P y Q son vectores unitarios del plano de la orbita: P apunta al periastro. Se obtienen de
// los elementos i, W (nodo ascendente) y w (argumento del periastro).
//
// Plano de referencia: XZ. El norte es -Y, de modo que una orbita directa gira de +X hacia +Z.
#pragma once

#include <string>
#include <vector>

#include "epsilon/JobSystem.h"
#include "epsilon/Math3D.h"
#include "epsilon/SceneGraph.h"

namespace eps {

struct OrbitalElements {
    double semiMajorAxis = 1;    // a, km
    double eccentricity = 0;     // e, de 0 a menos de 1
    double inclination = 0;      // i, rad; mas de pi/2 es una orbita retrograda
    double ascendingNode = 0;    // W, rad
    double argPeriapsis = 0;     // w, rad
    double meanAnomaly0 = 0;     // M0, rad, en t = 0
    double mu = 1;               // G * (masa central + masa del cuerpo), km^3/s^2
};

// Elementos mas los valores derivados, ya calculados para no repetirlos en cada paso.
struct KeplerOrbit {
    NodeId node = kNoNode;   // kNoNode = hueco libre o elemento sin nodo
    bool active = true;      // false = no se calcula hasta que vuelva a importar
    OrbitalElements elements;
    double meanMotion = 0;   // n, rad/s
    double semiMinorAxis = 0;  // b, km
    Vec3d p, q;              // base unitaria del plano; p apunta al periastro
};

KeplerOrbit MakeOrbit(NodeId node, const OrbitalElements& elements);

// Resuelve M = E - e * sin(E). Devuelve E en [-pi, pi]. Admite e hasta 0.999.
double SolveKepler(double meanAnomaly, double eccentricity);

double MeanAnomalyAt(const KeplerOrbit& orbit, double time);
double EccentricAnomalyAt(const KeplerOrbit& orbit, double time);
double TrueAnomalyAt(const KeplerOrbit& orbit, double time);

// Posicion y velocidad relativas al cuerpo central, en km y km/s.
Vec3d OrbitPositionFromEccentric(const KeplerOrbit& orbit, double eccentricAnomaly);
Vec3d OrbitPosition(const KeplerOrbit& orbit, double time);
Vec3d OrbitVelocity(const KeplerOrbit& orbit, double time);

// Elementos de la orbita que pasa por position con esa velocidad en el instante time. Es lo
// inverso de OrbitPosition y OrbitVelocity. Devuelve false si la orbita no es eliptica.
bool OrbitFromState(const Vec3d& position, const Vec3d& velocity, double mu, double time, OrbitalElements& out);

// Problema de Lambert: velocidad de salida para ir de r1 a r2 en el tiempo dt, por el camino
// directo y sin dar vueltas completas. Devuelve false si no hay solucion.
bool SolveLambert(const Vec3d& r1, const Vec3d& r2, double dt, double mu, Vec3d& departure, Vec3d& arrival);

double OrbitPeriod(const OrbitalElements& elements);  // s
double Periapsis(const OrbitalElements& elements);    // km
double Apoapsis(const OrbitalElements& elements);     // km

// Escribe en cada nodo su posicion en el instante absoluto indicado.
void UpdateOrbits(std::vector<KeplerOrbit>& orbits, double time, SceneGraph& scene, JobSystem& jobs);

// Giro de un cuerpo: theta(t) = phase + rate * t, alrededor de axis.
struct SpinModel {
    Vec3d axis{0, -1, 0};  // eje unitario, en el marco de la orbita
    double rate = 0;       // rad/s; un giro retrogrado es un eje con oblicuidad mayor de 90 grados
    double phase = 0;      // rad, en t = 0
};

// Rotacion mas corta que lleva el norte del motor (-Y) hasta el eje de giro.
Quatd SpinAxisRotation(const Vec3d& axis);

// Ecuaciones con los numeros de este cuerpo, listas para leer o copiar.
std::string OrbitEquation(const KeplerOrbit& orbit);
std::string SpinEquation(const SpinModel& spin);

}  // namespace eps
