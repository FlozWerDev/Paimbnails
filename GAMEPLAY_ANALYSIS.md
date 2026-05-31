# Analisis de Gameplay — Paimbnails v1.0.5

> Analisis profundo de los hooks de gameplay: PlayLayer, PauseLayer, EndLevelLayer y sistemas relacionados.

---

## 1. Resumen Ejecutivo

El mod intercepta el gameplay de GD en 3 puntos clave:

1. **PlayLayer** (1161 lineas) — Zoom en pausa, captura de thumbnails, keybinds, ForYou tracking
2. **PauseLayer** (1129 lineas) — Botones de captura, upload de thumbnails/GIF/video, file picker
3. **EndLevelLayer** (35 lineas) — Tracking de completacion para ForYou

**Hallazgo principal:** El codigo de gameplay es **extremadamente robusto** con manejo defensivo de race conditions, guards de null, y cleanup exhaustivo. Sin embargo, hay **2 problemas criticos de compatibilidad** con mods de pausa y **1 potencial crash** en el sistema de captura.

---

## 2. PlayLayer.cpp — Analisis Detallado

### 2.1 Arquitectura

```
PlayLayer.cpp
├── PauseZoomManager (singleton)
│   ├── onPause() / onResume()
│   ├── update() — ticker cada frame
│   ├── onScroll() — zoom con mouse wheel
│   ├── zoomInStep() / zoomOutStep() — keybinds
│   ├── togglePauseMenu() — keybind
│   ├── hidePauseMenu() / restorePauseMenuVisible()
│   └── zoomAtMouse() — zoom centrado en cursor
├── PaimonCapturePlayLayer ($modify)
│   ├── init() — setup keybinds, ForYou tracking
│   ├── onQuit() — cleanup
│   ├── pauseGame() — coordina con PauseZoomManager
│   ├── startGame() — reset zoom
│   └── captureScreenshot() — captura de pantalla
├── PauseZoomTickerNode — CCNode con update()
└── PaimonPauseZoomVisitFilter ($modify CCNode)
    └── visit() — skip render PauseLayer durante zoom
```

### 2.2 PauseZoomManager — Estado y Transiciones

```
[Idle] --pauseGame(true)--> [Paused]
[Paused] --scroll down--> [Zooming] --scroll up--> [Paused]
[Paused] --onResume()--> [Idle]
[Paused] --onQuit()--> [Idle]
```

**Guards de estado:**
- `m_isPaused` — booleano principal
- `m_pauseLayerMissingFrames` — cuenta frames sin PauseLayer (auto-resume a los 120)
- `m_menuForcedHidden` — tracking de si escondimos el menu nosotros

### 2.3 Race Conditions Manejadas

| Race Condition | Mitigacion |
|----------------|------------|
| PauseLayer no existe al pausar | `m_pauseLayerMissingFrames` counter (120 frames) |
| Esc + T mismo frame | `hasPauseLayerInScene()` check antes de captura |
| Captura keybind + boton pausa simultaneo | `gCaptureInProgress` atomic + `paimon::isCaptureInProgress()` |
| Popup destruido entre frames | `WeakRef` en todos los callbacks |
| PlayLayer destruido durante captura | `weakRef.lock()` + `getParent()` check |
| PauseLayer restaura m_bVisible | Hook de `CCNode::visit()` con `isPauseZoomHidden()` |
| Mod reemplaza pauseGame() | Deteccion por presencia de PauseLayer en escena, no por hook |

### 2.4 Keybinds Registrados

```cpp
"capture-keybind"        — Captura thumbnail desde gameplay
"zoom-in-keybind"        — Zoom in durante pausa
"zoom-out-keybind"       — Zoom out durante pausa
"zoom-reset-keybind"     — Reset zoom a 1.0
"zoom-toggle-menu-keybind" — Toggle visibilidad del menu de pausa
```

### 2.5 Problemas Encontrados en PlayLayer.cpp

#### P2.5.1 — `GetAsyncKeyState(VK_MBUTTON)` sin verificacion de plataforma

**Linea:** 198

```cpp
#ifdef GEODE_IS_WINDOWS
    m_isPanning = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
#else
    m_isPanning = false;  // ← Mac/Android no tienen panning
#endif
```

**Problema:** En Mac, el panning con middle-click no funciona. No es critico pero es una feature incompleta.

**Severidad:** BAJA

---

#### P2.5.2 — `CCNode::visit()` hook global puede afectar rendimiento

