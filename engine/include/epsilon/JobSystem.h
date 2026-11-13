// JobSystem.h - Reparto de trabajo en paralelo, sin dependencias externas.
//
// El resultado no depende del hilo que ejecuta cada tramo: los tramos se calculan solo
// con (count, grain). Si guardas cada salida por indice de tramo y luego concatenas,
// obtienes el mismo resultado con 1 o con N hilos.
//
// Esta interfaz permite cambiar la implementacion por enkiTS sin tocar al resto del motor.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace eps {

class JobSystem {
public:
    using RangeFn = std::function<void(std::size_t begin, std::size_t end, std::size_t chunk)>;

    // Por defecto, un hilo por nucleo logico menos el que llama, que tambien trabaja.
    explicit JobSystem(unsigned workerThreads = DefaultWorkers());
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    static unsigned DefaultWorkers();
    static std::size_t ChunkCount(std::size_t count, std::size_t grain);

    unsigned ThreadCount() const { return static_cast<unsigned>(workers_.size()) + 1; }

    // Ejecuta fn sobre [0, count) en tramos de 'grain' y bloquea hasta terminar.
    // Una llamada anidada desde un trabajo se ejecuta en serie en ese mismo hilo.
    // La primera excepcion lanzada por fn se relanza aqui.
    void ParallelFor(std::size_t count, std::size_t grain, const RangeFn& fn);

private:
    void WorkerLoop();
    void RunChunks();

    std::vector<std::thread> workers_;
    std::mutex dispatchMutex_;  // solo un ParallelFor activo por sistema

    std::mutex mutex_;
    std::condition_variable wakeWorkers_;
    std::condition_variable workersDone_;
    std::size_t invitations_ = 0;    // hilos que aun pueden sumarse al reparto en curso
    std::size_t activeWorkers_ = 0;  // hilos que estan dentro de el
    bool stop_ = false;

    const RangeFn* fn_ = nullptr;
    std::size_t count_ = 0, grain_ = 1, chunks_ = 0;
    std::atomic<std::size_t> nextChunk_{0};

    std::mutex errorMutex_;
    std::exception_ptr error_;
};

}  // namespace eps
