#include "epsilon/GpuProbe.h"

#include <cstdio>
#include <iterator>
#include <utility>

#ifdef _WIN32
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#endif

namespace eps {

std::string FeatureLevelName(int level) {
    switch (level) {
        case 0xb0: return "11_0";
        case 0xb1: return "11_1";
        case 0xc0: return "12_0";
        case 0xc1: return "12_1";
        case 0xc2: return "12_2";
        default: return "?";
    }
}

std::string ShaderModelName(int model) {
    if (model <= 0) return "?";
    char text[16];
    std::snprintf(text, sizeof(text), "%d.%d", model >> 4, model & 0xf);
    return text;
}

#ifdef _WIN32
namespace {

using Microsoft::WRL::ComPtr;

std::string Utf8(const wchar_t* wide) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), n, nullptr, nullptr);
    return out;
}

// El nivel mas alto de la lista que el dispositivo admite. Un runtime que no conoce un nivel
// responde con error a toda la lista, asi que se prueba primero con 12_2 y despues sin el.
int HighestFeatureLevel(ID3D12Device* device) {
    const D3D_FEATURE_LEVEL all[] = {static_cast<D3D_FEATURE_LEVEL>(0xc200), D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                     D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    for (int skip = 0; skip < 2; ++skip) {
        D3D12_FEATURE_DATA_FEATURE_LEVELS data{};
        data.NumFeatureLevels = static_cast<UINT>(std::size(all)) - static_cast<UINT>(skip);
        data.pFeatureLevelsRequested = all + skip;
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &data, sizeof(data)))) {
            return static_cast<int>(data.MaxSupportedFeatureLevel) >> 8;
        }
    }
    return 0;
}

// El modelo de shaders mas alto: se pregunta de mayor a menor, y el primero que el controlador acepta es el suyo.
int HighestShaderModel(ID3D12Device* device) {
    for (int candidate : {0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60, 0x51}) {
        D3D12_FEATURE_DATA_SHADER_MODEL data{};
        data.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(candidate);
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &data, sizeof(data)))) {
            return static_cast<int>(data.HighestShaderModel);
        }
    }
    return 0;
}

}  // namespace
#endif

GpuReport ProbeGpu() {
    GpuReport report;
#ifdef _WIN32
    HMODULE dxgiDll = LoadLibraryA("dxgi.dll");
    if (!dxgiDll) {
        report.problem = "Este Windows no tiene DXGI. El juego necesita Windows 10 o posterior.";
        return report;
    }
    using CreateFactory1 = HRESULT(WINAPI*)(REFIID, void**);
    const auto createFactory = reinterpret_cast<CreateFactory1>(GetProcAddress(dxgiDll, "CreateDXGIFactory1"));
    if (!createFactory) {
        report.problem = "Este Windows no tiene DXGI 1.1. El juego necesita Windows 10 o posterior.";
        FreeLibrary(dxgiDll);
        return report;
    }
    // d3d12.dll se carga a mano: en un Windows que no la tiene, el informe sigue saliendo.
    HMODULE d3d12Dll = LoadLibraryA("d3d12.dll");
    using CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    const auto createDevice = d3d12Dll ? reinterpret_cast<CreateDevice>(GetProcAddress(d3d12Dll, "D3D12CreateDevice")) : nullptr;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(createFactory(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf())))) {
        report.problem = "No se pudo consultar a Windows por la tarjeta gráfica (DXGI).";
    } else {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc))) continue;
            GpuAdapterInfo info;
            info.name = Utf8(desc.Description);
            info.vendorId = desc.VendorId;
            info.deviceId = desc.DeviceId;
            info.dedicatedMb = desc.DedicatedVideoMemory / (1024ull * 1024ull);
            info.sharedMb = desc.SharedSystemMemory / (1024ull * 1024ull);
            info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            LARGE_INTEGER version{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
                char text[48];
                std::snprintf(text, sizeof(text), "%u.%u.%u.%u", static_cast<unsigned>(HIWORD(version.HighPart)),
                              static_cast<unsigned>(LOWORD(version.HighPart)), static_cast<unsigned>(HIWORD(version.LowPart)),
                              static_cast<unsigned>(LOWORD(version.LowPart)));
                info.driverVersion = text;
            }
            if (createDevice) {
                ComPtr<ID3D12Device> device;
                if (SUCCEEDED(createDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), reinterpret_cast<void**>(device.GetAddressOf())))) {
                    info.d3d12 = true;
                    info.featureLevel = HighestFeatureLevel(device.Get());
                    info.shaderModel = HighestShaderModel(device.Get());
                    D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
                    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch)))) info.integrated = arch.UMA != FALSE;
                }
            }
            report.adapters.push_back(std::move(info));
        }
    }
    if (d3d12Dll) FreeLibrary(d3d12Dll);
    FreeLibrary(dxgiDll);
    if (!d3d12Dll && report.problem.empty()) report.problem = "Este Windows no tiene Direct3D 12. El juego necesita Windows 10 o posterior.";