**Lineas:** 1131-1155

```cpp
class $modify(PaimonPauseZoomVisitFilter, CCNode) {
    void visit() {
        if (paimon::isPauseZoomHidden()) {
            auto* activePause = static_cast<CCNode*>(paimon::getActivePauseLayer());
            if (activePause && this == activePause) {
                return;  // Skip render
            }
        }
        CCNode::visit();
    }
};
```

**Problema:** Aunque el comentario dice que cuesta ~5ns, este hook se aplica a **todos** los CCNode. Si otro mod (ej. devtools, debug overlay) tambien hookea `visit()`, hay competencia de prioridades.

**Mitigacion actual:** `setHookPriorityPre("cocos2d::CCNode::visit", geode::Priority::Late)`

**Riesgo:** Si un mod con mayor prioridad quiere renderizar el PauseLayer, nuestro filtro se ejecuta despues y lo bloquea.

**Severidad:** MEDIA

---

#### P2.5.3 — `getPauseLayer()` busca por tipo en toda la escena

**Lineas:** 377-395

```cpp
PauseLayer* getPauseLayer() const {
    auto* scene = CCDirector::get() ? CCDirector::get()->getRunningScene() : nullptr;
    // ... busca por ID primero, luego por typeinfo_cast
    for (auto* obj : CCArrayExt<CCObject*>(children)) {
        if (auto* pauseLayer = typeinfo_cast<PauseLayer*>(obj)) {
            return pauseLayer;
        }
    }
}
```

**Problema:** Si hay **multiple instancias** de PauseLayer (algun mod crea uno extra), este codigo retorna el primero que encuentra, que puede no ser el activo.

**Severidad:** BAJA (muy raro)

---

#### P2.5.4 — `captureScreenshot()` no verifica `m_isPaused`

**Lineas:** 893-942

```cpp
void captureScreenshot(CapturePreviewPopup* existingPopup = nullptr) {
    if (gCaptureInProgress.load()) return;
    gCaptureInProgress.store(true);
    // ... no verifica si el juego esta pausado
```

**Problema:** Si se llama a `captureScreenshot()` mientras el juego NO esta pausado, la captura incluira el HUD y elementos de UI que deberian estar ocultos.

**Severidad:** BAJA (solo afecta calidad visual)

---

### 2.6 Cosas Bien Hechas en PlayLayer.cpp

1. **Agent logging condicional** — `PAIMON_DEBUG_AGENT347` gatea logs de debug
2. **WeakRef en todos los callbacks async** — Previene dangling pointers
3. **Cleanup function en lambda** — `cleanup()` se llama en todas las rutas de salida
4. **Ref<CCTexture2D>** — Maneja lifecycle de texturas correctamente
5. **Comparacion de punteros en visit()** — O(1) sin RTTI overhead

---

## 3. PauseLayer.cpp — Analisis Detallado

### 3.1 Arquitectura

```
PauseLayer.cpp
├── PaimonPauseLayer ($modify)
│   ├── customSetup() — Agrega botones de captura
│   ├── onScreenshot() — Inicia captura desde boton
│   ├── performCaptureAndRestore() — Captura async
│   ├── onExit() — Cleanup exhaustivo
│   ├── onResume() — Guard contra null PlayLayer
│   ├── onRestart() / onRestartFull() / onNormalMode() / onPracticeMode()
│   └── processSelectedFile() — PNG/GIF/MP4 upload
└── Helpers
    ├── convertRGBAtoRGB()
    ├── tryCreateIcon()
    └── findButtonMenu() — Busqueda robusta de menus
```

### 3.2 Flujo de Captura desde PauseLayer

```
onScreenshot()
    ├── Verifica mutex con keybind (isCaptureInProgress)
    ├── setVisible(false) + setCaptureInProgress(true)
    ├── showLoadingOverlay()
    ├── scheduleOnce(captureSafetyRestore, 8.0s)
    └── scheduleSelector(performCaptureAndRestore, 0.05s)

performCaptureAndRestore()
    ├── unschedule performCaptureAndRestore
    ├── Verifica parent (orphaned check)
    ├── FramebufferCapture::validateCaptureConditions()
    ├── FramebufferCapture::requestCapture()
    └── Callback async:
        ├── WeakRef.lock() check
        ├── removeLoadingOverlay()
        ├── setCaptureInProgress(false)
        ├── CapturePreviewPopup::create()
        └── setVisible(true)

captureSafetyRestore() [watchdog]
    ├── Si capturo tardo > 8s
    └── Restaura UI y notifica error
```

