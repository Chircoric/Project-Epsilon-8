#include "epsilon/Features.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace eps {

namespace {

constexpr const char* kNames[] = {"cielo", "nebulosa", "asteroides", "agujeros", "modelos", "orbitas", "rutas",
                                  "hud", "bloom", "vineta", "aberracion", "grano", "streaming", "descarte", "colisiones"};
static_assert(std::size(kNames) == static_cast<std::size_t>(Feature::Count));

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

}  // namespace

const char* FeatureSet::Name(Feature f) { return kNames[static_cast<std::size_t>(f)]; }

std::optional<Feature> FeatureSet::Find(const std::string& name) {
    const std::string wanted = Lower(name);
    for (std::size_t i = 0; i < std::size(kNames); ++i)
        if (wanted == kNames[i]) return static_cast<Feature>(i);
    return std::nullopt;
}

void CommandRegistry::Add(const std::string& name, const std::string& help, Handler handler) {
    commands_[Lower(name)] = {help, std::move(handler)};
}

void CommandRegistry::AddAlias(const std::string& alias, const std::string& command) { aliases_[Lower(alias)] = Lower(command); }

void CommandRegistry::AddVariable(const std::string& name, float* value) { variables_[Lower(name)] = value; }

std::string CommandRegistry::Execute(const std::string& line) {
    std::istringstream words(line);
    std::string name, word;
    words >> name;
    Args args;
    while (words >> word) args.push_back(word);
    lastSucceeded_ = true;
    if (name.empty() || name[0] == '#') return {};

    name = Lower(name);
    if (const auto alias = aliases_.find(name); alias != aliases_.end()) name = alias->second;

    if (name == "fijar") {
        if (args.empty()) {
            std::string list;
            for (const auto& [key, value] : variables_) list += key + " = " + std::to_string(*value) + "\n";
            return list;
        }
        const auto variable = variables_.find(Lower(args[0]));
        if (variable == variables_.end()) {
            lastSucceeded_ = false;
            return "Variable desconocida: " + args[0];
        }
        if (args.size() >= 2) {
            char* end = nullptr;
            const float value = std::strtof(args[1].c_str(), &end);
            if (end == args[1].c_str() || *end != '\0') {
                lastSucceeded_ = false;
                return "Valor no valido: " + args[1];
            }
            *variable->second = value;
        }
        return variable->first + " = " + std::to_string(*variable->second);
    }

    const auto command = commands_.find(name);
    if (command == commands_.end()) {
        lastSucceeded_ = false;
        return "Comando desconocido: " + name + ". Escribe 'ayuda'.";
    }
    return command->second.handler(args);
}

int CommandRegistry::ExecuteScript(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    int failed = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        Execute(line);
        if (!lastSucceeded_) ++failed;
    }
    lastSucceeded_ = failed == 0;
    return failed;
}

std::vector<std::string> CommandRegistry::Help() const {
    std::vector<std::string> out;
    out.push_back("fijar [variable] [valor] - lista o cambia una variable");
    for (const auto& [name, entry] : commands_) out.push_back(name + " - " + entry.help);
    return out;
}

void RegisterCoreCommands(CommandRegistry& commands, FeatureSet& features) {
    const auto change = [&commands, &features](const char* verb, int mode) {
        return [&commands, &features, verb, mode](const CommandRegistry::Args& args) -> std::string {
            if (args.empty()) return std::string("Uso: ") + verb + " <funcion>. Mira 'estado'.";
            std::string answer;
            for (const std::string& name : args) {
                if (name == "todo") {
                    for (std::size_t i = 0; i < static_cast<std::size_t>(Feature::Count); ++i) {
                        const Feature f = static_cast<Feature>(i);
                        if (mode == 2) features.Toggle(f);
                        else features.Set(f, mode == 1);
                    }
                    answer += "todo\n";
                    continue;
                }
                const std::optional<Feature> f = FeatureSet::Find(name);
                if (!f) return commands.Fail("Funcion desconocida: " + name);
                if (mode == 2) features.Toggle(*f);
                else features.Set(*f, mode == 1);
                answer += std::string(FeatureSet::Name(*f)) + (features.Enabled(*f) ? ": activado\n" : ": desactivado\n");
            }
            if (!answer.empty()) answer.pop_back();
            return answer;
        };
    };
    commands.Add("activar", "enciende una funcion; 'todo' las enciende todas", change("activar", 1));
    commands.Add("desactivar", "apaga una funcion; 'todo' las apaga todas", change("desactivar", 0));
    commands.Add("alternar", "cambia el estado de una funcion", change("alternar", 2));
    commands.AddAlias("on", "activar");
    commands.AddAlias("off", "desactivar");
    commands.AddAlias("toggle", "alternar");
    commands.AddAlias("set", "fijar");
    commands.AddAlias("help", "ayuda");
    commands.AddAlias("status", "estado");

    commands.Add("estado", "muestra que funciones estan activas", [&features](const CommandRegistry::Args&) {
        std::string out;
        for (std::size_t i = 0; i < static_cast<std::size_t>(Feature::Count); ++i) {
            const Feature f = static_cast<Feature>(i);
            out += std::string(FeatureSet::Name(f)) + (features.Enabled(f) ? " si   " : " no   ");
        }
        return out;
    });
    commands.Add("ayuda", "lista los comandos", [&commands](const CommandRegistry::Args&) {
        std::string out;
        for (const std::string& line : commands.Help()) out += line + "\n";
        return out;
    });
}

}  // namespace eps
