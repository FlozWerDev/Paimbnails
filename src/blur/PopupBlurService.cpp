#include "PopupBlurService.hpp"

#include <Geode/Geode.hpp>
#include "../utils/Shaders.hpp"
#include "../core/Settings.hpp"
#include "../framework/compat/ModCompat.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#if defined(GEODE_IS_WINDOWS)
#include <windows.h>
#endif

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::popupblur {

// Key usada en user flags del popup para guardar el blur node asociado
static std::string const& blurNodeIdKey() {
    static std::string const key = Mod::get()->getID() + "/popup-blur-node-id";
    return key;
}

// Registry de popup -> (popupRef weak, blurRef weak). El CCNode* key es solo
// un token de identidad para lookups rapidos; la WeakRef del popup permite
// detectar cuando el popup murio sin pasar por cleanup() (el raw pointer queda
// dangling en ese caso pero el WeakRef devuelve null via lock()).
//
// Usamos el puntero como key porque los popups no tienen IDs unicos estables.
// Cuando un popup se destruye el raw pointer queda dangling PERO la entry se
// limpia en el siguiente watchdog tick porque popupRef.lock() retorna null.
//
// CRITICO: el mapa se heap-aloja y se "leakea" INTENCIONALMENTE. Motivo:
// contiene WeakRef<CCNode> cuyos destructores acceden a geode::WeakRefPool.
// Durante el shutdown del proceso (execute_onexit_table) el orden de
// destruccion de estaticos entre DLLs NO esta definido — si Geode.dll
// destruye su WeakRefPool antes que nuestro static map, los destructores de
// WeakRef leen memoria ya liberada (access violation en WeakRefPool::forget,
// leyendo offset 0x78 de un controlador null). El leak intencional evita
// que se ejecuten los destructores; el OS reclama la memoria al salir el
// proceso de todos modos.
//
// IMPORTANTE: NO guardamos WeakRef del popup — muchos tipos de popup viven
// fuera del WeakRefPool de geode (FLAlertLayer creado con autorelease sin
// pasar por createNode, popups custom del juego, etc.) y WeakRef::lock()
// sobre ellos crashea en WeakRefController::isManaged con out_of_range.
// En su lugar guardamos el raw popupPtr SOLO como identidad (nunca se
// desreferencia) y un Ref<CCNode> strong al blur (nuestro nodo, lo
// controlamos). La deteccion de popup muerto se hace por comparacion de
// punteros contra los children del parent del blur — nunca desreferenciamos
// el popupPtr, que puede estar dangling.
struct RegistryEntry {
    CCNode* popupPtr = nullptr;    // identidad solamente, nunca se desreferencia
    Ref<CCNode> blurRef;            // strong ref a NUESTRO blur node
    CCNode* parentPtr = nullptr;    // snapshot del parent al registrar — identidad
    float ageSeconds = 0.f;         // tiempo que lleva el blur vivo
    bool fadingOut = false;         // true si ya iniciamos fade-out — evita doble fade
};

static std::unordered_map<CCNode*, RegistryEntry>& blurRegistry() {
    static auto* map = new std::unordered_map<CCNode*, RegistryEntry>();
    return *map;
}

// ─────────────────────────────────────────────────────────────────────
// Flash overlay registry (perf optimizacion)
// ─────────────────────────────────────────────────────────────────────
//
// En lugar de DFS por toda la escena buscando flash overlays con blend
// additive en cada captura, el codigo que crea el flash se registra aqui.
// Capture cost por popup: O(K) sobre flashes activos (0-2 tipicamente)
// en lugar de O(N) sobre todos los nodos de la escena.
//
// Usamos Ref<CCNode> (strong reference) para mantener el nodo vivo mientras
// esta en el registry. Esto evita dangling pointers cuando CCRemoveSelf
// destruye el nodo — con Ref, el nodo sigue vivo (retain count > 0) y
// podemos llamar getParent() de forma segura para detectar que ya fue
// removido de la escena. El auto-prune en captureSceneTexture limpia
// entries sin parent.
//
// Heap-leak intencional por el mismo motivo que blurRegistry: evitar
// destruccion de Ref durante shutdown con orden indefinido entre DLLs.
static std::vector<Ref<CCNode>>& flashRegistry() {
    static auto* vec = new std::vector<Ref<CCNode>>();
    return *vec;
}

// ─────────────────────────────────────────────────────────────────────
// Snapshot cache — reusa la captura entre popups consecutivos
// ─────────────────────────────────────────────────────────────────────
//
// Cuando el usuario abre 2-3 popups en rapida sucesion (ej: click en
// boton del menu → popup → boton del popup → popup), capturar la escena
// completa cada vez es desperdicio: la escena no cambia entre frames
// consecutivos (el popup que se acaba de abrir tapa todo de todas formas).
//
// Cacheamos la ultima captura por ~250ms; si se pide una nueva dentro
// de esa ventana y el FBO sigue valido, lo reusamos. Esto elimina por
// completo el costo del segundo popup en cascada (typical user flow).
struct SnapshotCache {
    Ref<CCTexture2D> tex;
    CCSize size = CCSizeZero;
    double frameTime = -1.0;
    int sceneStamp = 0;       // CCDirector frame counter — invalida si cambia mucho
};
static SnapshotCache& snapshotCache() {
    static SnapshotCache c;
    return c;
}

// ─────────────────────────────────────────────────────────────────────
// Blur result cache — reusa el sprite blurreado entre popups
// ─────────────────────────────────────────────────────────────────────
//
// Aun mejor que cachear el snapshot: cachear el blur ya computado.
// Dos popups en cascada usan el mismo fondo blureado (el segundo se
// abre cuando el primero ya tapa toda la escena, asi que la captura
// del primero sigue siendo valida).
//
// El sprite blurreado puede ser referenciado por multiples blur nodes
// (CCSprite::createWithTexture comparte la textura). Solo guardamos la
// textura; el sprite se construye fresco cada vez.
struct BlurResultCache {
    Ref<CCTexture2D> blurredTex;
    CCSize blurredSize = CCSizeZero;
    std::string style;
    float intensity = -1.f;
    float darkness = -1.f;
    double frameTime = -1.0;
    // Token de la captura — solo es valido si la captura no cambio.
    void* snapshotToken = nullptr;
};
static BlurResultCache& blurResultCache() {
    static BlurResultCache c;
    return c;
}

void registerFlashOverlay(CCNode* flashLayer) {
    if (flashLayer) {
        auto& reg = flashRegistry();
        // Evitar duplicados
        for (auto& ref : reg) {
            if (ref.data() == flashLayer) return;
        }
        reg.emplace_back(flashLayer);
    }
}

void unregisterFlashOverlay(CCNode* flashLayer) {
    if (flashLayer) {
        auto& reg = flashRegistry();
        reg.erase(
            std::remove_if(reg.begin(), reg.end(),
                [flashLayer](Ref<CCNode> const& ref) { return ref.data() == flashLayer; }),
            reg.end());
    }
}

CCSprite* reuseBlurForSnapshot(CCTexture2D* snapshot, std::string const& style,
                                float intensity, float darkness) {
    if (!snapshot) return nullptr;
    auto& cache = blurResultCache();
    auto* cachedTex = cache.blurredTex.data();
    if (!cachedTex) return nullptr;
    if (cache.snapshotToken != static_cast<void*>(snapshot)) return nullptr;
    if (cache.style != style) return nullptr;
    if (std::abs(cache.intensity - intensity) > 0.01f) return nullptr;
    if (std::abs(cache.darkness - darkness) > 0.01f) return nullptr;

    auto* spr = CCSprite::createWithTexture(cachedTex);
    if (spr) {
        spr->setContentSize(cache.blurredSize);
        // ── FIX (popup blur invertido al reusar de cache) ──
        // La textura cacheada proviene de un CCRenderTexture (FBO). Cuando se
        // creo el sprite original en Shaders::createPopupBlurredSprite se le
        // aplico setFlipY(true) para compensar la orientacion vertical del
        // FBO. Un sprite nuevo creado de la misma textura SIN setFlipY se
        // renderiza al backbuffer espejado en Y — el blur sale "boca abajo"
        // y la imagen no coincide con la escena real.
        // Replicamos el flip aqui para que el reuso del cache mantenga la
        // misma orientacion que el sprite original.
        spr->setFlipY(true);
    }
    return spr;
}

void storeBlurForSnapshot(CCTexture2D* snapshot, CCSprite* blurredSprite,
                           std::string const& style, float intensity, float darkness) {
    if (!snapshot || !blurredSprite || !blurredSprite->getTexture()) return;
    auto& cache = blurResultCache();
    cache.blurredTex = Ref<CCTexture2D>(blurredSprite->getTexture());
    cache.blurredSize = blurredSprite->getContentSize();
    cache.style = style;
    cache.intensity = intensity;
    cache.darkness = darkness;
    cache.snapshotToken = static_cast<void*>(snapshot);
    auto* director = CCDirector::get();
    cache.frameTime = director ? static_cast<double>(director->getTotalFrames()) : -1.0;
}

// ─────────────────────────────────────────────────────────────────────
// Watchdog — safety net para popups que mueren sin cleanup()
// ─────────────────────────────────────────────────────────────────────
//
// Muchos popups se cierran por caminos que NO disparan ni keyBackClicked ni
// onExit hookeados:
//   - Popups custom que removeFromParent() directo
//   - Popups que no heredan de FLAlertLayer/ProfilePage/SetupTriggerPopup
//   - Scene transitions que retienen el popup brevemente mientras el blur
//     queda en la scene nueva
//   - Crashes / early-returns en codigo del popup que saltan el destructor
//
// El watchdog tickea cada ~0.15s sobre el registry activo y detecta blurs
// huerfanos:
//   - popup murio (WeakRef null) → fade-out y eliminar blur
//   - popup ya no esta en la scene (sin parent) → fade-out
//   - popup tiene running = false (removeFromParent sin cleanup) → fade-out
//   - blur quedo en una scene distinta a la running scene → eliminar inmediato
//
// El watchdog se schedule cuando el primer blur entra al registry y se
// unschedule cuando el registry queda vacio. Cero costo si no hay popups.

class WatchdogTarget : public cocos2d::CCObject {
public:
    void tick(float dt);
};

static bool g_watchdogScheduled = false;

static WatchdogTarget* getWatchdogTarget() {
    static WatchdogTarget* target = []() {
        auto* t = new WatchdogTarget();
        t->autorelease();
        t->retain();  // retain permanente — sobrevive scene changes
        return t;
    }();
    return target;
}

// Fade-out animado + removeFromParent. Usado cuando el watchdog detecta un
// blur huerfano y queremos removerlo sin corte visual brusco.
static void fadeAndRemoveBlurNode(CCNode* blur, float duration) {
    if (!blur || !blur->getParent()) return;
    blur->stopAllActions();
    if (duration <= 0.01f) {
        blur->removeFromParent();
        return;
    }
    blur->runAction(CCSequence::create(
        CCFadeTo::create(duration, 0),
        CCCallFunc::create(blur, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

static void scheduleWatchdogIfNeeded() {
    if (g_watchdogScheduled) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    // 250ms — balance entre detection latency y CPU cost. Antes era 150ms,
    // pero los blurs huerfanos no son criticos (cleanupAllActive en CCScene
    // ::cleanup ya los barre en transiciones de escena). 250ms reduce la
    // tick frequency 40% sin impacto visible.
    scheduler->scheduleSelector(
        schedule_selector(WatchdogTarget::tick),
        getWatchdogTarget(),
        0.25f,
        false
    );
    g_watchdogScheduled = true;
}

static void unscheduleWatchdogIfIdle() {
    if (!g_watchdogScheduled) return;
    if (!blurRegistry().empty()) return;
    auto* director = CCDirector::get();
    if (!director) return;
    auto* scheduler = director->getScheduler();
    if (!scheduler) return;
    scheduler->unscheduleSelector(
        schedule_selector(WatchdogTarget::tick),
        getWatchdogTarget()
    );
    g_watchdogScheduled = false;
}

// Busca la scene root de un nodo subiendo por getParent() hasta la cima.
// Usado para detectar blurs que quedaron en una scene vieja tras una
// transicion (el blur todavia tiene parent pero ese parent es un scene
// que ya no esta running).
static CCNode* findSceneRoot(CCNode* node) {
    if (!node) return nullptr;
    CCNode* cur = node;
    while (cur->getParent()) cur = cur->getParent();
    return cur;
}

// Comprueba si algun hijo directo de `parent` es exactamente el puntero
// `popupPtr`. SOLO compara punteros — nunca desreferencia popupPtr, por lo
// que es seguro aunque popupPtr sea dangling. Si el popup ya fue removido
// del parent (closed), devuelve false.
static bool popupStillChildOf(CCNode* parent, CCNode* popupPtr) {
    if (!parent || !popupPtr) return false;
    auto* children = parent->getChildren();
    if (!children) return false;
    int count = children->count();
    for (int i = 0; i < count; ++i) {
        if (children->objectAtIndex(i) == static_cast<cocos2d::CCObject*>(popupPtr)) {
            return true;
        }
    }
    return false;
}

void WatchdogTarget::tick(float dt) {
    auto& reg = blurRegistry();
    if (reg.empty()) {
        unscheduleWatchdogIfIdle();
        return;
    }

    auto* director = CCDirector::get();
    auto* runningScene = director ? director->getRunningScene() : nullptr;

    // Iteramos en 2 fases: primero recolectar ops, despues aplicarlas.
    // Evita iterator invalidation si un fade-out callback dispara cleanup.
    std::vector<CCNode*> toRemoveFromRegistry;
    std::vector<CCNode*> toFadeBlurs;
    std::vector<CCNode*> toInstantRemoveBlurs;

    for (auto& [popupKey, entry] : reg) {
        entry.ageSeconds += dt;

        // blurRef es Ref<CCNode> (strong). El blur no muere con el popup
        // porque lo retenemos. data() solo retorna null si nunca se asigno.
        auto* blur = entry.blurRef.data();

        // 1) Blur ya no existe o fue removido del scene graph → purga entry
        if (!blur || !blur->getParent()) {
            toRemoveFromRegistry.push_back(popupKey);
            continue;
        }

        // 2) Blur quedo en scene distinta a la running (scene transition
        //    sin cleanup). Remove inmediato para no dejar blur fantasma.
        if (runningScene) {
            CCNode* blurScene = findSceneRoot(blur);
            if (blurScene && blurScene != runningScene) {
                toInstantRemoveBlurs.push_back(blur);
                toRemoveFromRegistry.push_back(popupKey);
                continue;
            }
        }

        // Si ya iniciamos fade para esta entry, esperamos a que termine
        // (el fade sacara el blur del parent y en el siguiente tick
        // la entry se purga por la rama 1).
        if (entry.fadingOut) continue;

        // 3) Detectar popup cerrado SIN desreferenciar el popupPtr (puede
        //    ser dangling si el popup ya fue destruido). Usamos la relacion
        //    parent→children: si el popup ya no esta entre los hijos del
        //    parent donde lo registramos, esta cerrado.
        //
        //    CASOS:
        //    a) popup esta en children del parent → popup vivo y abierto
        //    b) popup no esta → cerrado (removeFromParent o destruido)
        //
        //    Damos una gracia de 0.2s para evitar cortar popups que
        //    re-parentan transitoriamente durante animacion de entrada.
        CCNode* parent = entry.parentPtr;
        if (!parent) {
            // parent se capturo null en algun punto raro — declarar muerto
            toFadeBlurs.push_back(blur);
            entry.fadingOut = true;
            continue;
        }

        // Si el parent del blur cambio, asumimos scene-level reshuffle
        // (caso raro). Usamos el parent actual del blur como proxy.
        CCNode* blurParent = blur->getParent();
        if (blurParent != parent) {
            parent = blurParent;
            entry.parentPtr = blurParent;
        }

        bool popupPresent = popupStillChildOf(parent, popupKey);
        if (!popupPresent && entry.ageSeconds > 0.2f) {
            toFadeBlurs.push_back(blur);
            entry.fadingOut = true;
            continue;
        }
    }

    // Aplicar ops
    for (auto* b : toInstantRemoveBlurs) {
        if (b && b->getParent()) b->removeFromParent();
    }
    for (auto* b : toFadeBlurs) {
        fadeAndRemoveBlurNode(b, 0.18f);
    }
    for (auto* k : toRemoveFromRegistry) {
        reg.erase(k);
    }

    if (reg.empty()) {
        unscheduleWatchdogIfIdle();
    }
}

// Limpia entradas muertas del registry — blurs que ya no estan en la escena.
// NO desreferencia el popupPtr (puede ser dangling).
static void pruneDeadEntries() {
    auto& reg = blurRegistry();
    for (auto it = reg.begin(); it != reg.end();) {
        auto* blur = it->second.blurRef.data();
        if (!blur || !blur->getParent()) {
            it = reg.erase(it);
        } else {
            ++it;
        }
    }
}

Config getConfig() {
    Config cfg;
    cfg.enabled = paimon::settings::popupblur::enabled();
    // Forzar paimonblur siempre (el usuario lo pidio asi).
    // El gaussian se mantiene en el codigo como fallback automatico cuando
    // los shaders Kawase no pueden cargar (manejado dentro del shader),
    // pero aqui no se setea desde settings.
    cfg.style = "paimonblur";
    cfg.intensity = std::max(0.1f, static_cast<float>(paimon::settings::popupblur::intensity()));
    cfg.darkness = std::clamp(static_cast<float>(paimon::settings::popupblur::darkness()), 0.0f, 1.0f);
    return cfg;
}

void registerExternalBlur(CCNode* popup, CCNode* blurNode) {
    if (!popup || !blurNode) return;
    pruneDeadEntries();
    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(blurNode);
    entry.parentPtr = popup->getParent();
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();
}

std::vector<std::pair<CCNode*, bool>> hideAllActiveBlurs() {
    pruneDeadEntries();

    std::vector<std::pair<CCNode*, bool>> hidden;
    hidden.reserve(blurRegistry().size());

    // Copia entries a limpiar para no mutar durante iteracion
    std::vector<CCNode*> deadKeys;
    for (auto& [popupPtr, entry] : blurRegistry()) {
        auto* blur = entry.blurRef.data();
        if (!blur || !blur->getParent()) {
            deadKeys.push_back(popupPtr);
            continue;
        }
        if (blur->isVisible()) {
            hidden.emplace_back(blur, true);
            blur->setVisible(false);
        }
    }
    for (auto* k : deadKeys) blurRegistry().erase(k);
    return hidden;
}

void restoreHiddenBlurs(std::vector<std::pair<CCNode*, bool>> const& hidden) {
    for (auto& [blur, wasVisible] : hidden) {
        if (blur) blur->setVisible(wasVisible);
    }
}

// ─────────────────────────────────────────────────────────────────────
// Captura de escena centralizada (Fase 1 fix + Fase 2 optimizacion)
// ─────────────────────────────────────────────────────────────────────

CCTexture2D* captureSceneTexture(CCNode* popupToHide, CCSize& outSize) {
    auto* director = CCDirector::get();
    if (!director) return nullptr;

    auto* scene = director->getRunningScene();
    auto winSize = director->getWinSize();
    if (!scene || winSize.width <= 0.f || winSize.height <= 0.f) return nullptr;

    // ── Snapshot reuse (perf optimizacion) ──
    // Si tenemos una captura reciente (<300ms) y la escena no cambio
    // significativamente, la reusamos. Tipico cuando se abren popups en
    // cascada (popup → boton → popup): el segundo popup ahorra ~5-15ms
    // de captura completa (incluyendo glFinish() en GPUs lentas).
    //
    // 300ms cubre monitores de alta frecuencia (144-240Hz) donde 12 frames
    // serian solo ~50-83ms. El contenido de la escena no cambia en ese
    // intervalo para popups consecutivos (el popup tapa toda la escena).
    {
        auto& cache = snapshotCache();
        auto* tex = cache.tex.data();
        if (tex && cache.size.width >= winSize.width - 1.f &&
            cache.size.height >= winSize.height - 1.f) {
            double now = static_cast<double>(director->getTotalFrames());
            if (cache.frameTime > 0 && (now - cache.frameTime) < 18.0) {
                outSize = winSize;
                return tex;
            }
        }
    }

    // Captura a resolucion completa. El downscale se hace en el blur pass
    // (createPopupBlurredSprite ya tiene su propio kPopupMaxDim = 720).
    // Capturar a menor resolucion con matrix scale rompe sprites con draw()
    // manual (PaimonShaderSprite, AnimatedGIFSprite) porque sus vertex
    // positions son absolutas y no se transforman correctamente con la
    // matrix stack modificada.
    int captureW = static_cast<int>(std::round(winSize.width));
    int captureH = static_cast<int>(std::round(winSize.height));
    if (captureW <= 0 || captureH <= 0) return nullptr;

    // ── CCRenderTexture con depth+stencil (fix cuadrado blanco con imagenes) ──
    // La escena contiene CCClippingNode (thumbnails en LevelCell) que usan el
    // stencil buffer para recortar. La version basica de CCRenderTexture::create
    // NO crea stencil buffer — el clipping falla silenciosamente y los sprites
    // dentro del clipping node no se renderizan, dejando visible el fondo
    // (blanco o garbage) en su lugar.
    // Usamos la version con GL_DEPTH24_STENCIL8 que crea un renderbuffer
    // combinado depth+stencil, permitiendo que CCClippingNode funcione
    // correctamente dentro del FBO de captura.
    auto* rt = CCRenderTexture::create(
        captureW, captureH,
        kCCTexture2DPixelFormat_RGBA8888,
        GL_DEPTH24_STENCIL8);
    if (!rt) return nullptr;

    // ── Validacion de FBO (Fase 1 fix) ──
    // CCRenderTexture::create puede retornar un objeto valido pero con un FBO
    // incompleto (status != GL_FRAMEBUFFER_COMPLETE). Si el FBO esta incompleto,
    // el render va a un buffer indefinido → blanco. Verificamos antes de usarlo.
    rt->begin();
    GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    rt->end();
    if (fboStatus != GL_FRAMEBUFFER_COMPLETE) {
        geode::log::warn("[PopupBlur] FBO incomplete (status=0x{:X}), skipping capture", fboStatus);
        return nullptr;
    }

    // Oculta TODOS los blur nodes activos durante la captura para evitar
    // feedback loop "blur sobre blur" que produce lavado acumulado cuando
    // se abre un segundo popup encima de uno que ya tiene blur.
    auto hiddenBlurs = hideAllActiveBlurs();

    // Oculta el propio popup si ya esta en la escena. Sin esto el popup
    // aparece blurreado sobre si mismo.
    bool hadSelfVisible = false;
    bool selfInScene = false;
    if (popupToHide && popupToHide->getParent()) {
        selfInScene = true;
        hadSelfVisible = popupToHide->isVisible();
        if (hadSelfVisible) popupToHide->setVisible(false);
    }

    // ── Ocultar flash layers (perf optimizacion) ──
    // Consultamos el flashRegistry. Con Ref<CCNode> los nodos se mantienen
    // vivos mientras estan en el registry, asi que getParent() es siempre
    // seguro (no hay dangling pointers). Auto-prune: entries sin parent
    // son nodos ya removidos de la escena por CCRemoveSelf/removeFromParent.
    std::vector<std::pair<cocos2d::CCNode*, bool>> hiddenFlashes;
    {
        auto& reg = flashRegistry();
        if (!reg.empty()) {
            // Prune entries sin parent (nodos ya removidos de la escena)
            reg.erase(
                std::remove_if(reg.begin(), reg.end(),
                    [](Ref<CCNode> const& ref) { return !ref || !ref->getParent(); }),
                reg.end());

            hiddenFlashes.reserve(reg.size());
            for (auto& ref : reg) {
                CCNode* flash = ref.data();
                if (!flash->isVisible()) continue;
                bool shouldHide = true;
                if (auto* layer = typeinfo_cast<CCLayerColor*>(flash)) {
                    shouldHide = layer->getOpacity() > 0;
                }
                if (shouldHide) {
                    hiddenFlashes.emplace_back(flash, true);
                    flash->setVisible(false);
                }
            }
        }
    }

    // ── FIX (popup blur thumbnail en esquina inferior izquierda) ──
    // beginWithClear en lugar de begin() + glClear manual. Motivo:
    // - begin() por si solo NO limpia el FBO; el framebuffer adjuntado
    //   contiene residuo (garbage en primer uso, o frame viejo). Cuando
    //   la escena tiene regiones con alpha (thumbnails, fondos custom)
    //   el alpha blending contra ese residuo produce los recuadros blancos
    //   y ghosting reportados.
    // - beginWithClear hace el clear ATOMICAMENTE despues de bindear el FBO
    //   desde dentro de Cocos, evitando race con el estado GL que manipulan
    //   otros shaders del mod (shader effects del editor, framebuffer capture,
    //   etc). El glClear manual es sensible al orden exacto de bind.
    // - La version con 5 parametros limpia color + depth + stencil. El stencil
    //   DEBE estar en 0 para que CCClippingNode funcione correctamente dentro
    //   del FBO — si tiene residuo de un frame anterior, el clipping falla y
    //   los sprites dentro del clipping node no se renderizan (cuadrado blanco
    //   donde deberia estar la imagen).
    //
    // ── IMPORTANTE: NO sobreescribir el viewport despues de beginWithClear ──
    // RobTop modifico CCRenderTexture para que el backing GL texture se aloje
    // a `captureW * contentScaleFactor × captureH * contentScaleFactor` pixeles
    // fisicos (con High Graphics scale=4 -> 2276x1280 para un FBO de 569x320
    // puntos). begin() internamente hace `glViewport(0, 0, pixelsWide, pixelsHigh)`,
    // mapeando los puntos logicos a TODO el FBO fisico via la projection que
    // empuja en la matrix stack — exactamente lo que queremos.
    //
    // Bug previo: este codigo llamaba `glViewport(0, 0, captureW, captureH)`
    // pasando puntos logicos despues de beginWithClear. Eso restringia el
    // render al sub-rectangulo bottom-left del FBO (1/16 del area con scale=4).
    // El resto del FBO quedaba con el clear color (negro), y como el sprite
    // resultante muestrea TODO el FBO al cubrir la pantalla, el usuario veia
    // una mini-imagen de la escena en la esquina inferior izquierda con el
    // resto en negro — exactamente el sintoma reportado.
    rt->beginWithClear(0.f, 0.f, 0.f, 1.f, 0.f, 0);
    scene->visit();
    rt->end();

    // Restaurar flash layers
    for (auto& [flash, wasVisible] : hiddenFlashes) {
        if (flash) flash->setVisible(wasVisible);
    }

    // ── Sincronizacion GPU (perf optimizacion) ──
    // glFinish() bloquea CPU hasta que TODOS los comandos GL terminen
    // (~1-3ms). Solo es necesario en GPUs con deferred rendering (mobile,
    // Intel HD viejos). En desktop moderno (NVIDIA/AMD desde 2015+,
    // Intel UHD+) glFlush es suficiente porque el siguiente bind de FBO
    // (en applyBlurPass) crea barrera implicita.
    //
    // Movemos glFinish a mobile-only y usamos glFlush en desktop. Si
    // alguien reporta cuadrados blancos en un GPU especifico podemos
    // anadir un setting para forzar glFinish.
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    glFinish();
#else
    glFlush();
#endif

    // Restaura visibilidades en orden inverso
    if (popupToHide && selfInScene && hadSelfVisible) popupToHide->setVisible(true);
    restoreHiddenBlurs(hiddenBlurs);

    auto* tex = rt->getSprite() ? rt->getSprite()->getTexture() : nullptr;
    if (!tex) return nullptr;

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    tex->setTexParameters(&params);

    // ── Diagnostico (una sola vez por sesion) ──
    // Confirma que el FBO retornado tiene las dimensiones esperadas. Si
    // alguno de estos valores difiere de winSize hay un mismatch que
    // explicaria el bug "blur ligeramente mas grande". Solo loggea la
    // primera vez para no spammear la consola en cada popup.
    {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            auto fboSpriteSize = rt->getSprite()->getContentSize();
            auto fboSpriteRect = rt->getSprite()->getTextureRect();
            geode::log::info("[PopupBlur::capture] winSize={}x{} captureWH={}x{} "
                             "fboSprite.contentSize={}x{} fboSprite.rect={}x{} "
                             "tex.contentSize={}x{}",
                winSize.width, winSize.height,
                captureW, captureH,
                fboSpriteSize.width, fboSpriteSize.height,
                fboSpriteRect.size.width, fboSpriteRect.size.height,
                tex->getContentSize().width, tex->getContentSize().height);
        }
    }

    outSize = winSize;

    // Cachea para reuso por la siguiente captura cercana
    {
        auto& cache = snapshotCache();
        cache.tex = Ref<CCTexture2D>(tex);
        cache.size = winSize;
        cache.frameTime = static_cast<double>(director->getTotalFrames());
    }

    return tex;
}

CCLayerColor* buildBlurNode(CCSprite* blurred, CCSize const& winSize, Config const& cfg) {
    // ── PERF: Root como CCLayerColor con ignoreAnchorPointForPosition ──
    // Necesitamos CCRGBAProtocol en el root para fade-in/out via CCFadeTo.
    // Pero CCLayerColor::draw() dibuja un quad fullscreen cada frame incluso
    // con alpha=0 — puro overdraw.
    //
    // Solucion: usamos CCLayerColor pero deshabilitamos su draw via
    // setContentSize(0,0) — CCLayerColor con size 0 no genera vertices.
    // Los hijos se posicionan con coordenadas absolutas (winSize).
    // El cascadeOpacity sigue funcionando para propagar el fade.
    auto root = CCLayerColor::create(ccc4(0, 0, 0, 0));
    if (!root) return nullptr;
    root->setContentSize(CCSizeZero);
    root->ignoreAnchorPointForPosition(true);
    root->setPosition(CCPointZero);
    root->setAnchorPoint(ccp(0.f, 0.f));
    root->setID("paimon-popup-blur-root"_spr);
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(255);

    // ── FIX (popup blur thumbnail en esquina) ──
    // Anteriormente este codigo escalaba el sprite via setScaleX/setScaleY(neg)
    // basado en `blurred->getContentSize()`. Eso fallaba en algunos cases
    // donde el sprite llegaba con contentSize en pixeles fisicos del FBO
    // (RobTop modifico CCRenderTexture con m_fInternalScaleX/Y, que escala
    // el backing GL texture por el contentScaleFactor de high graphics).
    //
    // Resultado visible: el blur se dibujaba en una porcion (~1/scale) en una
    // esquina de la pantalla (esquina inferior izquierda), no llenando todo.
    //
    // Solucion robusta: envolver el sprite en un CCNode wrapper con
    // setContentSize(winSize) explicito. El sprite hijo se posiciona en
    // (winSize/2, winSize/2) con anchor (0.5, 0.5) y se escala usando
    // getTextureRect().size — que es el tamaño REAL del quad renderizado
    // (m_obRect del CCSprite), siempre coherente con el rendering.
    //
    // Si el sprite tiene flipY ya aplicado (createBlurredSprite, el path
    // "no upsample" de createPopupPaimonBlurredSprite), no necesitamos
    // setScaleY negativo.
    blurred->setOpacity(255);
    blurred->setID("paimon-popup-blur-final"_spr);

    {
        // ── FIX (popup blur "ligeramente mas grande") ──
        // El sprite blureado puede llegar con cualquier combinacion de:
        //   - texture rect en pixeles fisicos del FBO (m_fInternalScaleX > 1)
        //   - contentSize en puntos logicos
        //   - scale != 1 si vino del cache compartido
        //   - flipY en cualquier estado
        //
        // En lugar de adivinar la combinacion correcta a partir de
        // getTextureRect()/getContentSize() — que ha probado ser fragil —
        // forzamos el rect del sprite a representar la textura COMPLETA
        // explicitamente. Esto reescribe m_obRect a las dimensiones reales
        // de la textura GL, eliminando cualquier residuo de configuraciones
        // previas. Despues le damos contentSize=winSize y scale=1, de modo
        // que el sprite cubre EXACTAMENTE winSize sin escalado adicional.
        auto* texForRect = blurred->getTexture();
        if (texForRect) {
            auto texSize = texForRect->getContentSize();
            // Si la textura reporta tamano valido, usar ese rect (cubre toda
            // la textura). Si no, fallback a winSize.
            if (texSize.width > 0.f && texSize.height > 0.f) {
                blurred->setTextureRect(CCRect(0.f, 0.f, texSize.width, texSize.height));
            } else {
                blurred->setTextureRect(CCRect(0.f, 0.f, winSize.width, winSize.height));
            }
        }

        // Reset scale a 1 — cualquier scale heredado se va a recomputar abajo.
        blurred->setScale(1.0f);

        // ── FIX (orientacion vertical del blur) ──
        // TODOS los blurs llegan desde un FBO (Y-invertido respecto al
        // backbuffer). Las funciones que producen el sprite final aplican
        // setFlipY(true) para compensar. Si llega sin flipY (caso defensivo),
        // forzamos true para que siempre veamos la orientacion correcta.
        if (!blurred->isFlipY()) {
            blurred->setFlipY(true);
        }

        // Despues de setTextureRect, contentSize == size del rect. Calculamos
        // el scale que lleva ese contentSize a winSize. Como el rect cubre
        // toda la textura y la textura proviene de un FBO de winSize, el
        // scale tipicamente es 1.0 (en puntos logicos) — pero si la textura
        // esta en pixeles fisicos, este scale lo compensa exactamente.
        auto curContent = blurred->getContentSize();
        if (curContent.width <= 0.f) curContent.width = winSize.width;
        if (curContent.height <= 0.f) curContent.height = winSize.height;

        blurred->setAnchorPoint(ccp(0.5f, 0.5f));
        blurred->setPosition(winSize * 0.5f);
        blurred->setScaleX(winSize.width / curContent.width);
        blurred->setScaleY(winSize.height / curContent.height);

        // Log diagnostico — primera vez por sesion
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            geode::log::info("[PopupBlur::buildBlurNode] winSize={}x{} content={}x{} "
                             "scale={}x{} flipY={} tex.contentSize={}",
                winSize.width, winSize.height,
                curContent.width, curContent.height,
                blurred->getScaleX(), blurred->getScaleY(),
                blurred->isFlipY(),
                texForRect ? fmt::format("{}x{}", texForRect->getContentSize().width,
                                                   texForRect->getContentSize().height)
                           : std::string("null"));
        }
    }

    // ── PERF: Pre-bake darkness en el color del sprite ──
    if (cfg.darkness > 0.f) {
        float factor = std::clamp(1.0f - cfg.darkness, 0.0f, 1.0f);
        GLubyte c = static_cast<GLubyte>(std::round(factor * 255.f));
        blurred->setColor(ccc3(c, c, c));
    }

    root->addChild(blurred, 0);

    return root;
}

// Static fallback texture cacheada — solo se crea una vez. Antes se hacia
// `new CCTexture2D` por cada failure, lo que sumaba allocs/leaks bajo
// presion (popups que fallan en serie).
static CCTexture2D* getDarkFallbackTexture() {
    static Ref<CCTexture2D> s_tex = []() -> CCTexture2D* {
        auto* t = new CCTexture2D();
        unsigned char blackPixel[4] = {0, 0, 0, 255};
        if (!t->initWithData(blackPixel, kCCTexture2DPixelFormat_RGBA8888, 1, 1, CCSizeMake(1, 1))) {
            t->release();
            return nullptr;
        }
        return t;  // Ref<> retains, no autorelease aqui — vive para siempre
    }();
    return s_tex.data();
}

// ─────────────────────────────────────────────────────────────────────
// normalizeBlurSpriteToWinSize — ULTIMA LINEA DE DEFENSA contra blur mal escalado
// ─────────────────────────────────────────────────────────────────────
//
// Renderiza el sprite blureado a un FBO de exactamente winSize, sin importar
// como llego (orientacion, contentSize, scale, flipY). Devuelve un sprite
// nuevo con setFlipY(true) que cubre exactamente winSize en el rendering.
//
// Esto es necesario porque el sprite del blur puede llegar con:
//   - contentSize en pixeles fisicos (FBO con m_fInternalScaleX de RobTop)
//   - flipY en cualquier estado
//   - setScale != 1 si vino del cache compartido
//
// El FBO normalizado garantiza que el sprite final cubra winSize sin
// importar nada de lo anterior, eliminando el bug del thumbnail en esquina.
CCSprite* normalizeBlurSpriteToWinSize(CCSprite* input, CCSize const& winSize) {
    if (!input) return nullptr;
    if (winSize.width <= 0.f || winSize.height <= 0.f) return input;

    // Crear FBO del tamaño exacto winSize
    int w = static_cast<int>(std::round(winSize.width));
    int h = static_cast<int>(std::round(winSize.height));
    if (w <= 0 || h <= 0) return input;

    auto* rt = CCRenderTexture::create(w, h);
    if (!rt) {
        // Fallback: si no podemos crear el FBO, retornamos el input tal
        // cual y dependemos del scaling de buildBlurNode.
        return input;
    }

    ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    rt->getSprite()->getTexture()->setTexParameters(&params);

    // Configurar el sprite input para que cubra exactamente winSize cuando
    // se renderee al FBO. Usamos el rect del sprite (m_obRect en cocos)
    // que define el tamaño visual del quad.
    auto rectSize = input->getTextureRect().size;
    if (rectSize.width <= 0.f || rectSize.height <= 0.f) {
        rectSize = input->getContentSize();
    }
    if (rectSize.width <= 0.f || rectSize.height <= 0.f) {
        if (auto* tex = input->getTexture()) {
            rectSize = tex->getContentSize();
        }
    }
    if (rectSize.width <= 0.f) rectSize.width = winSize.width;
    if (rectSize.height <= 0.f) rectSize.height = winSize.height;

    input->setAnchorPoint({0.5f, 0.5f});
    input->setPosition(winSize * 0.5f);
    input->setScaleX(winSize.width / rectSize.width);
    input->setScaleY(winSize.height / rectSize.height);
    // NOTA: NO llamamos setFlipY aqui — usamos lo que tenia el input. Si
    // ya tenia flipY, el visit() al FBO lo aplica; el FBO sale Y-flipped
    // adicionalmente (CCRenderTexture usa coordenadas GL invertidas).
    // El sprite final con setFlipY(true) compensa eso.
    //
    // Pero si el input NO tenia flipY, queremos que el resultado final
    // SI tenga la orientacion correcta. La logica completa:
    //   input flipY=true → visit produce Y-natural en FBO → final flipY=true → invertido (mal)
    //   input flipY=false → visit produce Y-invertido en FBO → final flipY=true → correcto
    //
    // Para uniformizar, forzamos input->setFlipY(false) antes del visit.
    // Asi el FBO siempre contiene Y-invertido, y el final con setFlipY(true)
    // siempre produce orientacion correcta.
    input->setFlipY(false);

    rt->beginWithClear(0.f, 0.f, 0.f, 0.f);
    input->visit();
    rt->end();

    auto* finalSprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
    if (!finalSprite) return input;
    finalSprite->setAnchorPoint({0.5f, 0.5f});
    finalSprite->setFlipY(true);
    finalSprite->getTexture()->setTexParameters(&params);
    // Forzar contentSize a winSize para que getContentSize() reporte
    // valores en puntos logicos correctos.
    finalSprite->setContentSize(winSize);
    return finalSprite;
}

bool captureAndApply(CCNode* popup) {
    if (!popup) return false;
    auto cfg = getConfig();
    if (!cfg.enabled) return false;

    // Compatibilidad con mods que ya aplican blur global a todos los
    // popups (alphalaneous.blur_bg). Si ese mod esta activo, aplicar
    // nuestro blur encima causa doble FBO pass y artefactos visuales
    // (popups con blur pintado dos veces, vignette duplicada).
    if (paimon::compat::ModCompat::externalGlobalBlurActive()) {
        return false;
    }

    // El popup debe estar ya agregado a la escena para que el blur se inserte
    // como sibling detras de el.
    auto* parent = popup->getParent();
    if (!parent) return false;

    // Limpia blur previo para este popup (instant — no fade, es reemplazo)
    cleanup(popup);

    CCSize captureSize = CCSizeZero;
    auto* tex = captureSceneTexture(popup, captureSize);
    if (!tex || captureSize.width <= 0.f || captureSize.height <= 0.f) return false;

    float effectiveIntensity = cfg.intensity;
    if (cfg.style == "paimonblur") {
        effectiveIntensity = std::min(10.0f, cfg.intensity * 1.15f + 0.35f);
    }

    // ── Blur result cache ──
    // Si la captura es el mismo texture que la anterior y el config no
    // cambio, reusamos el blur sprite ya computado. Esto elimina por
    // completo los 4-6 FBO passes para popups en cascada (caso comun).
    CCSprite* blurred = nullptr;
    {
        auto& cache = blurResultCache();
        auto* cachedTex = cache.blurredTex.data();
        bool sameSnapshot = cache.snapshotToken == static_cast<void*>(tex);
        bool sameStyle = cache.style == cfg.style;
        bool sameIntensity = std::abs(cache.intensity - effectiveIntensity) < 0.01f;
        bool sameDarkness = std::abs(cache.darkness - cfg.darkness) < 0.01f;
        if (cachedTex && sameSnapshot && sameStyle && sameIntensity && sameDarkness) {
            blurred = CCSprite::createWithTexture(cachedTex);
            if (blurred) {
                // Restaurar size/anchor que setea buildBlurNode despues
                blurred->setContentSize(cache.blurredSize);
                // ── FIX (popup blur invertido al reusar de cache) ──
                // La textura cacheada es de un FBO. El sprite original tenia
                // setFlipY(true) — replicamos aqui o el blur sale invertido.
                blurred->setFlipY(true);
            }
        }
    }

    // Cache miss: computar blur sincronicamente (single popup case)
    if (!blurred) {
        if (cfg.style == "paimonblur") {
            blurred = Shaders::createPopupPaimonBlurredSprite(tex, captureSize, effectiveIntensity);
        } else {
            blurred = Shaders::createPopupBlurredSprite(tex, captureSize, effectiveIntensity);
        }

        // Guardar en cache para el siguiente popup en cascada
        if (blurred && blurred->getTexture()) {
            auto& cache = blurResultCache();
            cache.blurredTex = Ref<CCTexture2D>(blurred->getTexture());
            cache.blurredSize = blurred->getContentSize();
            cache.style = cfg.style;
            cache.intensity = effectiveIntensity;
            cache.darkness = cfg.darkness;
            cache.snapshotToken = static_cast<void*>(tex);
            cache.frameTime = static_cast<double>(CCDirector::get()->getTotalFrames());
        }
    }

    // ── Fallback visual (Fase 1 fix) ──
    // Si el blur falla (shader no linkeo, FBO intermedio incompleto, etc),
    // en lugar de no mostrar nada (dejando la escena sin oscurecer), creamos
    // un sprite negro semi-transparente como fallback. Asi NUNCA se ve blanco.
    if (!blurred) {
        geode::log::warn("[PopupBlur] Blur failed, using dark fallback");
        if (auto* fallbackTex = getDarkFallbackTexture()) {
            blurred = CCSprite::createWithTexture(fallbackTex);
        }
        if (!blurred) return false;
    }

    // winSize real de la pantalla (para posicionar el blur node)
    auto winSize = CCDirector::get()->getWinSize();

    // ── FIX (popup blur thumbnail en esquina + todo en negro) ──
    // Normalizar el sprite blureado a un FBO de winSize antes de pasarlo a
    // buildBlurNode. Esto garantiza que el sprite tenga:
    //   - contentSize == winSize (en puntos logicos, no pixeles fisicos)
    //   - flipY=true (orientacion correcta respecto al backbuffer)
    //   - rendering visual cubriendo exactamente toda la pantalla
    //
    // Sin esto, el sprite del cache o del paimonblur llegaba con dimensiones
    // inconsistentes que causaban scaling raro en buildBlurNode (sprite
    // pequeño en esquina inferior izquierda mientras el resto de la pantalla
    // se veia negra debido al darkness aplicado a un sprite pequeño).
    //
    // El costo extra: un FBO mas + un visit pass en winSize (~1ms tipico).
    // Es minor comparado con el dual-Kawase de paimonblur (4-6 passes).
    if (auto* normalized = normalizeBlurSpriteToWinSize(blurred, winSize)) {
        blurred = normalized;
    }

    auto* root = buildBlurNode(blurred, winSize, cfg);
    if (!root) return false;

    // Insertar justo debajo del popup en z-order
    parent->addChild(root, popup->getZOrder() - 1);

    // ── PERF: Ocultar blur nodes anteriores (eliminacion de overdraw) ──
    // Cuando se apilan popups (popup A → popup B), el blur de A queda
    // completamente tapado por el blur de B (ambos son fullscreen opacos).
    // Dibujarlo es puro overdraw — ~1-2ms por blur node a 1080p.
    // Ocultamos todos los blur nodes previos. Se restauran en cleanup()
    // cuando el popup superior se cierra.
    {
        auto& reg = blurRegistry();
        for (auto& [key, entry] : reg) {
            if (key == popup) continue;  // no ocultar el que acabamos de crear
            auto* prevBlur = entry.blurRef.data();
            if (prevBlur && prevBlur->isVisible() && !entry.fadingOut) {
                prevBlur->setVisible(false);
            }
        }
    }

    RegistryEntry entry;
    entry.popupPtr = popup;
    entry.blurRef = Ref<CCNode>(root);
    entry.parentPtr = parent;
    entry.ageSeconds = 0.f;
    entry.fadingOut = false;
    blurRegistry()[popup] = std::move(entry);
    scheduleWatchdogIfNeeded();

    // Fade-in: el blur aparece en sync con el popup en lugar de hacer "pop"
    // instantaneo. La duracion matchea (aproximadamente) las animaciones de
    // entrada de popups del mod.
    float fadeDuration = std::clamp(
        static_cast<float>(paimon::settings::popupblur::fadeDuration()),
        0.0f, 1.0f);
    if (fadeDuration > 0.01f) {
        root->setOpacity(0);
        root->runAction(CCFadeTo::create(fadeDuration, 255));
    }
    return true;
}

// Implementacion compartida: remueve el blur del registry + detach del parent.
// Si fadeDuration > 0, hace fade-out antes de remover. El registry se limpia
// inmediatamente (antes del fade) para que hideAllActiveBlurs() no intente
// ocultarlo durante la animacion de salida.
static void cleanupImpl(CCNode* popup, float fadeDuration) {
    if (!popup) return;
    auto& reg = blurRegistry();
    auto it = reg.find(popup);
    if (it == reg.end()) return;

    auto* blurNode = it->second.blurRef.data();
    reg.erase(it);  // saca del registry YA — no participa en futuras capturas
    unscheduleWatchdogIfIdle();

    if (!blurNode || !blurNode->getParent()) {
        // ── PERF: Restaurar visibilidad del blur anterior ──
        // Si habiamos ocultado blur nodes previos, restaurar el mas reciente.
        for (auto& [key, entry] : reg) {
            auto* prevBlur = entry.blurRef.data();
            if (prevBlur && !prevBlur->isVisible() && !entry.fadingOut) {
                prevBlur->setVisible(true);
                break;  // solo restaurar el top — los demas siguen ocultos
            }
        }
        return;
    }

    // ── PERF: Restaurar visibilidad del blur anterior ──
    // Al cerrar este popup, el blur que estaba debajo vuelve a ser visible.
    // Solo restauramos uno (el mas reciente que no este en fade-out).
    for (auto& [key, entry] : reg) {
        auto* prevBlur = entry.blurRef.data();
        if (prevBlur && !prevBlur->isVisible() && !entry.fadingOut) {
            prevBlur->setVisible(true);
            break;
        }
    }

    if (fadeDuration <= 0.01f) {
        // ── FIX (crash con globed RoomPopup) ──
        // Diferimos al siguiente tick. Cuando esta llamada viene desde
        // onExit() (invocado por detachChild del parent durante removeFromParent
        // del popup), el blur es sibling del popup. Removerlo sincronicamente
        // muta el children[] del parent que cocos esta a punto de tocar al
        // volver de onExit, produciendo un access violation en memmove dentro
        // de CCArray::removeObjectAtIndex.
        //
        // Repro: dankmeme.globed2 RoomPopup cerrandose desde un event handler.
        // Stack: memmove → CCArray → Popup::onClose → onExit hook → cleanup.
        //
        // Capturamos un Ref<CCNode> para mantenerlo vivo aunque el parent lo
        // suelte mientras tanto. En el tick siguiente el children[] del parent
        // ya esta estable y removeFromParent es seguro.
        Ref<CCNode> keepAlive = Ref<CCNode>(blurNode);
        Loader::get()->queueInMainThread([keepAlive]() {
            if (auto* node = keepAlive.data(); node && node->getParent()) {
                node->removeFromParent();
            }
        });
        return;
    }

    // Fade-out + auto-remove. Ref<CCNode> del registry ya retenia el blur;
    // al sacarlo del map la Ref se destruye, pero el parent de cocos aun lo
    // retiene (addChild incrementa refCount). El runAction + CCCallFunc al
    // final garantizan el remove incluso si el scene graph no lo hace.
    blurNode->stopAllActions();
    blurNode->runAction(CCSequence::create(
        CCFadeTo::create(fadeDuration, 0),
        CCCallFunc::create(blurNode, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    ));
}

void cleanup(CCNode* popup) {
    cleanupImpl(popup, 0.f);
}

void cleanupWithFade(CCNode* popup, float duration) {
    cleanupImpl(popup, duration);
}

void cleanupAllActive(float fadeDuration) {
    // Invalidar snapshot y blur result cache — la escena va a cambiar (transition)
    {
        auto& cache = snapshotCache();
        cache.tex = nullptr;
        cache.size = CCSizeZero;
        cache.frameTime = -1.0;
    }
    {
        auto& bcache = blurResultCache();
        bcache.blurredTex = nullptr;
        bcache.snapshotToken = nullptr;
        bcache.frameTime = -1.0;
    }

    auto& reg = blurRegistry();
    if (reg.empty()) return;

    // Copia strong refs antes de mutar el map — mantiene blurs vivos durante
    // el fade aunque el parent los libere.
    std::vector<Ref<CCNode>> blurs;
    blurs.reserve(reg.size());
    for (auto& [_, entry] : reg) {
        blurs.push_back(entry.blurRef);
    }
    reg.clear();
    unscheduleWatchdogIfIdle();

    for (auto& ref : blurs) {
        auto* node = ref.data();
        if (!node || !node->getParent()) continue;
        fadeAndRemoveBlurNode(node, fadeDuration);
    }
}

} // namespace paimon::popupblur
