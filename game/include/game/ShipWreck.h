// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Chircoric
// ShipWreck.h - Como se rompe una nave al morir.
//
// La sesion solo anota que una nave ha muerto (Wreck): donde estaba, como giraba, que zonas le
// quedaban y de donde salio la explosion. Lo anota igual en todas las calidades, y no decide nada
// de la partida. Con eso, quien dibuja reparte las piezas del modelo en cuerpos (PlanWreck) y
// calcula donde esta cada uno en cada instante (PoseOf) con una formula cerrada: no hay estado por
// fotograma ni choques entre trozos, asi que cuesta lo mismo dibujar el segundo 1 que el 20.
//
// Cada cuerpo sigue girando con la nave hasta que el frente de la explosion lo alcanza; entonces
// sale con la velocidad que llevaba ese punto de la nave (el giro heredado) mas el impulso.
#pragma once

#include <cstdint>
#include <vector>

#include "epsilon/Math3D.h"
#include "game/Balance.h"
#include "game/HullZones.h"

namespace game {

// Una nave que acaba de morir. Sin punteros ni listas: viaja tal cual en la instantanea de red.
struct Wreck {
    int id = 0;
    int model = -1;            // -1: una nave de la flota (el modelo de la nave); >= 0: AlienModel
    int vessel = -1;           // id de la nave de la flota que era; -1 si era una anomalia
    std::uint32_t seed = 0;    // cada una se rompe a su manera
    eps::Vec3d absolute;       // km, sin restar el origen del mundo
    eps::Vec3d velocity;       // km/s simulados: la que llevaba
    eps::Vec3d drift;          // km/s reales: la deriva de una nave apagada
    eps::Quatd orientation;    // del modelo al mundo, al morir
    eps::Vec3d angular;        // rad/s reales, en el mundo: eje por ritmo
    eps::Vec3d origin;         // m, en el modelo: de donde sale la explosion
    HullZones zones{};         // integridad de cada zona al morir: las perdidas ya no estan
    double energy = 1.0;       // 1: revienta el nucleo; menos: se parte sin estallar
    double size = 0.5;         // km: radio de la nave
    double age = 0;            // s reales desde que murio
};

// Lo que hace falta saber de una pieza del modelo para repartirla.
struct WreckPart {
    eps::Vec3d position;   // m, en el modelo
    double radius = 1.0;   // m
    int zone = -1;
    bool gone = false;     // no se dibuja: oculta, o arrancada antes de morir
};

// Un cuerpo: un grupo de piezas que se va junto.
struct WreckBody {
    eps::Vec3d center;     // m, en el modelo
    double radius = 0;     // m
    eps::Vec3d half;       // m: media caja que lo envuelve, en los ejes del modelo
    int zone = -1;         // la zona del casco de su primera pieza
    eps::Vec3d velocity;   // m/s reales, en el modelo: el impulso de la explosion
    eps::Vec3d spinAxis{0, 1, 0};  // en el modelo
    double spin = 0;       // rad/s reales, ademas del giro heredado
    double release = 0;    // s reales tras la muerte hasta que se suelta
    int parts = 0;
};

struct WreckPlan {
    std::vector<int> bodyOfPart;   // por pieza: su cuerpo, o -1 si no se dibuja
    std::vector<WreckBody> bodies;
};

// Reparte las piezas en maxBodies cuerpos como mucho: primero por zonas del casco, y despues
// parte por la mitad el grupo mas grande hasta llegar al tope. Las zonas ya perdidas no entran.
WreckPlan PlanWreck(const std::vector<WreckPart>& parts, const Wreck& wreck, int maxBodies, const DestructionRules& rules);

// Donde esta un cuerpo a los age segundos reales de la muerte.
struct WreckPose {
    eps::Vec3d offset;      // m, en el mundo: del centro de los restos al centro del cuerpo
    eps::Quatd rotation;    // del modelo al mundo
    double free = 0;        // s que lleva suelto; 0 si aun va con la nave
};
WreckPose PoseOf(const Wreck& wreck, const WreckBody& body, double age);

// Un numero de 0 a 1 que depende solo de la semilla y de la sal: para repartir sin guardar nada.
double WreckRandom(std::uint32_t seed, std::uint32_t salt);

}  // namespace game
