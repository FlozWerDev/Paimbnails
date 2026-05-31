// ─────────────────────────────────────────────────────────────
// CCSceneSafety.cpp
//
// Protege contra crashes en cocos2d::CCScene::getHighestChildZ
// cuando la escena tiene 0 hijos (count()==0 → count()-1 = UINT_MAX
// → CCArray::objectAtIndex lee memoria invalida).
//
// Este escenario puede darse durante transiciones de escena cuando
// otros mods (betterinfo, automaticquests, globed, etc.) llaman a
// funciones del juego que a su vez invocan getHighestChildZ sobre
// una escena que todavia no tiene hijos o que acaba de ser vaciada.
//
// En vanilla el juego asume que la escena siempre tiene al menos
// un hijo, pero con 50+ mods hookeando levelComplete, el timing
// se rompe. Hookear aqui con Priority::First garantiza que el
// chequeo corra antes de que el codigo vanilla toque children[count()-1].
//
// Fix simbolo por simbolo — menor invasividad posible, no cambia
// el comportamiento si la escena tiene hijos.
// ─────────────────────────────────────────────────────────────

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScene.hpp>
#include "../blur/PopupBlurService.hpp"

using namespace geode::prelude;

class $modify(PaimonSafeCCScene, CCScene) {
    static void onModify(auto& self) {
        // Correr ANTES que cualquier otro mod, para interceptar el caso
        // degenerado antes de que llegue al codigo vanilla de cocos.
        (void)self.setHookPriorityPre("cocos2d::CCScene::getHighestChildZ", geode::Priority::First);
    }

    float getHighestChildZ() {
        // Defensivo: si no hay array de hijos o esta vacio, devolver 0.
        // Sin este guard, CCScene::getHighestChildZ hace
        //   m_pChildren->objectAtIndex(m_pChildren->count() - 1)
        // con count()==0 → index = UINT_MAX → access violation.
        auto* children = this->getChildren();
        if (!children || children->count() == 0) {
            return 0.0f;
        }
        return CCScene::getHighestChildZ();
    }

    // Barre blurs huerfanos cuando una escena se destruye. Scene::cleanup()
    // se llama cuando la escena sale del director (replace/push/pop scene).
    // Sin esto, un blur cuyo popup murio con la escena anterior podia
    // "viajar" a la escena nueva si algo lo retenia, quedando visible por
    // minutos hasta que otra operacion de blur lo pisara.
    //
    // El cleanupAllActive() con fade corto desvanece todos los blurs
    // registrados de golpe. En condiciones normales el registry estara
    // vacio (los hooks de los popups ya limpiaron su blur) y esta llamada
    // es un no-op barato. En condiciones degeneradas (popup cerrado sin
    // disparar keyBackClicked/onExit) es la red de seguridad final.
    void cleanup() {
        paimon::popupblur::cleanupAllActive(0.15f);
        CCScene::cleanup();
    }
};
