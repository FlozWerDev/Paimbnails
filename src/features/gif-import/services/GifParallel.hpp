#pragma once

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace paimon::gifimport {

// Tope de hilos del trazado. 0 deja que lo decida la maquina.
unsigned int workerLimit();

// Los hilos de un reparto nacen y se juntan dentro de la misma llamada, asi que
// no pasan por ThreadTracker: no hay nada que cerrar al salir del juego mas alla
// del hilo de importacion, que si esta apuntado y espera a estos.
unsigned int parallelThreads(std::size_t count);
void enterParallelRegion();
void leaveParallelRegion();

// Reparte [0, count) entre los hilos. Dentro de un reparto no se abre otro: los
// pases de render ya ocupan la maquina entera y anidar solo la sobrecarga.
template <typename Fn>
void parallelFor(std::size_t count, Fn body) {
    unsigned int const threads = parallelThreads(count);
    if (threads <= 1) {
        for (std::size_t index = 0; index < count; ++index) body(index);
        return;
    }

    std::atomic<std::size_t> next{0};
    auto consume = [&] {
        for (;;) {
            std::size_t const index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) return;
            body(index);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    for (unsigned int worker = 1; worker < threads; ++worker) {
        workers.emplace_back([&consume] {
            enterParallelRegion();
            consume();
            leaveParallelRegion();
        });
    }
    enterParallelRegion();
    consume();
    leaveParallelRegion();
    for (auto& worker : workers) worker.join();
}

} // namespace paimon::gifimport
