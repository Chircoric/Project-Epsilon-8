// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Chircoric
// GpuProbe.h - Que tarjetas graficas hay y si sirven para este motor, sin crear ventana ni SDL.
//
// El motor solo usa Direct3D 12 con shaders de modelo 6.0 (SdlGpuBackend). Una tarjeta integrada
// antigua o un controlador viejo no los admiten, y entonces el juego no puede arrancar. Esta sonda
// pregunta a DXGI y a D3D12 directamente, asi que funciona justo cuando SDL_GPU no consigue abrir
// el dispositivo: sirve para explicar el fallo y para el informe de --diagnostico.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eps {

struct GpuAdapterInfo {
    std::string name;
    std::uint32_t vendorId = 0, deviceId = 0;
    std::string driverVersion;         // p. ej. "31.0.101.2125"; vacio si no se pudo leer
    std::uint64_t dedicatedMb = 0;     // memoria de video propia
    std::uint64_t sharedMb = 0;        // memoria del sistema que puede usar
    bool software = false;             // WARP: Direct3D por procesador, sin tarjeta
    bool integrated = false;           // comparte memoria con el procesador (UMA)
    bool d3d12 = false;                // se pudo crear un dispositivo Direct3D 12
    int featureLevel = 0;              // 0xb0 = 11_0, 0xb1 = 11_1, 0xc0 = 12_0, 0xc1 = 12_1, 0xc2 = 12_2
    int shaderModel = 0;               // 0x60 = 6.0, 0x65 = 6.5...; 0 si no se pudo preguntar
};

struct GpuReport {
    std::vector<GpuAdapterInfo> adapters;
    bool supported = false;            // alguna tarjeta real admite D3D12 y shaders 6.0
    std::string problem;               // si no, por que (en español, para ensenarlo al jugador)
};

// Pregunta a Windows. En otros sistemas devuelve un informe vacio, no soportado.
GpuReport ProbeGpu();

// "Intel(R) HD Graphics 530 | integrada | D3D12 12_1 | shaders 6.5 | controlador 31.0.101.2125".
std::string DescribeAdapter(const GpuAdapterInfo& adapter);

// El informe entero, una tarjeta por linea, y el veredicto.
std::string FormatGpuReport(const GpuReport& report);

// Texto para un aviso cuando el juego no ha podido abrir la tarjeta: que se encontro y que hacer.
std::string ExplainGpuFailure(const GpuReport& report);

// Nombres de los niveles, para las pruebas: 0xc1 -> "12_1", 0x65 -> "6.5".
std::string FeatureLevelName(int level);
std::string ShaderModelName(int model);

}  // namespace eps
