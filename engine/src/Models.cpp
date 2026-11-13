#include "epsilon/Models.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <stdexcept>
#include <tuple>

#if defined(EPSILON_HAS_GLTF)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#endif

namespace eps {

namespace {

constexpr float kTau = 6.2831853f;

void Triangle(MeshData& m, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
}

// Cuadrilatero a, b, c, d en orden tal que (b - a) x (c - a) apunta hacia fuera.
void Quad(MeshData& m, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    Triangle(m, a, b, c);
    Triangle(m, a, c, d);
}

float Hash3(int x, int y, int z, std::uint32_t seed) {
    std::uint32_t h = seed + static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(y) * 668265263u + static_cast<std::uint32_t>(z) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xFFFFFF) / 16777215.0f;
}

float SmoothNoise(const Vec3f& p, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(p.x)), iy = static_cast<int>(std::floor(p.y)), iz = static_cast<int>(std::floor(p.z));
    const float fx = p.x - static_cast<float>(ix), fy = p.y - static_cast<float>(iy), fz = p.z - static_cast<float>(iz);
    const auto fade = [](float t) { return t * t * (3.0f - 2.0f * t); };
    const float sx = fade(fx), sy = fade(fy), sz = fade(fz);
    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float x00 = lerp(Hash3(ix, iy, iz, seed), Hash3(ix + 1, iy, iz, seed), sx);
    const float x10 = lerp(Hash3(ix, iy + 1, iz, seed), Hash3(ix + 1, iy + 1, iz, seed), sx);
    const float x01 = lerp(Hash3(ix, iy, iz + 1, seed), Hash3(ix + 1, iy, iz + 1, seed), sx);
    const float x11 = lerp(Hash3(ix, iy + 1, iz + 1, seed), Hash3(ix + 1, iy + 1, iz + 1, seed), sx);
    return lerp(lerp(x00, x10, sy), lerp(x01, x11, sy), sz) * 2.0f - 1.0f;
}

}  // namespace

void ComputeNormals(MeshData& mesh) {
    for (MeshVertex& v : mesh.vertices) v.normal = {};
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        MeshVertex& a = mesh.vertices[mesh.indices[i]];
        MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
        MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const Vec3f n = Cross(b.position - a.position, c.position - a.position);
        a.normal = a.normal + n;
        b.normal = b.normal + n;
        c.normal = c.normal + n;
    }
    for (MeshVertex& v : mesh.vertices) {
        if (Dot(v.normal, v.normal) > 0.0f) v.normal = Normalize(v.normal);
        else v.normal = {0, -1, 0};
    }
}

MeshData MakeBox(float width, float height, float depth) {
    const Vec3f half{width * 0.5f, height * 0.5f, depth * 0.5f};
    const struct { Vec3f n, u, v; } faces[] = {
        {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}},  {{-1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},  {{0, -1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, 0, 1}, {-1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}},
    };
    const auto scaled = [&](const Vec3f& a) { return Vec3f{a.x * half.x, a.y * half.y, a.z * half.z}; };

    MeshData m;
    for (const auto& f : faces) {
        const auto first = static_cast<std::uint32_t>(m.vertices.size());
        for (const auto [su, sv] : {std::pair{-1.0f, -1.0f}, std::pair{1.0f, -1.0f}, std::pair{1.0f, 1.0f}, std::pair{-1.0f, 1.0f}}) {
            MeshVertex v;
            v.position = scaled(f.n + f.u * su + f.v * sv);
            v.normal = f.n;
            v.u = su * 0.5f + 0.5f;
            v.v = sv * 0.5f + 0.5f;
            m.vertices.push_back(v);
        }
        // El sentido depende de la mano de la base de cada cara.
        const Vec3f turn = Cross(f.u, f.v);
        if (Dot(turn, f.n) > 0.0f) Quad(m, first, first + 1, first + 2, first + 3);
        else Quad(m, first, first + 3, first + 2, first + 1);
    }
    return m;
}

MeshData MakeCylinder(float radius, float height, int segments) {
    MeshData m;
    const float half = height * 0.5f;
    const auto ring = [&](float y, float ny, float v) {
        for (int i = 0; i <= segments; ++i) {
            const float a = kTau * static_cast<float>(i) / static_cast<float>(segments);
            MeshVertex p;
            p.position = {std::cos(a) * radius, y, std::sin(a) * radius};
            p.normal = ny != 0.0f ? Vec3f{0, ny, 0} : Vec3f{std::cos(a), 0, std::sin(a)};
            p.u = static_cast<float>(i) / static_cast<float>(segments) * 4.0f;
            p.v = v;
            m.vertices.push_back(p);
        }
    };
    const auto side = static_cast<std::uint32_t>(segments + 1);
    ring(-half, 0.0f, 0.0f);
    ring(half, 0.0f, 1.0f);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(segments); ++i) Quad(m, i, i + side, i + side + 1, i + 1);

    for (const float sign : {-1.0f, 1.0f}) {
        const auto first = static_cast<std::uint32_t>(m.vertices.size());
        ring(sign * half, sign, 0.5f);
        MeshVertex center;
        center.position = {0, sign * half, 0};
        center.normal = {0, sign, 0};
        center.u = center.v = 0.5f;
        const auto c = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back(center);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(segments); ++i) {
            if (sign < 0.0f) Triangle(m, c, first + i, first + i + 1);
            else Triangle(m, c, first + i + 1, first + i);
        }
    }
    return m;
}

