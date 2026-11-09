// Features.h - Interruptores del motor y consola de comandos.
//
// Cada modulo consulta su interruptor en cada fotograma: apagar algo elimina su trabajo, no
// solo su dibujo. Los comandos son texto, asi que sirven igual desde una consola dentro del
// juego, un archivo de configuracion o la linea de ordenes.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eps {

enum class Feature : std::uint8_t {
    Sky,          // panorama y estrellas de fondo
    Nebula,       // nubes volumetricas
    Asteroids,    // cinturones y campos
    BlackHoles,   // lente gravitacional y disco
    Models,       // modelos 2D y 3D a escala humana
    OrbitLines,
    Routes,       // rutas entre sistemas del juego original
    Hud,
    Bloom,
    Vignette,
    Aberration,
    Grain,
    Streaming,    // carga y descarga de sistemas
    BodyCulling,  // no calcula lo que no ocupa pixeles
    Collisions,   // detecta choques y dibuja sus explosiones
    Count
};

class FeatureSet {
public:
    FeatureSet() { enabled_.fill(true); }

    bool Enabled(Feature f) const { return enabled_[static_cast<std::size_t>(f)]; }
    void Set(Feature f, bool on) { enabled_[static_cast<std::size_t>(f)] = on; }
    void Toggle(Feature f) { Set(f, !Enabled(f)); }

    // Nombre del comando: "nebulosa", "bloom"...
    static const char* Name(Feature f);
    static std::optional<Feature> Find(const std::string& name);

private:
    std::array<bool, static_cast<std::size_t>(Feature::Count)> enabled_{};
};

// Ejecuta lineas como "desactivar nebulosa" o "fijar bloom.fuerza 0.6".
// No es seguro entre hilos: se llama desde el hilo que dueno de los datos.
class CommandRegistry {
public:
    using Args = std::vector<std::string>;
    using Handler = std::function<std::string(const Args&)>;

    void Add(const std::string& name, const std::string& help, Handler handler);
    void AddAlias(const std::string& alias, const std::string& command);

    // Variable numerica que se lee y cambia con "fijar". Debe sobrevivir al registro.
    void AddVariable(const std::string& name, float* value);

    // Devuelve el texto de respuesta. Un comando desconocido no es un error fatal.
    std::string Execute(const std::string& line);

    // Varias lineas; '#' empieza un comentario. Devuelve cuantas lineas no se reconocieron.
    int ExecuteScript(const std::string& text);

    std::vector<std::string> Help() const;
    bool LastSucceeded() const { return lastSucceeded_; }

    // Para los comandos: marca la ejecucion actual como fallida y devuelve el mensaje.
    std::string Fail(const std::string& message) {
        lastSucceeded_ = false;
        return message;
    }

private:
    struct Entry {
        std::string help;
        Handler handler;
    };
    std::map<std::string, Entry> commands_;
    std::map<std::string, std::string> aliases_;
    std::map<std::string, float*> variables_;
    bool lastSucceeded_ = true;
};

// Anade activar, desactivar, alternar, estado, ayuda y fijar.
void RegisterCoreCommands(CommandRegistry& commands, FeatureSet& features);

}  // namespace eps