#else
    report.problem = "Solo se puede consultar la tarjeta gráfica en Windows.";
#endif

    bool anyReal = false;
    for (const GpuAdapterInfo& a : report.adapters) {
        if (a.software) continue;
        anyReal = true;
        if (a.d3d12 && a.shaderModel >= 0x60) report.supported = true;
    }
    if (!report.supported && report.problem.empty()) {
        if (report.adapters.empty()) {
            report.problem = "Windows no encuentra ninguna tarjeta gráfica.";
        } else if (!anyReal) {
            report.problem = "Solo hay un adaptador por software (WARP), sin tarjeta gráfica real: Windows no está usando el controlador de la tarjeta.";
        } else {
            report.problem = "Ninguna tarjeta gráfica admite Direct3D 12 con shaders de modelo 6.0.";
        }
    }
    return report;
}

std::string DescribeAdapter(const GpuAdapterInfo& a) {
    std::string text = a.name.empty() ? "(sin nombre)" : a.name;
    text += a.software ? " | por software" : a.integrated ? " | integrada" : " | dedicada";
    if (a.d3d12) {
        text += " | D3D12 " + FeatureLevelName(a.featureLevel);
        text += " | shaders " + ShaderModelName(a.shaderModel);
    } else {
        text += " | sin Direct3D 12";
    }
    char memory[96];
    std::snprintf(memory, sizeof(memory), " | memoria %llu MB propia, %llu MB compartida", static_cast<unsigned long long>(a.dedicatedMb),
                  static_cast<unsigned long long>(a.sharedMb));
    text += memory;
    if (!a.driverVersion.empty()) text += " | controlador " + a.driverVersion;
    return text;
}

std::string FormatGpuReport(const GpuReport& report) {
    std::string out;
    for (std::size_t i = 0; i < report.adapters.size(); ++i) out += "Tarjeta " + std::to_string(i + 1) + ": " + DescribeAdapter(report.adapters[i]) + "\n";
    if (report.adapters.empty()) out += "Tarjetas: ninguna encontrada\n";
    out += report.supported ? "Veredicto: la tarjeta cumple lo que pide el juego (Direct3D 12, shaders 6.0)\n" : "Veredicto: " + report.problem + "\n";
    return out;
}

std::string ExplainGpuFailure(const GpuReport& report) {
    std::string out = "No se pudo iniciar la tarjeta gráfica.\n\n";
    out += FormatGpuReport(report);
    out += "\nEl juego necesita Direct3D 12 con shaders de modelo 6.0 (Windows 10 o posterior).\n";
    out += "Qué probar: actualizar el controlador de la tarjeta desde la web de su fabricante (Intel, AMD o NVIDIA) y Windows Update. ";
    out += "Si es una tarjeta de antes de 2015, es probable que no lo admita.\n";
    return out;
}

}  // namespace eps