MeshData MakeTorus(float radius, float tube, int segments, int sides) {
    MeshData m;
    for (int i = 0; i <= segments; ++i) {
        const float a = kTau * static_cast<float>(i) / static_cast<float>(segments);
        const Vec3f out{std::cos(a), 0, std::sin(a)};
        for (int j = 0; j <= sides; ++j) {
            const float b = kTau * static_cast<float>(j) / static_cast<float>(sides);
            const Vec3f n = out * std::cos(b) + Vec3f{0, std::sin(b), 0};
            MeshVertex v;
            v.position = out * radius + n * tube;
            v.normal = n;
            v.u = static_cast<float>(i) / static_cast<float>(segments) * 8.0f;
            v.v = static_cast<float>(j) / static_cast<float>(sides);
            m.vertices.push_back(v);
        }
    }
    const auto row = static_cast<std::uint32_t>(sides + 1);
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(segments); ++i)
        for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(sides); ++j) {
            const std::uint32_t a = i * row + j;
            Quad(m, a, a + 1, a + row + 1, a + row);
        }
    return m;
}

MeshData MakeSphere(float radius, int segments) {
    MeshData m;
    const int rings = std::max(segments / 2, 3);
    for (int y = 0; y <= rings; ++y) {
        const float t = static_cast<float>(kPi) * static_cast<float>(y) / static_cast<float>(rings);
        for (int x = 0; x <= segments; ++x) {
            const float a = kTau * static_cast<float>(x) / static_cast<float>(segments);
            MeshVertex v;
            v.normal = {std::sin(t) * std::cos(a), -std::cos(t), std::sin(t) * std::sin(a)};
            v.position = v.normal * radius;
            v.u = static_cast<float>(x) / static_cast<float>(segments);
            v.v = static_cast<float>(y) / static_cast<float>(rings);
            m.vertices.push_back(v);
        }
    }
    const auto row = static_cast<std::uint32_t>(segments + 1);
    for (std::uint32_t y = 0; y < static_cast<std::uint32_t>(rings); ++y)
        for (std::uint32_t x = 0; x < static_cast<std::uint32_t>(segments); ++x) {
            const std::uint32_t a = y * row + x;
            Quad(m, a, a + row, a + row + 1, a + 1);
        }
    return m;
}

MeshData MakeQuad(float width, float height) {
    MeshData m;
    const float x = width * 0.5f, y = height * 0.5f;
    const Vec3f n{0, 0, -1};
    m.vertices.push_back({{-x, -y, 0}, n, 0, 0});
    m.vertices.push_back({{x, -y, 0}, n, 1, 0});
    m.vertices.push_back({{x, y, 0}, n, 1, 1});
    m.vertices.push_back({{-x, y, 0}, n, 0, 1});
    Quad(m, 0, 3, 2, 1);
    return m;
}

MeshData MakeAsteroid(std::uint32_t seed, int subdivisions) {
    // Icosaedro subdividido: reparte los vertices mejor que una esfera por paralelos.
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3f> points = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (Vec3f& p : points) p = Normalize(p);
    std::vector<std::uint32_t> faces = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
                                        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};

    for (int level = 0; level < subdivisions; ++level) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> middle;
        const auto split = [&](std::uint32_t a, std::uint32_t b) {
            const auto key = std::minmax(a, b);
            const auto found = middle.find({key.first, key.second});
            if (found != middle.end()) return found->second;
            const auto index = static_cast<std::uint32_t>(points.size());
            points.push_back(Normalize(points[a] + points[b]));
            middle[{key.first, key.second}] = index;
            return index;
        };
        std::vector<std::uint32_t> next;
        next.reserve(faces.size() * 4);
        for (std::size_t i = 0; i < faces.size(); i += 3) {
            const std::uint32_t a = faces[i], b = faces[i + 1], c = faces[i + 2];
            const std::uint32_t ab = split(a, b), bc = split(b, c), ca = split(c, a);
            for (const std::uint32_t k : {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca}) next.push_back(k);
        }
        faces = std::move(next);
    }

    std::mt19937 rng(seed);
    const auto unit = [&rng] { return static_cast<float>(rng()) / 4294967295.0f; };
    const Vec3f stretch{0.75f + 0.45f * unit(), 0.7f + 0.35f * unit(), 0.8f + 0.4f * unit()};

    // Crateres: hoyos con borde, en direcciones al azar.
    struct Crater { Vec3f center; float size; };
    std::vector<Crater> craters(14);
    for (Crater& c : craters) {
        c.center = Normalize(Vec3f{unit() * 2.0f - 1.0f, unit() * 2.0f - 1.0f, unit() * 2.0f - 1.0f});
        c.size = 0.15f + 0.3f * unit() * unit();
    }

    MeshData m;
    m.vertices.resize(points.size());
    for (std::size_t i = 0; i < points.size(); ++i) {
        const Vec3f p = points[i];
        float h = 1.0f + 0.30f * SmoothNoise(p * 1.4f, seed) + 0.14f * SmoothNoise(p * 3.1f, seed + 1) + 0.06f * SmoothNoise(p * 6.7f, seed + 2);
        for (const Crater& c : craters) {
            const float d = Length(p - c.center) / c.size;
            if (d < 1.0f) h -= c.size * 0.45f * (1.0f - d * d);
            else if (d < 1.3f) h += c.size * 0.10f * (1.3f - d) / 0.3f;
        }
        m.vertices[i].position = Vec3f{p.x * stretch.x, p.y * stretch.y, p.z * stretch.z} * std::max(h, 0.35f);
        m.vertices[i].u = p.x * 0.5f + 0.5f;
        m.vertices[i].v = p.y * 0.5f + 0.5f;
    }
    m.indices = std::move(faces);
    ComputeNormals(m);
    return m;
}

