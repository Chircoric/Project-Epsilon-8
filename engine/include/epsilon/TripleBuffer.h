// TripleBuffer.h - Entrega sin bloqueos del ultimo fotograma: un productor, un consumidor.
//
// La simulacion escribe en una ranura y la publica. El render toma siempre la publicacion
// mas reciente. Ninguno espera al otro; si el render va lento, se saltan fotogramas viejos.
#pragma once

#include <atomic>
#include <cstdint>

namespace eps {

template <class T>
class TripleBuffer {
public:
    // Solo el productor.
    T& WriteSlot() { return slots_[write_]; }
    void Publish() {
        const std::uint8_t prev = shared_.exchange(static_cast<std::uint8_t>(write_ | kFresh),
                                                   std::memory_order_acq_rel);
        write_ = prev & kIndexMask;
    }

    // Solo el consumidor. Devuelve true si hay una publicacion nueva.
    bool AcquireLatest() {
        if (!(shared_.load(std::memory_order_acquire) & kFresh)) return false;
        const std::uint8_t prev = shared_.exchange(read_, std::memory_order_acq_rel);
        read_ = prev & kIndexMask;
        return true;
    }
    const T& ReadSlot() const { return slots_[read_]; }

private:
    static constexpr std::uint8_t kIndexMask = 0x3;
    static constexpr std::uint8_t kFresh = 0x4;

    T slots_[3]{};
    std::uint8_t write_ = 0;
    std::uint8_t read_ = 1;
    std::atomic<std::uint8_t> shared_{2};
};

}  // namespace eps
