#include "epsilon/Kepler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace eps {

namespace {

constexpr std::size_t kOrbitGrain = 2048;
constexpr double kTau = 2.0 * kPi;

// Lleva un angulo a [-pi, pi].
double WrapAngle(double angle) {
    const double wrapped = std::fmod(angle, kTau);
    if (wrapped > kPi) return wrapped - kTau;
    if (wrapped < -kPi) return wrapped + kTau;
    return wrapped;
}

// Vector del plano XZ del motor a partir de coordenadas de orbita con el norte en z.
Vec3d ToEngineAxes(double x, double y, double north) { return {x, -north, y}; }

}  // namespace

KeplerOrbit MakeOrbit(NodeId node, const OrbitalElements& e) {
    if (!(e.semiMajorAxis > 0.0) || !(e.mu > 0.0) || e.eccentricity < 0.0 || e.eccentricity >= 1.0)
        throw std::invalid_argument("MakeOrbit: se necesita a > 0, mu > 0 y 0 <= e < 1");

    const double cosW = std::cos(e.ascendingNode), sinW = std::sin(e.ascendingNode);
    const double cosW2 = std::cos(e.argPeriapsis), sinW2 = std::sin(e.argPeriapsis);
    const double cosI = std::cos(e.inclination), sinI = std::sin(e.inclination);

    KeplerOrbit o;
    o.node = node;
    o.elements = e;
    o.meanMotion = std::sqrt(e.mu / (e.semiMajorAxis * e.semiMajorAxis * e.semiMajorAxis));
    o.semiMinorAxis = e.semiMajorAxis * std::sqrt(1.0 - e.eccentricity * e.eccentricity);
    // Producto Rz(W) * Rx(i) * Rz(w), escrito por columnas.
    o.p = ToEngineAxes(cosW * cosW2 - sinW * sinW2 * cosI, sinW * cosW2 + cosW * sinW2 * cosI, sinW2 * sinI);
    o.q = ToEngineAxes(-cosW * sinW2 - sinW * cosW2 * cosI, -sinW * sinW2 + cosW * cosW2 * cosI, cosW2 * sinI);
    return o;
}

double SolveKepler(double meanAnomaly, double e) {
    const double m = WrapAngle(meanAnomaly);
    if (e <= 0.0) return m;

    // Con e alta, Newton puede divergir desde M; desde +-pi siempre converge.
    double E = e < 0.8 ? m : (m >= 0.0 ? kPi : -kPi);
    for (int i = 0; i < 40; ++i) {
        const double f = E - e * std::sin(E) - m;
        const double step = f / (1.0 - e * std::cos(E));
        E -= step;
        if (std::abs(step) < 1e-14) break;
    }
    return E;
}

double MeanAnomalyAt(const KeplerOrbit& o, double time) {
    return WrapAngle(o.elements.meanAnomaly0 + WrapAngle(o.meanMotion * time));
}

double EccentricAnomalyAt(const KeplerOrbit& o, double time) {
    return SolveKepler(MeanAnomalyAt(o, time), o.elements.eccentricity);
}

double TrueAnomalyAt(const KeplerOrbit& o, double time) {
    const double E = EccentricAnomalyAt(o, time);
    const double e = o.elements.eccentricity;
    return 2.0 * std::atan2(std::sqrt(1.0 + e) * std::sin(E * 0.5), std::sqrt(1.0 - e) * std::cos(E * 0.5));
}

Vec3d OrbitPositionFromEccentric(const KeplerOrbit& o, double E) {
    const double x = o.elements.semiMajorAxis * (std::cos(E) - o.elements.eccentricity);
    const double y = o.semiMinorAxis * std::sin(E);
    return o.p * x + o.q * y;
}

Vec3d OrbitPosition(const KeplerOrbit& o, double time) { return OrbitPositionFromEccentric(o, EccentricAnomalyAt(o, time)); }

Vec3d OrbitVelocity(const KeplerOrbit& o, double time) {
    const double E = EccentricAnomalyAt(o, time);
    const double rate = o.meanMotion / (1.0 - o.elements.eccentricity * std::cos(E));  // dE/dt
    return o.p * (-o.elements.semiMajorAxis * std::sin(E) * rate) + o.q * (o.semiMinorAxis * std::cos(E) * rate);
}

