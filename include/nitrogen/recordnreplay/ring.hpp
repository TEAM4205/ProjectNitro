#pragma once
// ============================================================================
//  ring.hpp  -  anillo de bloques entre dos tasks (un productor, un consumidor), sin mutex
//
//  Cada ranura guarda un bloque completo. El productor escribe DIRECTO en la ranura
//  (acquire -> escribir -> commit) y el consumidor la lee en su lugar (front -> usar -> pop):
//  cero copias. head solo lo mueve el productor y tail solo el consumidor.
//  La memoria se pide al empezar a grabar/reproducir y se devuelve al terminar.
// ============================================================================

#include <atomic>
#include <cstdint>
#include <new>

namespace rnr {

class SlotRing {
public:
    ~SlotRing() { release(); }

    // false si no hay memoria. Al menos 2 ranuras.
    bool allocate(uint32_t slotBytes, uint32_t totalBytes) {
        release();
        size = slotBytes;
        count = totalBytes / slotBytes < 2 ? 2 : totalBytes / slotBytes;
        mem = new (std::nothrow) uint8_t[size_t(count) * size];
        head = tail = 0;
        return mem != nullptr;
    }

    void release() {
        delete[] mem;
        mem = nullptr;
    }

    // Productor
    uint8_t* acquire() {
        uint32_t h = head.load(std::memory_order_relaxed);
        return h - tail.load(std::memory_order_acquire) == count ? nullptr : slot(h);
    }
    void commit() { head.store(head.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    // Consumidor
    uint8_t* front() {
        uint32_t t = tail.load(std::memory_order_relaxed);
        return t == head.load(std::memory_order_acquire) ? nullptr : slot(t);
    }
    void pop() { tail.store(tail.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    uint32_t used() const { return head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire); }
    uint32_t slots() const { return count; }
    uint32_t slotSize() const { return size; }

private:
    uint8_t* mem = nullptr;
    uint32_t size = 0, count = 0;
    std::atomic<uint32_t> head{0}, tail{0};

    uint8_t* slot(uint32_t i) { return mem + size_t(i % count) * size; }
};

} // namespace rnr