void AppendMesh(MeshData& to, const MeshData& from, const Vec3f& position, const Quatf& rotation, float scale) {
    const auto first = static_cast<std::uint32_t>(to.vertices.size());
    const Mat4f r = rotation.ToMatrix();
    for (const MeshVertex& v : from.vertices) {
        MeshVertex out = v;
        out.position = r.TransformDirection(v.position * scale) + position;
        out.normal = r.TransformDirection(v.normal);
        to.vertices.push_back(out);
    }
    for (const std::uint32_t i : from.indices) to.indices.push_back(first + i);
}

// Refinamiento rojo-verde: en cada pasada se marcan las aristas largas y cada triangulo se parte
// segun cuantas tenga marcadas (2, 3 o 4 hijos). Una arista compartida se parte en el mismo punto
// por los dos lados, asi que no quedan grietas.
void SubdivideLongEdges(MeshData& mesh, float maxEdge, std::size_t maxTriangles) {
    if (maxEdge <= 0.0f) return;
    const float limit = maxEdge * maxEdge;
    for (int pass = 0; pass < 16; ++pass) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> middle;
        const auto key = [](std::uint32_t a, std::uint32_t b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); };
        const auto longEdge = [&](std::uint32_t a, std::uint32_t b) {
            const Vec3f d = mesh.vertices[a].position - mesh.vertices[b].position;
            return Dot(d, d) > limit;
        };
        const std::size_t triangles = mesh.indices.size() / 3;
        bool any = false;
        for (std::size_t t = 0; t < triangles && !any; ++t)
            for (int e = 0; e < 3 && !any; ++e) any = longEdge(mesh.indices[t * 3 + e], mesh.indices[t * 3 + (e + 1) % 3]);
        if (!any || triangles * 2 > maxTriangles) return;

        const auto split = [&](std::uint32_t a, std::uint32_t b) -> std::uint32_t {
            const auto k = key(a, b);
            const auto found = middle.find(k);
            if (found != middle.end()) return found->second;
            const MeshVertex& va = mesh.vertices[a];
            const MeshVertex& vb = mesh.vertices[b];
            MeshVertex v;
            v.position = (va.position + vb.position) * 0.5f;
            const Vec3f n = va.normal + vb.normal;
            v.normal = Dot(n, n) > 1e-12f ? Normalize(n) : va.normal;
            v.u = (va.u + vb.u) * 0.5f;
            v.v = (va.v + vb.v) * 0.5f;
            const auto index = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(v);
            middle[k] = index;
            return index;
        };
        std::vector<std::uint32_t> out;
        out.reserve(mesh.indices.size() * 2);
        const auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) { out.insert(out.end(), {a, b, c}); };
        for (std::size_t t = 0; t < triangles; ++t) {
            const std::uint32_t v[3] = {mesh.indices[t * 3], mesh.indices[t * 3 + 1], mesh.indices[t * 3 + 2]};
            bool cut[3];
            int cuts = 0;
            for (int e = 0; e < 3; ++e) cuts += (cut[e] = longEdge(v[e], v[(e + 1) % 3])) ? 1 : 0;
            if (cuts == 0) {
                tri(v[0], v[1], v[2]);
            } else if (cuts == 3) {
                const std::uint32_t m0 = split(v[0], v[1]), m1 = split(v[1], v[2]), m2 = split(v[2], v[0]);
                tri(v[0], m0, m2);
                tri(m0, v[1], m1);
                tri(m2, m1, v[2]);
                tri(m0, m1, m2);
            } else if (cuts == 1) {
                const int e = cut[0] ? 0 : cut[1] ? 1 : 2;
                const std::uint32_t a = v[e], b = v[(e + 1) % 3], c = v[(e + 2) % 3], m = split(a, b);
                tri(a, m, c);
                tri(m, b, c);
            } else {
                // Dos aristas: la que queda entera es e; se parten las otras dos.
                const int e = !cut[0] ? 0 : !cut[1] ? 1 : 2;
                const std::uint32_t a = v[e], b = v[(e + 1) % 3], c = v[(e + 2) % 3];
                const std::uint32_t mbc = split(b, c), mca = split(c, a);
                tri(a, b, mbc);
                tri(a, mbc, mca);
                tri(mca, mbc, c);
            }
        }
        mesh.indices.swap(out);
    }
}

void ConvertYUpToEngine(MeshData& mesh) {
    for (MeshVertex& v : mesh.vertices) {
        v.position = {v.position.x, -v.position.y, -v.position.z};
        v.normal = {v.normal.x, -v.normal.y, -v.normal.z};
    }
}

