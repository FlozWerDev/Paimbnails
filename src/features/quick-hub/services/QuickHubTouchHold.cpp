#include "QuickHubManager.hpp"
#include "../ui/QuickHubRadial.hpp"
#include "../../main-menu-layout/ui/MainMenuLayoutEditor.hpp"
#include "../../main-menu-layout/services/MainMenuLayoutManager.hpp"
#include "../../main-menu-layout/hooks/LayoutEditorKeybind.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using namespace cocos2d;

// ─────────────────────────────────────────────────────────────────────────────
// QuickHubTouchHold — Gesto de hold-tap para Android/iOS (touch puro).
//
// En plataformas sin teclado fisico, el hold de Ctrl no funciona. En su lugar:
//   - 1 dedo mantenido 1.5s → abre el Quick Hub Radial
//   - 2 dedos mantenidos 1.5s → abre el Button Layout Editor
//
// Cancelacion:
//   - Si el dedo se mueve mas de 15px → cancelar (es un drag/scroll)
//   - Si se suelta antes de completar → cancelar
//   - Si ya hay un popup/radial abierto → no iniciar
//
// Solo se compila en Android/iOS. En Windows/Mac el Ctrl hold se encarga.
// ─────────────────────────────────────────────────────────────────────────────

#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)

namespace {

constexpr float kDeadZone = 0.5f;
constexpr float kFillDuration = 1.0f;
constexpr float kTotalHold = kDeadZone + kFillDuration;
constexpr float kMoveThreshold = 15.f; // px de movimiento antes de cancelar

struct TouchHoldState {
    bool active = false;
    int fingerCount = 0;        // 1 o 2 dedos
    float elapsed = 0.f;
    bool barVisible = false;
    bool completed = false;
    CCPoint startPos = CCPointZero; // posicion inicial del primer dedo
    CCNode* progressBar = nullptr;
    CCNode* progressFill = nullptr;
};

static TouchHoldState s_touch;

void cleanupBar() {
    if (s_touch.progressBar) {
        s_touch.progressBar->removeFromParent();
        s_touch.progressBar = nullptr;
        s_touch.progressFill = nullptr;
    }
    s_touch.barVisible = false;
}

void resetTouch() {
    s_touch.active = false;
    s_touch.fingerCount = 0;
    s_touch.elapsed = 0.f;
    s_touch.completed = false;
    cleanupBar();
}

void createBar() {
    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (!scene) return;

    auto winSize = CCDirector::sharedDirector()->getWinSize();

    auto container = CCNode::create();
    container->setPosition({winSize.width / 2.f, winSize.height - 8.f});
    container->setContentSize({180.f, 4.f});
    container->setAnchorPoint({0.5f, 0.5f});
    container->setZOrder(99999);
    scene->addChild(container);

    auto bg = CCLayerColor::create({40, 40, 50, 180});
    bg->setContentSize({180.f, 4.f});
    bg->setPosition({-90.f, -2.f});
    container->addChild(bg, 0);

    auto fill = CCLayerColor::create({255, 255, 255, 220});
    fill->setContentSize({0.f, 4.f});
    fill->setPosition({-90.f, -2.f});
    container->addChild(fill, 1);

    s_touch.progressBar = container;
    s_touch.progressFill = fill;
    s_touch.barVisible = true;
}

void updateBar(float progress) {
    if (!s_touch.progressFill) return;
    float w = 180.f * std::clamp(progress, 0.f, 1.f);
    s_touch.progressFill->setContentSize({w, 4.f});
}

// Scheduler node para el update loop del hold
class TouchHoldScheduler : public CCNode {
public:
    static TouchHoldScheduler* get() {
        static TouchHoldScheduler* s_instance = nullptr;
        if (!s_instance) {
            s_instance = new TouchHoldScheduler();
            s_instance->init();
            s_instance->retain();
            CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
                schedule_selector(TouchHoldScheduler::onUpdate),
                s_instance, 0.f, false
            );
        }
        return s_instance;
    }

