// system_report.cpp - Genera un sistema solar e imprime sus datos y ecuaciones. No usa GPU.
//
//   epsilon_system                semilla 1 y estrella al azar
//   epsilon_system 42             otra semilla
//   epsilon_system 42 G           clase: O B A F G K M RG RSG BSG WD NS
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "epsilon/Astro.h"

using namespace eps::astro;

int main(int argc, char** argv) {
    const auto seed = static_cast<std::uint32_t>(argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1);
    std::mt19937 rng(seed);

    StarClass kind = RandomStarClass(rng, false);
    if (argc > 2) {
        const std::string name = argv[2];
        const char* const names[] = {"O", "B", "A", "F", "G", "K", "M", "RG", "RSG", "BSG", "WD", "NS"};
        bool found = false;
        for (int i = 0; i < 12; ++i) {
            if (name == names[i]) {
                kind = static_cast<StarClass>(i);
                found = true;
            }
        }
        if (!found) {
            std::printf("Clase desconocida: %s. Usa O B A F G K M RG RSG BSG WD NS\n", name.c_str());
            return 2;
        }
    }

    const StarSystemData system = GenerateSystem(kind, rng);
    std::fputs(DescribeSystem(system, "Sistema " + std::to_string(seed)).c_str(), stdout);

    const std::string problem = CheckStability(system);
    std::printf("Estabilidad: %s\n", problem.empty() ? "correcta" : problem.c_str());
    return problem.empty() ? 0 : 1;
}
