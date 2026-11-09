// Camera.h - Sustituye a GRAFICOS::CalcularProyeccion y MenuManager::proyectar3D.
#pragma once

#include <array>
#include <cstddef>

#include "epsilon/Math3D.h"

namespace eps {

// Equivalente a PuntoProyectado (x, y, profundidad, visible).
struct ScreenPoint {
    float x = 0, y = 0, depth = 0;
    bool visible = false;
};

// Planos ax + by + cz + d >= 0, relativos a la camara (mundo sin traslacion).
struct Frustum {
    struct Plane { Vec3f normal; float d = 0; };
    std::array<Plane, 5> planes;  // izquierda, derecha, abajo, arriba, cerca

    bool SphereVisible(const Vec3f& center, float radius) const {
        for (const Plane& p : planes)
            if (Dot(p.normal, center) + p.d < -radius) return false;
        return true;
    }
};

class Camera {
public:
    // focalPx = la variable "fov" del juego; alli vale ancho / 2, o sea 90 grados horizontales.
    void SetViewport(int width, int height, float focalPx, float nearZ = 1.0f);

    void SetPosition(const Vec3d& position) { position_ = position; }
    const Vec3d& Position() const { return position_; }

    void SetOrientation(const Quatf& q) { orientation_ = q.Normalized(); }
    const Quatf& Orientation() const { return orientation_; }

    // Convencion del motor: +yaw gira a la derecha, +pitch mira hacia abajo, +roll
    // gira la imagen en sentido antihorario. Coincide con el fondo y los enemigos del juego.
    void SetYawPitchRoll(float yaw, float pitch, float roll);

    // Reproduce exactamente CalcularProyeccion, donde los signos de yaw y pitch estan invertidos.
    void SetLegacyAngles(float yaw, float pitch, float roll) { SetYawPitchRoll(-yaw, -pitch, roll); }

    Vec3f Forward() const { return orientation_.ToMatrix().TransformDirection({0, 0, 1}); }
    Vec3f Right() const { return orientation_.ToMatrix().TransformDirection({1, 0, 0}); }
    Vec3f Down() const { return orientation_.ToMatrix().TransformDirection({0, 1, 0}); }

    // Rotacion mundo -> vista. No incluye traslacion: el motor resta la posicion en double.
    Mat4f ViewRotation() const { return orientation_.Conjugate().ToMatrix(); }
    Mat4f Projection() const {
        return PerspectiveReverseZ(focalPx_, static_cast<float>(width_), static_cast<float>(height_), nearZ_);
    }

    Vec3f ToCameraRelative(const Vec3d& world) const { return Vec3f(world - position_); }
    Vec3f ToViewSpace(const Vec3d& world) const {
        return ViewRotation().TransformDirection(ToCameraRelative(world));
    }

    ScreenPoint WorldToScreen(const Vec3d& world) const;
    Frustum CameraRelativeFrustum() const;

    int Width() const { return width_; }
    int Height() const { return height_; }
    float FocalPx() const { return focalPx_; }
    float NearZ() const { return nearZ_; }

private:
    Vec3d position_;
    Quatf orientation_;
    int width_ = 1920, height_ = 1080;
    float focalPx_ = 960.0f, nearZ_ = 1.0f;
};

// Tamano en pantalla de un objeto: diametro * focal / z, limitado para que siga siendo visible.
inline float PixelSize(float worldSize, float viewZ, float focalPx, float minPx, float maxPx) {
    return std::clamp(worldSize * focalPx / viewZ, minPx, maxPx);
}

// Radio aparente de una esfera vista desde una distancia a su centro. Cerca de la superficie
// supera a radio * focal / distancia porque la silueta es la de un cono tangente.
inline float SilhouettePixels(float radius, float distance, float focalPx) {
    const float sine = std::min(radius / distance, 0.9999f);
    return focalPx * sine / std::sqrt(1.0f - sine * sine);
}

// Direccion de un objetivo a partir de sus angulos (DatosCombate::enemyYaw / enemyPitch).
inline Vec3f DirectionFromYawPitch(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp};
}

// Sustituye la deteccion de sistemaApuntado, que estaba mezclada con el dibujado.
// Es de solo lectura y debe llamarse desde la simulacion.
// Devuelve el indice mas cercano al centro de la pantalla, o -1 si ninguno esta a menos de maxPixels.
int PickNearestToCenter(const Camera& camera, const Vec3d* positions, std::size_t count,
                        float maxPixels = 150.0f, float marginPixels = 200.0f);

// Matriz ortografica del mapa tactico (MenuManager::proyectar3D).
// Da x, y en pixeles; z es la profundidad sin zoom.
inline Mat4f TacticalMapMatrix(float rotXDeg, float rotYDeg, float zoom, float centerX, float centerY) {
    const float toRad = static_cast<float>(kPi) / 180.0f;
    return Translation(Vec3f{centerX, centerY, 0.0f}) * Scaling(Vec3f{zoom, zoom, 1.0f}) *
           RotationX(rotXDeg * toRad) * RotationY(-rotYDeg * toRad);
}

}  // namespace eps