bool OrbitFromState(const Vec3d& position, const Vec3d& velocity, double mu, double time, OrbitalElements& out) {
    // De los ejes del motor a coordenadas de orbita, con el norte en z.
    const Vec3d r{position.x, position.z, -position.y};
    const Vec3d v{velocity.x, velocity.z, -velocity.y};
    const double radius = Length(r), speed2 = Dot(v, v);
    const double energy = 0.5 * speed2 - mu / radius;
    if (!(radius > 0.0) || energy >= 0.0) return false;

    const Vec3d h = Cross(r, v);
    const double hLength = Length(h);
    if (!(hLength > 0.0)) return false;  // caida en linea recta: no es una elipse
    const Vec3d eVector = Cross(v, h) * (1.0 / mu) - r * (1.0 / radius);
    const double e = Length(eVector);
    if (e >= 0.9999) return false;

    const double a = -mu / (2.0 * energy);
    const double inclination = std::acos(std::clamp(h.z / hLength, -1.0, 1.0));
    const double nodeLength = std::sqrt(h.x * h.x + h.y * h.y);

    // Sin nodo (orbita en el plano) o sin periastro (circular), el angulo que falta vale 0.
    const Vec3d nodeAxis = nodeLength > 1e-12 * hLength ? Vec3d{-h.y / nodeLength, h.x / nodeLength, 0.0} : Vec3d{1.0, 0.0, 0.0};
    const Vec3d nodeSide = Normalize(Cross(h, nodeAxis));
    const Vec3d periapsis = e > 1e-10 ? eVector * (1.0 / e) : nodeAxis;
    const Vec3d periapsisSide = Normalize(Cross(h, periapsis));

    const double trueAnomaly = std::atan2(Dot(r, periapsisSide), Dot(r, periapsis));
    const double E = 2.0 * std::atan2(std::sqrt(1.0 - e) * std::sin(trueAnomaly * 0.5), std::sqrt(1.0 + e) * std::cos(trueAnomaly * 0.5));
    const double M = E - e * std::sin(E);
    const double n = std::sqrt(mu / (a * a * a));

    out.semiMajorAxis = a;
    out.eccentricity = e;
    out.inclination = inclination;
    out.ascendingNode = std::atan2(nodeAxis.y, nodeAxis.x);
    out.argPeriapsis = std::atan2(Dot(periapsis, nodeSide), Dot(periapsis, nodeAxis));
    out.meanAnomaly0 = WrapAngle(M - WrapAngle(n * time));
    out.mu = mu;
    return true;
}

namespace {

// Funciones de Stumpff, con su serie cerca de cero.
double StumpffC(double z) {
    if (z > 1e-6) return (1.0 - std::cos(std::sqrt(z))) / z;
    if (z < -1e-6) return (std::cosh(std::sqrt(-z)) - 1.0) / -z;
    return 0.5 - z / 24.0;
}

double StumpffS(double z) {
    if (z > 1e-6) {
        const double s = std::sqrt(z);
        return (s - std::sin(s)) / (s * s * s);
    }
    if (z < -1e-6) {
        const double s = std::sqrt(-z);
        return (std::sinh(s) - s) / (s * s * s);
    }
    return 1.0 / 6.0 - z / 120.0;
}

}  // namespace

