// Math3D.h - Algebra lineal del motor.
//
// Convenciones (las mismas de GRAFICOS::CalcularProyeccion):
//   * Mundo y vista: +X derecha, +Y abajo, +Z adelante.
//   * Vectores columna: v' = M * v. Las transformaciones se leen de derecha a izquierda.
//   * Mat4T::m[columna][fila]. Es el orden "column_major" que HLSL usa por defecto.
//
// Una rotacion o escala es una transformacion lineal. Una traslacion NO lo es: es afin.
// Por eso se usan matrices 4x4 con coordenadas homogeneas (x, y, z, 1).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace eps {

inline constexpr double kPi = 3.14159265358979323846;

template <class T>
struct Vec3T {
    T x{}, y{}, z{};

    constexpr Vec3T() = default;
    constexpr Vec3T(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}
    template <class U>
    constexpr explicit Vec3T(const Vec3T<U>& o)
        : x(static_cast<T>(o.x)), y(static_cast<T>(o.y)), z(static_cast<T>(o.z)) {}

    constexpr Vec3T operator+(const Vec3T& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3T operator-(const Vec3T& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3T operator-() const { return {-x, -y, -z}; }
    constexpr Vec3T operator*(T s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3T operator/(T s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3T& operator+=(const Vec3T& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

template <class T> constexpr T Dot(const Vec3T<T>& a, const Vec3T<T>& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
template <class T> constexpr Vec3T<T> Cross(const Vec3T<T>& a, const Vec3T<T>& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <class T> T Length(const Vec3T<T>& v) { return std::sqrt(Dot(v, v)); }
template <class T> Vec3T<T> Normalize(const Vec3T<T>& v) {
    const T len = Length(v);
    return len > T(0) ? v / len : Vec3T<T>{};
}

template <class T>
struct Vec4T {
    T x{}, y{}, z{}, w{};
};

template <class T>
struct Mat4T {
    T m[4][4]{};  // m[columna][fila]

    static constexpr Mat4T Identity() {
        Mat4T r;
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = T(1);
        return r;
    }

    template <class U>
    static constexpr Mat4T Cast(const Mat4T<U>& o) {
        Mat4T r;
        for (int c = 0; c < 4; ++c)
            for (int f = 0; f < 4; ++f) r.m[c][f] = static_cast<T>(o.m[c][f]);
        return r;
    }

    constexpr Mat4T operator*(const Mat4T& b) const {
        Mat4T r;
        for (int c = 0; c < 4; ++c)
            for (int f = 0; f < 4; ++f)
                for (int k = 0; k < 4; ++k) r.m[c][f] += m[k][f] * b.m[c][k];
        return r;
    }

    constexpr Vec4T<T> operator*(const Vec4T<T>& v) const {
        return {m[0][0] * v.x + m[1][0] * v.y + m[2][0] * v.z + m[3][0] * v.w,
                m[0][1] * v.x + m[1][1] * v.y + m[2][1] * v.z + m[3][1] * v.w,
                m[0][2] * v.x + m[1][2] * v.y + m[2][2] * v.z + m[3][2] * v.w,
                m[0][3] * v.x + m[1][3] * v.y + m[2][3] * v.z + m[3][3] * v.w};
    }

    // Punto: w = 1, le afecta la traslacion.
    constexpr Vec3T<T> TransformPoint(const Vec3T<T>& p) const {
        const Vec4T<T> r = *this * Vec4T<T>{p.x, p.y, p.z, T(1)};
        return {r.x, r.y, r.z};
    }
    // Direccion: w = 0, solo la parte lineal (3x3).
    constexpr Vec3T<T> TransformDirection(const Vec3T<T>& d) const {
        const Vec4T<T> r = *this * Vec4T<T>{d.x, d.y, d.z, T(0)};
        return {r.x, r.y, r.z};
    }

    constexpr Vec3T<T> Translation() const { return {m[3][0], m[3][1], m[3][2]}; }
};

using Vec3f = Vec3T<float>;
using Vec3d = Vec3T<double>;
using Vec4f = Vec4T<float>;
using Mat4f = Mat4T<float>;
using Mat4d = Mat4T<double>;

// --- Matrices elementales -------------------------------------------------

template <class T> constexpr Mat4T<T> Translation(const Vec3T<T>& t) {
    Mat4T<T> r = Mat4T<T>::Identity();
    r.m[3][0] = t.x; r.m[3][1] = t.y; r.m[3][2] = t.z;
    return r;
}

template <class T> constexpr Mat4T<T> Scaling(const Vec3T<T>& s) {
    Mat4T<T> r;
    r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z; r.m[3][3] = T(1);
    return r;
}

// y' = y cos(a) - z sin(a);  z' = y sin(a) + z cos(a)
template <class T> Mat4T<T> RotationX(T a) {
    const T c = std::cos(a), s = std::sin(a);
    Mat4T<T> r = Mat4T<T>::Identity();
    r.m[1][1] = c; r.m[2][1] = -s;
    r.m[1][2] = s; r.m[2][2] = c;
    return r;
}

// x' = x cos(a) + z sin(a);  z' = -x sin(a) + z cos(a)
template <class T> Mat4T<T> RotationY(T a) {
    const T c = std::cos(a), s = std::sin(a);
    Mat4T<T> r = Mat4T<T>::Identity();
    r.m[0][0] = c;  r.m[2][0] = s;
    r.m[0][2] = -s; r.m[2][2] = c;
    return r;
}

// x' = x cos(a) - y sin(a);  y' = x sin(a) + y cos(a)
template <class T> Mat4T<T> RotationZ(T a) {
    const T c = std::cos(a), s = std::sin(a);
    Mat4T<T> r = Mat4T<T>::Identity();
    r.m[0][0] = c; r.m[1][0] = -s;
    r.m[0][1] = s; r.m[1][1] = c;
    return r;
}

// Inversa de una matriz afin (ultima fila = 0 0 0 1): invierte el bloque lineal 3x3
// con cofactores y aplica -inv(A) * t. Devuelve la identidad si no es invertible.
template <class T> Mat4T<T> InverseAffine(const Mat4T<T>& a) {
    const Vec3T<T> c0{a.m[0][0], a.m[0][1], a.m[0][2]};
    const Vec3T<T> c1{a.m[1][0], a.m[1][1], a.m[1][2]};
    const Vec3T<T> c2{a.m[2][0], a.m[2][1], a.m[2][2]};
    const Vec3T<T> r0 = Cross(c1, c2);
    const Vec3T<T> r1 = Cross(c2, c0);
    const Vec3T<T> r2 = Cross(c0, c1);
    const T det = Dot(c0, r0);
    if (det == T(0)) return Mat4T<T>::Identity();
    const T k = T(1) / det;

    Mat4T<T> r = Mat4T<T>::Identity();
    r.m[0][0] = r0.x * k; r.m[1][0] = r0.y * k; r.m[2][0] = r0.z * k;
    r.m[0][1] = r1.x * k; r.m[1][1] = r1.y * k; r.m[2][1] = r1.z * k;
    r.m[0][2] = r2.x * k; r.m[1][2] = r2.y * k; r.m[2][2] = r2.z * k;
    const Vec3T<T> t = r.TransformDirection(a.Translation());
    r.m[3][0] = -t.x; r.m[3][1] = -t.y; r.m[3][2] = -t.z;
    return r;
}

// Proyeccion perspectiva con Z invertida y plano lejano infinito.
//   focalPx: distancia focal en pixeles. Es la variable "fov" del juego original.
//   Resultado: profundidad = near / z (1 en el plano cercano, tiende a 0 al infinito).
//   NDC: X a la derecha, Y arriba, Z en [0, 1], como Direct3D 12 y SDL_GPU.
//   El signo negativo de Y convierte la vista (+Y abajo) a NDC (+Y arriba).
//
// En el pipeline de GPU usa comparacion GREATER y limpia el depth buffer a 0.
inline Mat4f PerspectiveReverseZ(float focalPx, float width, float height, float nearZ) {
    Mat4f p;
    p.m[0][0] = 2.0f * focalPx / width;
    p.m[1][1] = -2.0f * focalPx / height;
    p.m[3][2] = nearZ;
    p.m[2][3] = 1.0f;
    return p;
}

// --- Cuaterniones ---------------------------------------------------------
// Evitan el bloqueo de cardan de yaw/pitch/roll y son la base para vuelo libre.

template <class T>
struct QuatT {
    T x{}, y{}, z{}, w{1};

    template <class U>
    static constexpr QuatT Cast(const QuatT<U>& o) {
        QuatT r;
        r.x = static_cast<T>(o.x); r.y = static_cast<T>(o.y);
        r.z = static_cast<T>(o.z); r.w = static_cast<T>(o.w);
        return r;
    }

    static QuatT FromAxisAngle(const Vec3T<T>& axis, T angle) {
        const Vec3T<T> n = Normalize(axis);
        const T s = std::sin(angle * T(0.5));
        QuatT r;
        r.x = n.x * s; r.y = n.y * s; r.z = n.z * s; r.w = std::cos(angle * T(0.5));
        return r;
    }

    constexpr QuatT operator*(const QuatT& b) const {
        QuatT r;
        r.x = w * b.x + x * b.w + y * b.z - z * b.y;
        r.y = w * b.y - x * b.z + y * b.w + z * b.x;
        r.z = w * b.z + x * b.y - y * b.x + z * b.w;
        r.w = w * b.w - x * b.x - y * b.y - z * b.z;
        return r;
    }

    constexpr QuatT Conjugate() const {
        QuatT r;
        r.x = -x; r.y = -y; r.z = -z; r.w = w;
        return r;
    }

    QuatT Normalized() const {
        const T len = std::sqrt(x * x + y * y + z * z + w * w);
        QuatT r;
        r.x = x / len; r.y = y / len; r.z = z / len; r.w = w / len;
        return r;
    }

    constexpr Mat4T<T> ToMatrix() const {
        Mat4T<T> r = Mat4T<T>::Identity();
        const T xx = x * x, yy = y * y, zz = z * z;
        const T xy = x * y, xz = x * z, yz = y * z;
        const T wx = w * x, wy = w * y, wz = w * z;
        r.m[0][0] = T(1) - T(2) * (yy + zz);
        r.m[0][1] = T(2) * (xy + wz);
        r.m[0][2] = T(2) * (xz - wy);
        r.m[1][0] = T(2) * (xy - wz);
        r.m[1][1] = T(1) - T(2) * (xx + zz);
        r.m[1][2] = T(2) * (yz + wx);
        r.m[2][0] = T(2) * (xz + wy);
        r.m[2][1] = T(2) * (yz - wx);
        r.m[2][2] = T(1) - T(2) * (xx + yy);
        return r;
    }
};

using Quatf = QuatT<float>;
using Quatd = QuatT<double>;

}  // namespace eps