### 3.3 Problemas Encontrados en PauseLayer.cpp

#### P3.3.1 — **CRITICO:** `onResume()` puede causar crash si PlayLayer es null

**Lineas:** 1076-1096

```cpp
void onResume(CCObject* sender) {
    if (!PlayLayer::get()) {
        log::warn("[PauseLayer] onResume called but PlayLayer::get() is null");
        this->removeFromParentAndCleanup(true);
        return;
    }
    paimon::notifyPauseClosing();
    PauseLayer::onResume(sender);
}
```

**Problema:** El guard `!PlayLayer::get()` es correcto, pero `this->removeFromParentAndCleanup(true)` puede causar **double-free** si el PauseLayer ya esta siendo destruido por otro codigo (ej. transicion de escena).

**Severidad:** ALTA

**Fix sugerido:**
```cpp
void onResume(CCObject* sender) {
    if (!PlayLayer::get()) {
        log::warn("[PauseLayer] onResume called but PlayLayer::get() is null");
        // No hacer removeFromParentAndCleanup — dejar que el sistema lo maneje
        return;
    }
    // ...
}
```

---

#### P3.3.2 — **CRITICO:** `onRestart/onRestartFull/onNormalMode/onPracticeMode` no notifican a PauseZoomManager

**Lineas:** 1098-1128

```cpp
void onRestart(CCObject* sender) {
    if (!PlayLayer::get()) return;
    PauseLayer::onRestart(sender);  // ← No llama notifyPauseClosing()
}
```

**Problema:** Cuando el usuario presiona Restart, el PauseLayer se destruye pero **no se llama `paimon::notifyPauseClosing()`**. Esto deja:
- `PauseZoomManager::m_isPaused = true`
- `paimon::isPauseZoomHidden() = true` (si estaba en zoom)

El proximo nivel que se juegue tendra el PauseLayer invisible hasta que se pause de nuevo.

**Severidad:** ALTA

**Fix sugerido:**
```cpp
void onRestart(CCObject* sender) {
    if (!PlayLayer::get()) return;
    paimon::notifyPauseClosing();  // ← Agregar esto
    PauseLayer::onRestart(sender);
}
```

Aplicar lo mismo a `onRestartFull`, `onNormalMode`, `onPracticeMode`.

---

#### P3.3.3 — `processSelectedFile()` lee archivos completos en memoria

**Lineas:** 647-936

```cpp
std::ifstream pngFile(selectedPath, std::ios::binary | std::ios::ate);
size_t fileSize = (size_t)pngFile.tellg();
std::vector<uint8_t> pngData(fileSize);
pngFile.read(reinterpret_cast<char*>(pngData.data()), fileSize);
```

**Problema:** Si el usuario selecciona un archivo de **varios GB**, el mod intentara cargarlo completo en RAM, causando OOM.

**Mitigacion actual:** Solo se permite seleccionar imagenes (pickImage) o media (pickMedia para mods).

**Severidad:** BAJA

---

#### P3.3.4 — `findButtonMenu()` puede fallar con mods de pausa personalizados

**Lineas:** 126-179

```cpp
auto findButtonMenu = [this](char const* id, bool rightSide) -> CCMenu* {
    // Busca por ID primero, luego por heuristica
    // ...
    if (best && bestScore < 5.f) {
        log::warn("PauseLayer fallback menu found but contains no known buttons");
        return nullptr;
    }
};
```

**Problema:** Mods como **Compact Pause Menu** o **BetterPause** reemplazan completamente el layout del PauseLayer. La heuristica puede no encontrar los botones conocidos.

**Mitigacion actual:** `setHookPriorityAfterPost("PauseLayer::customSetup", "geode.node-ids")` — corre despues de que otros mods hayan modificado el menu.

**Severidad:** MEDIA

---

### 3.5 Cosas Bien Hechas en PauseLayer.cpp

1. **WeakRef en callbacks de file picker** — Previene UAF si el popup se cierra
2. **Watchdog de 8 segundos** — `captureSafetyRestore()` evita UI congelada
3. **UnscheduleAllForTarget en onExit()** — Limpia todos los timers
4. **Validacion de magic bytes MP4** — Verifica `ftyp` al offset 4
5. **Doble-check de parent** — `getParent()` en callbacks async
6. **Mutua exclusion capture-keybind vs boton** — `isCaptureInProgress()`