    void onUpdate(float dt) {
        if (!s_touch.active) return;
        if (s_touch.completed) return;

        s_touch.elapsed += dt;

        if (s_touch.elapsed < kDeadZone) return;

        if (!s_touch.barVisible) {
            createBar();
        }

        float fillProgress = (s_touch.elapsed - kDeadZone) / kFillDuration;
        updateBar(fillProgress);

        if (s_touch.elapsed >= kTotalHold) {
            cleanupBar();
            s_touch.completed = true;

            if (s_touch.fingerCount >= 2) {
                // 2 dedos → abrir editor de layout
                openLayoutEditor();
            } else {
                // 1 dedo → abrir Quick Hub Radial
                paimon::quickhub::QuickHubRadial::openRadial();
            }
        }
    }

private:
    void openLayoutEditor() {
        using namespace paimon::menu_layout;

        // Si ya esta activo, no abrir otro
        if (MainMenuLayoutEditor::isActive()) return;

        // Buscar el layer interactivo mas arriba en la escena
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;

        CCLayer* topLayer = nullptr;
        auto* children = scene->getChildren();
        if (children) {
            for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
                auto* node = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
                if (!node || !node->isVisible()) continue;
                auto* layer = typeinfo_cast<CCLayer*>(node);
                if (layer) { topLayer = layer; break; }
            }
        }

        if (!topLayer) return;

        MainMenuLayoutManager::get().captureDefaultsAndApply(topLayer);
        MainMenuLayoutEditor::open(topLayer);
    }
};

} // anonymous namespace

// ─── Hook de CCTouchDispatcher para detectar gestos de hold ─────────────────
//
// Usamos un hook en CCLayer::ccTouchBegan/Moved/Ended a nivel global no es
// viable. En su lugar, hookeamos handleTouchesBegin/Move/End en CCEGLView
// que es el punto de entrada de TODOS los touches antes de que se dispatchen
// a los delegates. Esto nos da acceso a los touches raw.
//
// NOTA: Ya existe un hook en CCEGLView.cpp para captura y pet. Creamos un
// segundo $modify con nombre distinto — Geode los combina automaticamente.

#include <Geode/modify/CCEGLView.hpp>

class $modify(TouchHoldView, CCEGLView) {
    void handleTouchesBegin(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLView::handleTouchesBegin(num, ids, xs, ys, timestamp);

        // No iniciar si el radial ya esta abierto o el editor activo
        if (paimon::quickhub::QuickHubRadial::isOpen()) return;
        if (paimon::menu_layout::MainMenuLayoutEditor::isActive()) return;

        // Asegurar scheduler
        TouchHoldScheduler::get();

        if (!s_touch.active) {
            // Primer touch — iniciar hold de 1 dedo
            s_touch.active = true;
            s_touch.fingerCount = num;
            s_touch.elapsed = 0.f;
            s_touch.completed = false;
            s_touch.barVisible = false;
            // Guardar posicion inicial para deteccion de movimiento
            if (num > 0) {
                s_touch.startPos = ccp(xs[0], ys[0]);
            }
        } else if (!s_touch.completed) {
            // Mas dedos se unieron — upgrade a 2 dedos
            s_touch.fingerCount += num;
            if (s_touch.fingerCount > 2) s_touch.fingerCount = 2;
        }
    }

    void handleTouchesMove(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLView::handleTouchesMove(num, ids, xs, ys, timestamp);

        if (!s_touch.active || s_touch.completed) return;

        // Si el dedo se movio mucho, cancelar (es un drag/scroll)
        if (num > 0) {
            CCPoint current = ccp(xs[0], ys[0]);
            float dist = ccpDistance(current, s_touch.startPos);
            if (dist > kMoveThreshold) {
                resetTouch();
            }
        }
    }

    void handleTouchesEnd(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLView::handleTouchesEnd(num, ids, xs, ys, timestamp);

        if (!s_touch.active) return;

        // Si se solto antes de completar, cancelar
        if (!s_touch.completed) {
            s_touch.fingerCount -= num;
            if (s_touch.fingerCount <= 0) {
                resetTouch();
            }
        } else {
            // Ya se completo — reset para el proximo gesto
            s_touch.fingerCount -= num;
            if (s_touch.fingerCount <= 0) {
                resetTouch();
            }
        }
    }

    void handleTouchesCancel(int num, int ids[], float xs[], float ys[], double timestamp) {
        CCEGLView::handleTouchesCancel(num, ids, xs, ys, timestamp);
        resetTouch();
    }
};

#endif // GEODE_IS_ANDROID || GEODE_IS_IOS
