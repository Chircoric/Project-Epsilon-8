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

    std::lock_guard dispatch(dispatchMutex_);
    {
        std::lock_guard lock(mutex_);
        fn_ = &fn;
        count_ = count;
        grain_ = grain;
        chunks_ = chunks;
        nextChunk_.store(0, std::memory_order_relaxed);
        error_ = nullptr;
        pendingWorkers_ = workers_.size();
        ++generation_;
    }
    wakeWorkers_.notify_all();

    RunChunks();

    // Cada hilo confirma cada generacion, asi ninguno sigue leyendo fn_ al volver.
    std::unique_lock lock(mutex_);
    workersDone_.wait(lock, [this] { return pendingWorkers_ == 0; });
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
    std::size_t seen = 0;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            wakeWorkers_.wait(lock, [&] { return stop_ || generation_ != seen; });
            if (stop_) return;
            seen = generation_;
        }
        RunChunks();
        {
            std::lock_guard lock(mutex_);
            if (--pendingWorkers_ == 0) workersDone_.notify_one();
        }
    }
}

}  // namespace eps
