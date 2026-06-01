#pragma once

#include <Geode/cocos/CCDirector.h>
#include <algorithm>
#include <cstdint>
#include <chrono>

namespace paimon::framebudget {

// Presupuesto de microsegundos POR FRAME compartido por TODO el trabajo de
// main-thread de thumbnails / LevelCell (GPU uploads, callbacks de carga, y el
// setup diferido de gradiente / extras de cada celda).
//
// A 360fps un frame dura ~2778us. Antes, cada fuente admitia trabajo con su
// propio presupuesto independiente (upload 1500us + callback 1200us + setup
// por-celda SIN presupuesto), de modo que al cargar muchas miniaturas a la vez
// un solo frame podia apilar >5ms y tirar los FPS al piso. Este presupuesto
// compartido acota el trabajo TOTAL de thumbnails por frame, dejando el resto
// del frame libre para el render + logica de GD y manteniendo los FPS cerca
// del objetivo de 360. El trabajo que no entra se reparte solo entre los
// frames siguientes (las fuentes reprograman / difieren).
//
// Solo se toca desde el main thread. La clave de frame es
// CCDirector::getTotalFrames(), asi el presupuesto se resetea automaticamente
// cada frame sin necesidad de un hook explicito de inicio de frame.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
inline constexpr int64_t kFrameBudgetUs = 900;
#else
inline constexpr int64_t kFrameBudgetUs = 1100;
#endif

inline int64_t& usedUsRef() { static int64_t v = 0; return v; }
inline int64_t& frameKeyRef() { static int64_t v = -1; return v; }

// Resetea el contador si entramos a un frame nuevo.
inline void refresh() {
    auto* dir = cocos2d::CCDirector::sharedDirector();
    int64_t key = dir ? static_cast<int64_t>(dir->getTotalFrames()) : 0;
    if (key != frameKeyRef()) {
        frameKeyRef() = key;
        usedUsRef() = 0;
    }
}

// Microsegundos que quedan del presupuesto de thumbnails para este frame.
inline int64_t remainingUs() {
    refresh();
    return std::max<int64_t>(0, kFrameBudgetUs - usedUsRef());
}

// true si aun queda presupuesto este frame.
inline bool hasBudget() {
    return remainingUs() > 0;
}

// Registra microsegundos consumidos por trabajo de thumbnails este frame.
inline void consume(int64_t us) {
    refresh();
    if (us > 0) usedUsRef() += us;
}

} // namespace paimon::framebudget