---

## 4. EndLevelLayer.cpp — Analisis

### 4.1 Funcionalidad

```cpp
class $modify(ForYouEndLevelLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        // Diferir tracker al proximo tick para no participar del
        // stack de levelComplete (donde el juego dispara achievements).
        Loader::get()->queueInMainThread([levelID]() {
            ForYouTracker::get().onLevelComplete(pl->m_level);
        });
    }
};
```

**Observacion:** Muy simple, no hay problemas obvios. El `queueInMainThread` es correcto para evitar interferir con el stack de achievements.

---

## 5. Interaccion PlayLayer <-> PauseLayer

### 5.1 Diagrama de Secuencia — Pausa + Zoom

```
Usuario presiona Esc
    |
    v
PlayLayer::pauseGame(true)
    ├── PauseZoomManager::onPause()
    │   ├── m_isPaused = true
    │   └── ensurePauseZoomTicker()
    │
    └── PauseLayer::customSetup()
        ├── paimon::setActivePauseLayer(this)
        ├── paimon::setPauseZoomHidden(false)
        └── Agrega botones de captura
    |
    v
[Frames siguientes] PauseZoomTickerNode::update()
    ├── Detecta PauseLayer en escena
    ├── Si zoom > 1.01: hidePauseMenu()
    │   ├── setVisible(false)
    │   ├── setPauseZoomHidden(true)
    │   └── setTouchEnabled(false)
    └── Si zoom <= 1.01: restorePauseMenuVisible()

Usuario presiona Resume
    |
    v
PauseLayer::onResume()
    ├── paimon::notifyPauseClosing()
    │   └── PauseZoomManager::onResume()
    │       ├── resetPlayLayerZoom()
    │       ├── restorePauseMenuVisible()
    │       └── m_isPaused = false
    └── PauseLayer::onResume(sender)
```

### 5.2 Estados Compartidos

| Estado | Donde se setea | Donde se lee | Riesgo |
|--------|---------------|--------------|--------|
| `gCaptureInProgress` | PlayLayer, PauseLayer | PlayLayer, PauseLayer | Race condition si no hay mutex |
| `paimon::isPauseZoomHidden()` | PauseZoomManager | CCNode::visit() | Si queda en true, PauseLayer invisible |
| `paimon::getActivePauseLayer()` | PauseLayer::customSetup | PauseZoomManager, CCNode::visit | Dangling si no se limpia en onExit |

---

## 6. Recomendaciones

### 6.1 Fixes Inmediatos (Alta Prioridad)

1. **Agregar `notifyPauseClosing()` en onRestart/onRestartFull/onNormalMode/onPracticeMode**
   - Archivo: `PauseLayer.cpp`
   - Lineas: 1098-1128

2. **Revisar `removeFromParentAndCleanup` en onResume cuando PlayLayer es null**
   - Archivo: `PauseLayer.cpp`
   - Linea: 1082
   - Considerar solo `return` en lugar de `removeFromParentAndCleanup`

### 6.2 Fixes Mediana Prioridad

3. **Verificar `m_isPaused` en `captureScreenshot()`**
   - Archivo: `PlayLayer.cpp`
   - Linea: 893
   - Agregar guard: `if (!this->m_isPaused) { gCaptureInProgress.store(false); return; }`

4. **Mejorar deteccion de PauseLayer con multiple instancias**
   - Archivo: `PlayLayer.cpp`
   - Lineas: 377-395
   - Usar `paimon::getActivePauseLayer()` como fuente de verdad

### 6.3 Mejoras de Compatibilidad

5. **Testear con mods de pausa populares:**
   - `prevter.compact-pause-menu`
   - `geode.better-pause`
   - `globed.globed`

6. **Considerar hook de `CCScene::visit()` en lugar de `CCNode::visit()`**
   - Menos nodos afectados, mismo efecto

---

## 7. Conclusion

El sistema de gameplay de Paimbnails es **sofisticado y bien defendido**. Los desarrolladores claramente pasaron tiempo debuggeando race conditions y edge cases (los comentarios extensos lo demuestran).

Los 2 problemas criticos encontrados son:
1. **Falta de `notifyPauseClosing()` en restart/mode change** — Deja el zoom system en estado inconsistente
2. **`removeFromParentAndCleanup` potencialmente peligroso en onResume** — Puede causar double-free

Ambos son faciles de fixear y mejorarian significativamente la estabilidad del mod.
