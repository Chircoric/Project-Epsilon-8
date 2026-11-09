// Units.h - Las cifras del juego en unidades reales.
//
// Por dentro siguen siendo numeros comodos, los de Balance.h. Aqui se dice que significan:
//
//   1 de energia  = 1 exajulio (EJ), unos 1,6 veces lo que gasta el mundo en un dia.
//   1 de material = 1 kilotonelada (kt), 1000 toneladas.
//
// Con esa escala una central junto a una estrella como el Sol capta unos 750 exavatios, dos
// millonesimas de su luz: un enjambre de espejos de unos 28 000 km de lado a 8 radios. Formar una
// burbuja de curvatura cuesta 1 YJ, unas 5,6 kt de antimateria. La bodega cabe en una nave de 1,2 km.
#pragma once

#include <cstdio>
#include <string>

namespace game {

inline std::string UnitText(double value, const char* unit, int decimals) {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f %s", decimals, value, unit);
    return text;
}

// Con tres cifras como mucho: 808 EJ, 1.20 ZJ, 45 PJ.
inline std::string EnergyText(double exajoules) {
    if (exajoules >= 999500.0) return UnitText(exajoules / 1.0e6, "YJ", 2);
    if (exajoules >= 999.5) return UnitText(exajoules / 1000.0, "ZJ", 2);
    if (exajoules >= 1.0) return UnitText(exajoules, "EJ", 0);
    return UnitText(exajoules * 1000.0, "PJ", 0);
}

// De energia por hora a potencia: 1 EJ/h son 0,278 PW.
inline std::string PowerText(double exajoulesPerHour) {
    const double petawatts = exajoulesPerHour * 1.0e18 / 3600.0 / 1.0e15;
    if (petawatts >= 999.5) return UnitText(petawatts / 1000.0, "EW", 0);
    if (petawatts >= 1.0) return UnitText(petawatts, "PW", 1);
    if (petawatts >= 1.0e-3) return UnitText(petawatts * 1000.0, "TW", 0);
    return UnitText(petawatts * 1.0e6, "GW", 0);
}

inline std::string MassText(double kilotonnes) {
    if (kilotonnes >= 1000.0) return UnitText(kilotonnes / 1000.0, "Mt", 2);
    if (kilotonnes >= 1.0) return UnitText(kilotonnes, "kt", 1);
    return UnitText(kilotonnes * 1000.0, "t", 0);
}

inline std::string MassRateText(double kilotonnesPerHour) { return MassText(kilotonnesPerHour) + "/h"; }

}  // namespace game
