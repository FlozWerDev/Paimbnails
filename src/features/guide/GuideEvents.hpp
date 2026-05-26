#pragma once

#include <Geode/Geode.hpp>
#include <Geode/loader/Event.hpp>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// GuideEvents.hpp
//
// Eventos del sistema "Guia de Paimon".
//
// Sigue el patron v5 que ya usa el resto del proyecto (ver
// utils/ExtendedKeybind.hpp ExtendedKeybindTriggerEvent):
//
//     class X : public geode::Event<X, bool(args), FilterType> {
//         using Event::Event;
//     };
//
// Quien emite:
//   - PaimonHubLayer cuando el toggle "Guia" cambia
//   - PaimonGuideService::setEnabled() (futuro: si alguien la llama
//     directamente sin pasar por el toggle)
//
// Quien escucha:
//   - $modify(MenuLayer): para activar/desactivar la Paimon dinamica sin
//                          recargar la escena.
//
// Uso:
//
//   // Emitir
//   GuideEnabledChangedEvent("toggle").send(true);
//
//   // Escuchar (listener global "leaked")
//   auto listener = GuideEnabledChangedEvent("toggle").listen(
//       [](bool enabled) {
//           log::info("guide enabled = {}", enabled);
//           return ListenerResult::Propagate;
//       }
//   );
//   listener.leak();
// ─────────────────────────────────────────────────────────────────────────────

namespace paimon::guide {

class GuideEnabledChangedEvent final
    : public geode::Event<GuideEnabledChangedEvent, bool(bool enabled), std::string>
{
public:
    using Event::Event;
};

// Helper para no tener que recordar la cadena de filtro; siempre es la misma
// porque el evento es global del mod.
inline char const* kGuideEventFilter = "guide.toggle";

} // namespace paimon::guide
