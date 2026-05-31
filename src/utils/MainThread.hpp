#pragma once

#include <thread>
#include <atomic>
#include <mutex>

namespace paimon {

// Helper centralizado para detectar si el thread actual es el main thread.
// Geode no expone una API estable para esto, asi que capturamos el ID del
// thread la primera vez que se invoca `captureMainThread()` (idealmente
// desde MenuLayer::init o algun bootstrap temprano que SIEMPRE corra en main).
//
// Uso tipico:
//   if (paimon::isMainThread()) { /* GL calls OK */ }
//   else { /* defer via queueInMainThread */ }
//
// IMPORTANTE: si nadie llama a captureMainThread() antes del primer uso,
// `isMainThread()` devolvera false (id por defecto). El bootstrap del mod
// debe llamarlo en el primer hook que sepa que esta en main (MenuLayer
// init, LoadingLayer init, etc.).
inline std::thread::id& getMainThreadId() {
    // Heap-allocated para evitar destructor en atexit.
    static auto* id = new std::thread::id{};
    return *id;
}

inline std::once_flag& getMainThreadInitFlag() {
    static auto* flag = new std::once_flag{};
    return *flag;
}

// Captura el ID del thread actual como "main". Llamar SOLO desde main thread.
// Idempotente — solo el primer call efectua la captura.
inline void captureMainThread() {
    std::call_once(getMainThreadInitFlag(), []() {
        getMainThreadId() = std::this_thread::get_id();
    });
}

// Devuelve true si se esta corriendo en el thread que se capturo via
// captureMainThread(). Si nunca se capturo, devuelve false (defensivo).
inline bool isMainThread() {
    auto& id = getMainThreadId();
    if (id == std::thread::id{}) return false; // no inicializado
    return std::this_thread::get_id() == id;
}

} // namespace paimon