bool ParseObj(const std::string& text, MeshData& out, std::string* error) {
    std::vector<Vec3f> positions, normals;
    std::vector<std::pair<float, float>> uvs;
    std::map<std::tuple<int, int, int>, std::uint32_t> unique;
    bool hasNormals = true;
    out = {};

    const auto resolve = [](int index, std::size_t count) {
        if (index > 0 && static_cast<std::size_t>(index) <= count) return index - 1;
        if (index < 0 && static_cast<std::size_t>(-index) <= count) return static_cast<int>(count) + index;
        return -1;
    };

    std::istringstream lines(text);
    std::string line;
    int lineNumber = 0;
    while (std::getline(lines, line)) {
        ++lineNumber;
        std::istringstream row(line);
        std::string tag;
        row >> tag;
        if (tag == "v") {
            Vec3f p;
            row >> p.x >> p.y >> p.z;
            positions.push_back(p);
        } else if (tag == "vn") {
            Vec3f n;
            row >> n.x >> n.y >> n.z;
            normals.push_back(n);
        } else if (tag == "vt") {
            float u = 0, v = 0;
            row >> u >> v;
            uvs.emplace_back(u, 1.0f - v);  // OBJ cuenta V desde abajo
        } else if (tag == "f") {
            std::vector<std::uint32_t> polygon;
            std::string corner;
            while (row >> corner) {
                int p = 0, t = 0, n = 0;
                const std::size_t a = corner.find('/');
                p = std::atoi(corner.c_str());
                if (a != std::string::npos) {
                    const std::size_t b = corner.find('/', a + 1);
                    if (b != a + 1) t = std::atoi(corner.c_str() + a + 1);
                    if (b != std::string::npos) n = std::atoi(corner.c_str() + b + 1);
                }
                const int ip = resolve(p, positions.size());
                const int it = t != 0 ? resolve(t, uvs.size()) : -1;
                const int in = n != 0 ? resolve(n, normals.size()) : -1;
                if (ip < 0) {
                    if (error) *error = "Indice invalido en la linea " + std::to_string(lineNumber);
                    return false;
                }
                hasNormals = hasNormals && in >= 0;
                const auto key = std::make_tuple(ip, it, in);
                const auto found = unique.find(key);
                if (found != unique.end()) {
                    polygon.push_back(found->second);
                    continue;
                }
                MeshVertex v;
                v.position = positions[static_cast<std::size_t>(ip)];
                if (in >= 0) v.normal = Normalize(normals[static_cast<std::size_t>(in)]);
                if (it >= 0) std::tie(v.u, v.v) = uvs[static_cast<std::size_t>(it)];
                const auto index = static_cast<std::uint32_t>(out.vertices.size());
                out.vertices.push_back(v);
                unique[key] = index;
                polygon.push_back(index);
            }
            for (std::size_t i = 1; i + 1 < polygon.size(); ++i) Triangle(out, polygon[0], polygon[i], polygon[i + 1]);
        }
    }
    if (out.indices.empty()) {
        if (error) *error = "El archivo no contiene caras";
        return false;
    }
    if (!hasNormals) ComputeNormals(out);
    ConvertYUpToEngine(out);
    return true;
}

bool LoadObj(const std::string& path, MeshData& out, std::string* error) {
    std::ifstream file(path);
    if (!file) {
        if (error) *error = "No se pudo abrir " + path;
        return false;
    }
    std::stringstream text;
    text << file.rdbuf();
    return ParseObj(text.str(), out, error);
}

#if defined(EPSILON_HAS_GLTF)

bool GltfSupported() { return true; }

bool LoadGltf(const std::string& path, std::vector<ImportedPart>& out, std::string* error) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result == cgltf_result_success) result = cgltf_load_buffers(&options, data, path.c_str());
    if (result == cgltf_result_success) result = cgltf_validate(data);
    if (result != cgltf_result_success) {
        if (error) *error = "cgltf no pudo leer " + path + " (codigo " + std::to_string(static_cast<int>(result)) + ")";
        cgltf_free(data);
        return false;
    }

    const std::size_t slash = path.find_last_of("/\\");
    const std::string folder = slash == std::string::npos ? "" : path.substr(0, slash + 1);

    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        const auto transformPoint = [&](const float* p) {
            return Vec3f{world[0] * p[0] + world[4] * p[1] + world[8] * p[2] + world[12],
                         world[1] * p[0] + world[5] * p[1] + world[9] * p[2] + world[13],
                         world[2] * p[0] + world[6] * p[1] + world[10] * p[2] + world[14]};
        };
        const auto transformNormal = [&](const float* p) {
            return Normalize(Vec3f{world[0] * p[0] + world[4] * p[1] + world[8] * p[2], world[1] * p[0] + world[5] * p[1] + world[9] * p[2],
                                   world[2] * p[0] + world[6] * p[1] + world[10] * p[2]});
        };

        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            const cgltf_primitive& primitive = node.mesh->primitives[pi];
            if (primitive.type != cgltf_primitive_type_triangles) continue;

            ImportedPart part;
            part.name = node.name ? node.name : (node.mesh->name ? node.mesh->name : "parte");
            if (node.mesh->primitives_count > 1) part.name += "_" + std::to_string(pi);

            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* uvs = nullptr;
            for (cgltf_size a = 0; a < primitive.attributes_count; ++a) {
                const cgltf_attribute& attribute = primitive.attributes[a];
                if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
                else if (attribute.type == cgltf_attribute_type_normal) normals = attribute.data;
                else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) uvs = attribute.data;
            }
            if (!positions) continue;

            part.mesh.vertices.resize(positions->count);
            for (cgltf_size i = 0; i < positions->count; ++i) {
                float p[3] = {0, 0, 0}, normal[3] = {0, 1, 0}, uv[2] = {0, 0};
                cgltf_accessor_read_float(positions, i, p, 3);
                part.mesh.vertices[i].position = transformPoint(p);
                if (normals) {
                    cgltf_accessor_read_float(normals, i, normal, 3);
                    part.mesh.vertices[i].normal = transformNormal(normal);
                }
                if (uvs) {
                    cgltf_accessor_read_float(uvs, i, uv, 2);
                    part.mesh.vertices[i].u = uv[0];
                    part.mesh.vertices[i].v = uv[1];
                }
            }
            if (primitive.indices) {
                part.mesh.indices.resize(primitive.indices->count);
                for (cgltf_size i = 0; i < primitive.indices->count; ++i) part.mesh.indices[i] = static_cast<std::uint32_t>(cgltf_accessor_read_index(primitive.indices, i));
            } else {
                part.mesh.indices.resize(positions->count);
                for (cgltf_size i = 0; i < positions->count; ++i) part.mesh.indices[i] = static_cast<std::uint32_t>(i);
            }
            if (!normals) ComputeNormals(part.mesh);
            ConvertYUpToEngine(part.mesh);

            if (primitive.material && primitive.material->has_pbr_metallic_roughness) {
                const cgltf_pbr_metallic_roughness& pbr = primitive.material->pbr_metallic_roughness;
                part.color = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2], pbr.base_color_factor[3]};
                const cgltf_texture* texture = pbr.base_color_texture.texture;
                if (texture && texture->image && texture->image->uri) part.texturePath = folder + texture->image->uri;
            }
            if (primitive.material) {
                // Blender exporta la intensidad (> 1) aparte, con KHR_materials_emissive_strength.
                const cgltf_material& material = *primitive.material;
                const float factor = std::max({material.emissive_factor[0], material.emissive_factor[1], material.emissive_factor[2]});
                const float strength = material.has_emissive_strength ? material.emissive_strength.emissive_strength : 1.0f;
                part.emissive = factor * strength;
            }
            out.push_back(std::move(part));
        }
    }
    cgltf_free(data);
    if (out.empty()) {
        if (error) *error = "El archivo no contiene mallas de triangulos: " + path;
        return false;
    }
    return true;
}