bool SolveLambert(const Vec3d& r1, const Vec3d& r2, double dt, double mu, Vec3d& departure, Vec3d& arrival) {
    const double a1 = Length(r1), a2 = Length(r2);
    if (!(a1 > 0.0) || !(a2 > 0.0) || !(dt > 0.0)) return false;

    // Angulo recorrido en sentido directo. El norte del motor es -Y.
    const double cosine = std::clamp(Dot(r1, r2) / (a1 * a2), -1.0, 1.0);
    double angle = std::acos(cosine);
    const Vec3d north{0.0, -1.0, 0.0};
    if (Dot(Cross(r1, r2), north) < 0.0) angle = 2.0 * kPi - angle;
    const double sine = std::sin(angle);
    if (std::abs(sine) < 1e-6) return false;  // 0 o 180 grados: el plano no queda definido

    const double A = sine * std::sqrt(a1 * a2 / (1.0 - std::cos(angle)));
    const double target = dt * std::sqrt(mu);
    const auto y = [&](double z) {
        return a1 + a2 + A * (z * StumpffS(z) - 1.0) / std::sqrt(StumpffC(z));
    };
    const auto timeOf = [&](double z) {
        const double value = y(z);
        const double x = std::sqrt(value / StumpffC(z));
        return x * x * x * StumpffS(z) + A * std::sqrt(value);
    };

    // Biseccion en z: el tiempo crece con z mientras y sea positivo, hasta z = (2 pi)^2.
    double low = -4.0 * kPi * kPi, high = 4.0 * kPi * kPi - 1e-6;
    while (y(low) < 0.0 && low < high) low += 0.01 * (high - low) + 1e-6;
    for (int guard = 0; y(low) < 0.0 && guard < 200; ++guard) low = low * 0.5 + high * 0.5;
    if (y(low) < 0.0 || timeOf(low) > target || timeOf(high) < target) return false;
    for (int i = 0; i < 120; ++i) {
        const double z = 0.5 * (low + high);
        if (y(z) < 0.0 || timeOf(z) < target) low = z;
        else high = z;
    }
    const double z = 0.5 * (low + high);
    const double value = y(z);
    if (value <= 0.0) return false;

    // Coeficientes de Lagrange.
    const double f = 1.0 - value / a1;
    const double g = A * std::sqrt(value / mu);
    const double gDot = 1.0 - value / a2;
    departure = (r2 - r1 * f) * (1.0 / g);
    arrival = (r2 * gDot - r1) * (1.0 / g);
    return true;
}

double OrbitPeriod(const OrbitalElements& e) {
    return kTau * std::sqrt(e.semiMajorAxis * e.semiMajorAxis * e.semiMajorAxis / e.mu);
}

double Periapsis(const OrbitalElements& e) { return e.semiMajorAxis * (1.0 - e.eccentricity); }
double Apoapsis(const OrbitalElements& e) { return e.semiMajorAxis * (1.0 + e.eccentricity); }

void UpdateOrbits(std::vector<KeplerOrbit>& orbits, double time, SceneGraph& scene, JobSystem& jobs) {
    jobs.ParallelFor(orbits.size(), kOrbitGrain, [&](std::size_t begin, std::size_t end, std::size_t) {
        for (std::size_t i = begin; i < end; ++i)
            if (orbits[i].node != kNoNode && orbits[i].active) scene.Local(orbits[i].node).position = OrbitPosition(orbits[i], time);
    });
}

Quatd SpinAxisRotation(const Vec3d& axis) {
    const Vec3d from{0, -1, 0};
    const Vec3d to = Normalize(axis);
    const double d = Dot(from, to);
    if (d < -0.999999) return Quatd::FromAxisAngle({1, 0, 0}, kPi);
    const Vec3d c = Cross(from, to);
    Quatd q;
    q.x = c.x; q.y = c.y; q.z = c.z; q.w = 1.0 + d;
    return q.Normalized();
}

std::string OrbitEquation(const KeplerOrbit& o) {
    const OrbitalElements& e = o.elements;
    const double a = e.semiMajorAxis, b = o.semiMinorAxis;
    char text[1024];
    std::snprintf(text, sizeof(text),
                  "  M(t) = %.9g + %.9e * t   [rad, t en s]\n"
                  "  E - %.6f * sin(E) = M(t)\n"
                  "  r(t) = %.6e * (1 - %.6f * cos(E))   [km, distancia al centro]\n"
                  "  x(t) = %.6e * (cos(E) - %.6f) + %.6e * sin(E)\n"
                  "  y(t) = %.6e * (cos(E) - %.6f) + %.6e * sin(E)\n"
                  "  z(t) = %.6e * (cos(E) - %.6f) + %.6e * sin(E)\n",
                  e.meanAnomaly0, o.meanMotion, e.eccentricity, a, e.eccentricity,
                  a * o.p.x, e.eccentricity, b * o.q.x,
                  a * o.p.y, e.eccentricity, b * o.q.y,
                  a * o.p.z, e.eccentricity, b * o.q.z);
    return text;
}

std::string SpinEquation(const SpinModel& s) {
    char text[256];
    std::snprintf(text, sizeof(text), "  giro(t) = %.6f + %.9e * t   [rad], eje (%.4f, %.4f, %.4f)\n", s.phase, s.rate,
                  s.axis.x, s.axis.y, s.axis.z);
    return text;
}

}  // namespace eps
