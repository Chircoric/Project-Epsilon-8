#include "epsilon/JobSystem.h"

#include <algorithm>

namespace eps {

namespace {
thread_local bool tlsInsideJob = false;
}

unsigned JobSystem::DefaultWorkers() {
    const unsigned hw = std::thread::hardware_concurrency();
    return hw > 1 ? hw - 1 : 0;
}

std::size_t JobSystem::ChunkCount(std::size_t count, std::size_t grain) {
    grain = std::max<std::size_t>(grain, 1);
    return (count + grain - 1) / grain;
}

JobSystem::JobSystem(unsigned workerThreads) {
    workers_.reserve(workerThreads);
    for (unsigned i = 0; i < workerThreads; ++i) workers_.emplace_back([this] { WorkerLoop(); });
}

JobSystem::~JobSystem() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wakeWorkers_.notify_all();
    for (std::thread& t : workers_) t.join();
}

void JobSystem::ParallelFor(std::size_t count, std::size_t grain, const RangeFn& fn) {
    if (count == 0) return;
    grain = std::max<std::size_t>(grain, 1);
    const std::size_t chunks = ChunkCount(count, grain);

    if (workers_.empty() || chunks == 1 || tlsInsideJob) {
        for (std::size_t c = 0; c < chunks; ++c) {
            const std::size_t begin = c * grain;
            fn(begin, std::min(begin + grain, count), c);
        }
        return;
    }

    // Se invita solo a los hilos que hacen falta (uno por tramo, menos el que llama) y no se espera
    // a que despierten: despertar a todos y aguardar su confirmacion costaba mas que el trabajo
    // cuando este era pequeno, y un fotograma hace decenas de repartos. El que llama empieza ya; el
    // hilo que llega a tiempo ayuda, y el que llega tarde encuentra las invitaciones retiradas.
    std::lock_guard dispatch(dispatchMutex_);
    const std::size_t invited = std::min(chunks - 1, workers_.size());
    {
        std::lock_guard lock(mutex_);
        fn_ = &fn;
        count_ = count;
        grain_ = grain;
        chunks_ = chunks;
        nextChunk_.store(0, std::memory_order_relaxed);
        error_ = nullptr;
        invitations_ = invited;
    }
    if (invited >= workers_.size()) wakeWorkers_.notify_all();
    else for (std::size_t i = 0; i < invited; ++i) wakeWorkers_.notify_one();

    RunChunks();

    // Solo se espera a los hilos que llegaron a entrar: asi ninguno sigue leyendo fn_ al volver.
    std::unique_lock lock(mutex_);
    invitations_ = 0;
    workersDone_.wait(lock, [this] { return activeWorkers_ == 0; });
    fn_ = nullptr;
    if (error_) std::rethrow_exception(error_);
}

void JobSystem::RunChunks() {
    const bool wasInside = tlsInsideJob;
    tlsInsideJob = true;
    for (;;) {
        const std::size_t c = nextChunk_.fetch_add(1, std::memory_order_relaxed);
        if (c >= chunks_) break;
        const std::size_t begin = c * grain_;
        try {
            (*fn_)(begin, std::min(begin + grain_, count_), c);
        } catch (...) {
            std::lock_guard lock(errorMutex_);
            if (!error_) error_ = std::current_exception();
        }
    }
    tlsInsideJob = wasInside;
}

void JobSystem::WorkerLoop() {
    for (;;) {
        {
            // Entrar es coger una invitacion, con el cerrojo: solo las hay mientras dura un reparto.
            std::unique_lock lock(mutex_);
            wakeWorkers_.wait(lock, [&] { return stop_ || invitations_ > 0; });
            if (stop_) return;
            --invitations_;
            ++activeWorkers_;
        }
        RunChunks();
        {
            std::lock_guard lock(mutex_);
            if (--activeWorkers_ == 0) workersDone_.notify_one();
        }
    }
}

}  // namespace eps