bool LoadGltfMarkers(const std::string& path, std::vector<ImportedMarker>& out, std::string* error) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result != cgltf_result_success) {
        if (error) *error = "cgltf no pudo leer " + path + " (codigo " + std::to_string(static_cast<int>(result)) + ")";
        cgltf_free(data);
        return false;
    }
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (node.mesh || node.children_count > 0 || !node.name) continue;  // los anclajes son hojas sin malla
        cgltf_float world[16];
        cgltf_node_transform_world(&node, world);
        ImportedMarker m;
        m.name = node.name;
        // Mismo cambio de ejes que las mallas: Y y Z cambian de signo.
        m.position = {world[12], -world[13], -world[14]};
        const Vec3f up{world[4], -world[5], -world[6]};
        m.up = Dot(up, up) > 1e-12f ? Normalize(up) : Vec3f{0, -1, 0};
        out.push_back(m);
    }
    cgltf_free(data);
    return true;
}

namespace {

// Valor de un canal en el instante t: lineal (el giro, por el camino corto), escalon o la parte de
// valor de un spline cubico. count es 3 (traslacion, escala) o 4 (giro).
void SampleChannel(const cgltf_animation_sampler& s, float t, int count, float* out) {
    const cgltf_accessor* input = s.input;
    const cgltf_accessor* output = s.output;
    const cgltf_size n = input->count;
    if (n == 0) return;
    const bool cubic = s.interpolation == cgltf_interpolation_type_cubic_spline;
    const auto value = [&](cgltf_size k, float* v) { cgltf_accessor_read_float(output, cubic ? k * 3 + 1 : k, v, static_cast<cgltf_size>(count)); };
    float t0 = 0, t1 = 0;
    cgltf_accessor_read_float(input, 0, &t0, 1);
    if (t <= t0 || n == 1) return value(0, out);
    cgltf_accessor_read_float(input, n - 1, &t1, 1);
    if (t >= t1) return value(n - 1, out);
    cgltf_size lo = 0, hi = n - 1;  // busqueda binaria del tramo
    while (hi - lo > 1) {
        const cgltf_size mid = (lo + hi) / 2;
        float tm = 0;
        cgltf_accessor_read_float(input, mid, &tm, 1);
        (tm <= t ? lo : hi) = mid;
    }
    float ta = 0, tb = 0, a[4] = {}, b[4] = {};
    cgltf_accessor_read_float(input, lo, &ta, 1);
    cgltf_accessor_read_float(input, hi, &tb, 1);
    value(lo, a);
    if (s.interpolation == cgltf_interpolation_type_step) {
        for (int i = 0; i < count; ++i) out[i] = a[i];
        return;
    }
    value(hi, b);
    const float u = tb > ta ? (t - ta) / (tb - ta) : 0.0f;
    if (count == 4 && a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0.0f)
        for (float& c : b) c = -c;
    float len = 0;
    for (int i = 0; i < count; ++i) {
        out[i] = a[i] + (b[i] - a[i]) * u;
        len += out[i] * out[i];
    }
    if (count == 4 && len > 0.0f)
        for (int i = 0; i < 4; ++i) out[i] /= std::sqrt(len);
}

// Giro de una matriz 3x3 (fila, columna) sin escala.
Quatf QuatOf(const float r[3][3]) {
    Quatf q;
    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.0f) {
        const float s = std::sqrt(trace + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (r[2][1] - r[1][2]) / s;
        q.y = (r[0][2] - r[2][0]) / s;
        q.z = (r[1][0] - r[0][1]) / s;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2.0f;
        q.w = (r[2][1] - r[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (r[0][1] + r[1][0]) / s;
        q.z = (r[0][2] + r[2][0]) / s;
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2.0f;
        q.w = (r[0][2] - r[2][0]) / s;
        q.x = (r[0][1] + r[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (r[1][2] + r[2][1]) / s;
    } else {
        const float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2.0f;
        q.w = (r[1][0] - r[0][1]) / s;
        q.x = (r[0][2] + r[2][0]) / s;
        q.y = (r[1][2] + r[2][1]) / s;
        q.z = 0.25f * s;
    }
    return q.Normalized();
}

// Inversa de una matriz afin de cgltf (columnas), con escala y giro.
void InverseAffine(const float* m, float* out) {
    float a[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) a[r][c] = m[c * 4 + r];
    const float det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    const float k = std::fabs(det) > 1e-20f ? 1.0f / det : 0.0f;
    float inv[3][3];
    inv[0][0] = (a[1][1] * a[2][2] - a[1][2] * a[2][1]) * k;
    inv[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * k;
    inv[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * k;
    inv[1][0] = (a[1][2] * a[2][0] - a[1][0] * a[2][2]) * k;
    inv[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * k;
    inv[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * k;
    inv[2][0] = (a[1][0] * a[2][1] - a[1][1] * a[2][0]) * k;
    inv[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * k;
    inv[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * k;
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) out[c * 4 + r] = inv[r][c];
        out[12 + r] = -(inv[r][0] * m[12] + inv[r][1] * m[13] + inv[r][2] * m[14]);
        out[r * 4 + 3] = 0.0f;
    }
    out[15] = 1.0f;
}

void MultiplyAffine(const float* a, const float* b, float* out) {  // out = a * b, columnas
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = sum;
        }
}

}  // namespace

bool LoadGltfAnimation(const std::string& path, const std::string& name, ModelAnimation& out, double fps, std::string* error) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    if (result == cgltf_result_success) result = cgltf_load_buffers(&options, data, path.c_str());
    if (result != cgltf_result_success) {
        if (error) *error = "cgltf no pudo leer " + path + " (codigo " + std::to_string(static_cast<int>(result)) + ")";
        cgltf_free(data);
        return false;
    }
    const cgltf_animation* anim = nullptr;
    for (cgltf_size i = 0; i < data->animations_count; ++i)
        if (name.empty() || (data->animations[i].name && name == data->animations[i].name)) {
            anim = &data->animations[i];
            break;
        }
    if (!anim) {
        if (error) *error = "sin la animacion \"" + name + "\" en " + path;
        cgltf_free(data);
        return false;
    }

    // Las partes, en el mismo orden que LoadGltf: una por primitiva de triangulos de cada nodo con malla.
    out = ModelAnimation{};
    out.name = anim->name ? anim->name : "";
    std::vector<const cgltf_node*> partNodes;
    for (cgltf_size n = 0; n < data->nodes_count; ++n) {
        const cgltf_node& node = data->nodes[n];
        if (!node.mesh) continue;
        for (cgltf_size pi = 0; pi < node.mesh->primitives_count; ++pi) {
            if (node.mesh->primitives[pi].type != cgltf_primitive_type_triangles) continue;
            std::string partName = node.name ? node.name : (node.mesh->name ? node.mesh->name : "parte");
            if (node.mesh->primitives_count > 1) partName += "_" + std::to_string(pi);
            out.parts.push_back(partName);
            partNodes.push_back(&node);
        }
    }
    std::vector<std::array<float, 16>> restInverse(partNodes.size());
    for (std::size_t p = 0; p < partNodes.size(); ++p) {
        float world[16];
        cgltf_node_transform_world(partNodes[p], world);
        InverseAffine(world, restInverse[p].data());
    }

    float duration = 0;
    for (cgltf_size c = 0; c < anim->samplers_count; ++c) {
        const cgltf_accessor* input = anim->samplers[c].input;
        if (input && input->count > 0) {
            float t = 0;
            cgltf_accessor_read_float(input, input->count - 1, &t, 1);
            duration = std::max(duration, t);
        }
    }
    out.duration = duration;
    const int count = std::max(2, static_cast<int>(std::ceil(duration * fps)) + 1);
    out.step = duration / static_cast<double>(count - 1);
    out.samples.assign(static_cast<std::size_t>(count), std::vector<PartMotion>(partNodes.size()));

    for (int k = 0; k < count; ++k) {
        const float t = static_cast<float>(out.step * k);
        for (cgltf_size c = 0; c < anim->channels_count; ++c) {
            const cgltf_animation_channel& ch = anim->channels[c];
            cgltf_node* node = ch.target_node;
            if (!node || !ch.sampler) continue;
            node->has_matrix = false;
            if (ch.target_path == cgltf_animation_path_type_translation) {
                SampleChannel(*ch.sampler, t, 3, node->translation);
                node->has_translation = true;
            } else if (ch.target_path == cgltf_animation_path_type_rotation) {
                SampleChannel(*ch.sampler, t, 4, node->rotation);
                node->has_rotation = true;
            } else if (ch.target_path == cgltf_animation_path_type_scale) {
                SampleChannel(*ch.sampler, t, 3, node->scale);
                node->has_scale = true;
            }
        }
        for (std::size_t p = 0; p < partNodes.size(); ++p) {
            float world[16], delta[16];
            cgltf_node_transform_world(partNodes[p], world);
            MultiplyAffine(world, restInverse[p].data(), delta);
            // Mismo cambio de ejes que las mallas: C = diag(1, -1, -1), y el delta pasa a C * D * C.
            const float sign[3] = {1.0f, -1.0f, -1.0f};
            float r[3][3];
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col) r[row][col] = delta[col * 4 + row] * sign[row] * sign[col];
            // Quita la escala que pudiera quedar, columna a columna.
            for (int col = 0; col < 3; ++col) {
                const float len = std::sqrt(r[0][col] * r[0][col] + r[1][col] * r[1][col] + r[2][col] * r[2][col]);
                if (len > 1e-9f)
                    for (int row = 0; row < 3; ++row) r[row][col] /= len;
            }
            PartMotion& m = out.samples[static_cast<std::size_t>(k)][p];
            m.rotation = QuatOf(r);
            m.offset = {delta[12], -delta[13], -delta[14]};
        }
    }
    cgltf_free(data);
    return true;
}

#else

bool GltfSupported() { return false; }

bool LoadGltfAnimation(const std::string& path, const std::string&, ModelAnimation&, double, std::string* error) {
    if (error) *error = "Motor compilado sin cgltf: no se puede leer " + path;
    return false;
}

bool LoadGltf(const std::string& path, std::vector<ImportedPart>&, std::string* error) {
    if (error) *error = "Motor compilado sin cgltf: no se puede leer " + path;
    return false;
}

bool LoadGltfMarkers(const std::string& path, std::vector<ImportedMarker>&, std::string* error) {
    if (error) *error = "Motor compilado sin cgltf: no se puede leer " + path;
    return false;
}

#endif

void ModelAnimation::Pose(double t, std::vector<PartMotion>& out) const {
    out.assign(parts.size(), PartMotion{});
    if (samples.empty()) return;
    const double at = step > 0.0 ? std::clamp(t, 0.0, duration) / step : 0.0;
    const std::size_t a = std::min(static_cast<std::size_t>(at), samples.size() - 1);
    const std::size_t b = std::min(a + 1, samples.size() - 1);
    const float u = static_cast<float>(at - static_cast<double>(a));
    for (std::size_t p = 0; p < parts.size(); ++p) {
        const PartMotion& m0 = samples[a][p];
        const PartMotion& m1 = samples[b][p];
        Quatf q1 = m1.rotation;
        if (m0.rotation.x * q1.x + m0.rotation.y * q1.y + m0.rotation.z * q1.z + m0.rotation.w * q1.w < 0.0f) q1 = {-q1.x, -q1.y, -q1.z, -q1.w};
        Quatf q;
        q.x = m0.rotation.x + (q1.x - m0.rotation.x) * u;
        q.y = m0.rotation.y + (q1.y - m0.rotation.y) * u;
        q.z = m0.rotation.z + (q1.z - m0.rotation.z) * u;
        q.w = m0.rotation.w + (q1.w - m0.rotation.w) * u;
        out[p].rotation = q.Normalized();
        out[p].offset = m0.offset + (m1.offset - m0.offset) * u;
    }
}

ModelPart MakeSpritePart(const std::string& name, MeshId unitQuad, TextureId texture, float widthMetres, float heightMetres) {
    ModelPart part;
    part.name = name;
    part.mesh = unitQuad;
    part.texture = texture;
    part.scale = 1.0f;
    part.billboard = true;
    part.spriteWidth = widthMetres;
    part.spriteHeight = heightMetres;
    part.radius = std::sqrt(widthMetres * widthMetres + heightMetres * heightMetres) * 0.5f;
    return part;
}

ModelRef BuildModel(const std::string& name, const std::vector<ImportedPart>& parts, const std::function<MeshId(const MeshData&)>& upload) {
    auto model = std::make_shared<ModelAsset>();
    model->name = name;
    for (const ImportedPart& imported : parts) {
        ModelPart part;
        part.name = imported.name;
        part.mesh = upload(imported.mesh);
        part.color = imported.color;
        part.radius = MeshRadius(imported.mesh);
        model->parts.push_back(std::move(part));
    }
    return model;
}

LocalSpace::InstanceId LocalSpace::Spawn(ModelRef model, const Vec3d& position, const Quatd& rotation, float scale) {
    if (!model) throw std::invalid_argument("LocalSpace::Spawn: modelo nulo");
    InstanceId id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
    } else {
        id = static_cast<InstanceId>(instances_.size());
        instances_.emplace_back();
    }
    Instance& i = instances_[Slot(id)];
    i = {};
    i.parts.resize(model->parts.size());
    for (std::size_t p = 0; p < model->parts.size(); ++p) {
        i.parts[p].color = model->parts[p].color;
        i.parts[p].emissive = model->parts[p].emissive;
    }
    i.model = std::move(model);
    i.position = position;
    i.rotation = rotation;
    i.scale = scale;
    i.used = true;
    return id;
}

void LocalSpace::Remove(InstanceId id) {
    Instance& i = instances_[Slot(id)];
    if (!i.used) return;
    i = {};
    free_.push_back(id);
}

int LocalSpace::InstanceCount() const { return static_cast<int>(instances_.size() - free_.size()); }

bool LocalSpace::SetPartVisible(InstanceId id, const std::string& part, bool visible) {
    Instance& instance = instances_[Slot(id)];
    bool found = false;
    for (std::size_t p = 0; p < instance.parts.size(); ++p) {
        if (instance.model->parts[p].name == part) {
            instance.parts[p].visible = visible;
            found = true;
        }
    }
    return found;
}

bool LocalSpace::SetPartColor(InstanceId id, const std::string& part, const Color& color, float emissive) {
    Instance& instance = instances_[Slot(id)];
    bool found = false;
    for (std::size_t p = 0; p < instance.parts.size(); ++p) {
        if (instance.model->parts[p].name == part) {
            instance.parts[p].color = color;
            instance.parts[p].emissive = emissive;
            found = true;
        }
    }
    return found;
}

const std::string& LocalSpace::PartName(InstanceId id, int part) const { return instances_[Slot(id)].model->parts[static_cast<std::size_t>(part)].name; }

RayHit LocalSpace::Raycast(const Vec3d& origin, const Vec3d& direction, double maxDistance) const {
    RayHit best;
    best.distance = maxDistance;
    const Vec3d d = Normalize(direction);
    for (std::size_t id = 0; id < instances_.size(); ++id) {
        const Instance& instance = instances_[id];
        if (!instance.used || !instance.visible) continue;
        const Mat4d rotation = instance.rotation.ToMatrix();
        for (std::size_t p = 0; p < instance.parts.size(); ++p) {
            if (!instance.parts[p].visible) continue;
            const ModelPart& part = instance.model->parts[p];
            const Vec3d center = instance.position + rotation.TransformDirection(Vec3d(part.position)) * static_cast<double>(instance.scale);
            const double radius = static_cast<double>(part.radius) * instance.scale;
            const Vec3d toCenter = center - origin;
            const double along = Dot(toCenter, d);
            const double miss2 = Dot(toCenter, toCenter) - along * along;
            if (miss2 > radius * radius) continue;
            if (along + radius < 0.0) continue;
            double t = std::max(along - std::sqrt(radius * radius - miss2), 0.0);
            if (part.collision) {
                // El rayo se lleva al espacio de la malla. Moller y Trumbore (1997).
                const Mat4d inverse = (instance.rotation * Quatd::Cast(part.rotation)).Conjugate().ToMatrix();
                const double scale = static_cast<double>(part.scale) * instance.scale;
                const Vec3d localOrigin = inverse.TransformDirection(origin - center) * (1.0 / scale);
                const Vec3d localDirection = inverse.TransformDirection(d);
                const MeshData& mesh = *part.collision;
                double closest = 1.0e30;
                for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
                    const Vec3d a(mesh.vertices[mesh.indices[i]].position);
                    const Vec3d e1 = Vec3d(mesh.vertices[mesh.indices[i + 1]].position) - a;
                    const Vec3d e2 = Vec3d(mesh.vertices[mesh.indices[i + 2]].position) - a;
                    const Vec3d cross = Cross(localDirection, e2);
                    const double det = Dot(e1, cross);
                    if (std::abs(det) < 1e-12) continue;
                    const Vec3d s = localOrigin - a;
                    const double u = Dot(s, cross) / det;
                    if (u < 0.0 || u > 1.0) continue;
                    const Vec3d q = Cross(s, e1);
                    const double v = Dot(localDirection, q) / det;
                    if (v < 0.0 || u + v > 1.0) continue;
                    const double hit = Dot(e2, q) / det;
                    if (hit >= 0.0 && hit < closest) closest = hit;
                }
                if (closest >= 1.0e30) continue;
                t = closest * scale;
            }
            if (t < best.distance) {
                best.distance = t;
                best.instance = static_cast<int>(id);
                best.part = static_cast<int>(p);
            }
        }
    }
    return best;
}

Vec3d LocalSpace::ToWorldKm(const SceneGraph& scene, const Vec3d& localMetres) const {
    const Vec3d base = (anchor != kNoNode ? scene.WorldPosition(anchor) : Vec3d{}) + anchorOffset;
    return base + orientation.ToMatrix().TransformDirection(localMetres) * 0.001;
}

void LocalSpace::Collect(const FrameContext& c, FramePacket& out) const {
    const float focal = c.camera.FocalPx();
    const Mat4d toWorld = orientation.ToMatrix();
    const Vec3d base = (anchor != kNoNode ? c.scene.WorldPosition(anchor) : Vec3d{}) + anchorOffset;
    const Vec3d baseRelative = base - c.cameraPosition;
    const Quatf viewFromWorld = c.camera.Orientation().Conjugate();

    Vec3f lightView = Normalize(c.viewRotation.TransformDirection(Vec3f{0.5f, -0.6f, 0.6f}));
    if (lightNode != kNoNode) lightView = Normalize(c.viewRotation.TransformDirection(Vec3f(c.scene.WorldPosition(lightNode) - base)));

    for (std::size_t id = 0; id < instances_.size(); ++id) {
        const Instance& instance = instances_[id];
        if (!instance.used || !instance.visible) continue;
        const Mat4d rotation = instance.rotation.ToMatrix();

        for (std::size_t p = 0; p < instance.parts.size(); ++p) {
            const PartState& state = instance.parts[p];
            if (!state.visible) continue;
            const ModelPart& part = instance.model->parts[p];
            if (part.mesh == kNoMesh) continue;

            const Vec3d local = instance.position + rotation.TransformDirection(Vec3d(part.position)) * static_cast<double>(instance.scale);
            // Con visitante se resta en metros: la distancia al sol del sistema no afecta.
            const Vec3d rel = hasViewer ? toWorld.TransformDirection(local - viewer) * 0.001 : baseRelative + toWorld.TransformDirection(local) * 0.001;
            const Vec3f relative(rel);
            const float radius = part.radius * instance.scale * 0.001f;
            const float distance = Length(relative);
            if (distance > radius && radius / distance * focal < 0.5f) continue;  // menos de un pixel
            if (!c.frustum.SphereVisible(relative, radius)) continue;

            MeshInstance m;
            m.viewPosition = c.viewRotation.TransformDirection(relative);
            m.scale = part.scale * instance.scale * 0.001f;
            m.rotation = part.billboard ? Quatf{} : viewFromWorld * Quatf::Cast((orientation * instance.rotation).Normalized()) * part.rotation;
            m.stretchX = part.billboard ? part.spriteWidth : 1.0f;
            m.stretchY = part.billboard ? part.spriteHeight : 1.0f;
            m.lightDirection = lightView;
            m.ambient = part.billboard ? 1.0f : ambient;
            m.color = state.color;
            m.emissive = state.emissive;
            m.id = 0x10000000u + static_cast<std::uint32_t>(id * 4096 + p);
            m.mesh = part.mesh;
            m.texture = part.texture;
            out.meshes.push_back(m);
        }
    }
}

void ModelLayer::Collect(const FrameContext& c, FramePacket& out) {
    if (!c.features.Enabled(Feature::Models)) return;
    for (const LocalSpace* space : spaces_) space->Collect(c, out);
}

}  // namespace eps
