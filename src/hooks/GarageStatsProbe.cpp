// TEMPORAL - sonda de diagnostico.
//
// Vigila el menu de estadisticas del icon kit (capeling.garage-stats-menu) y
// registra en el log el momento exacto en que alguno de sus nodos cambia de
// visible a invisible, junto con quien esta por encima de el. Sirve para
// localizar que parte de Paimbnails los apaga.
//
// Borrar este archivo cuando el bug este resuelto.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJGarageLayer.hpp>

#include "../framework/HookConventions.hpp"

#include <string>
#include <vector>

using namespace geode::prelude;

namespace {

constexpr char const* kStatsMenuID = "capeling.garage-stats-menu/stats-menu";
constexpr int kWatchFrames = 240;

std::string describe(CCNode* node) {
    if (!node) return "<null>";
    auto id = std::string(node->getID());
    if (id.empty()) id = "<sin id>";
    auto pos = node->getPosition();
    return fmt::format("{} visible={} z={} pos=({:.1f},{:.1f}) scale={:.2f} kids={}",
        id, node->isVisible(), node->getZOrder(), pos.x, pos.y,
        node->getScale(), node->getChildrenCount());
}

class StatsProbe : public CCNode {
public:
    static StatsProbe* create(CCNode* statsMenu) {
        auto* n = new StatsProbe();
        n->init();
        n->m_menu = statsMenu;
        n->autorelease();
        n->scheduleUpdate();
        return n;
    }

    void update(float) override {
        auto menuRef = m_menu.lock();
        auto* menu = static_cast<CCNode*>(menuRef);
        if (!menu) return;
        ++m_frame;

        bool const menuVisible = menu->isVisible();
        if (menuVisible != m_lastMenuVisible) {
            m_lastMenuVisible = menuVisible;
            log::info("[stats-probe] frame {}: el MENU paso a visible={} -> {}",
                m_frame, menuVisible, describe(menu));
        }

        auto* children = menu->getChildren();
        if (!children) return;

        int const count = children->count();
        if (count != m_lastCount) {
            log::info("[stats-probe] frame {}: children {} -> {}", m_frame, m_lastCount, count);
            m_lastCount = count;
            m_lastVisible.assign(count, true);
            for (int i = 0; i < count; ++i) {
                auto* c = static_cast<CCNode*>(children->objectAtIndex(i));
                m_lastVisible[i] = c && c->isVisible();
                log::info("[stats-probe]   [{}] {}", i, describe(c));
            }
            return;
        }

        for (int i = 0; i < count; ++i) {
            auto* c = static_cast<CCNode*>(children->objectAtIndex(i));
            if (!c) continue;
            bool const vis = c->isVisible();
            if (vis == m_lastVisible[i]) continue;
            m_lastVisible[i] = vis;
            log::info("[stats-probe] frame {}: hijo [{}] paso a visible={} -> {}",
                m_frame, i, vis, describe(c));
        }

        if (m_frame >= kWatchFrames) {
            log::info("[stats-probe] fin de la vigilancia ({} frames)", kWatchFrames);
            unscheduleUpdate();
        }
    }

private:
    WeakRef<CCNode> m_menu = nullptr;
    std::vector<bool> m_lastVisible;
    int m_lastCount = -1;
    int m_frame = 0;
    bool m_lastMenuVisible = true;
};

}  // anonymous namespace

class $modify(PaimonGarageStatsProbe, GJGarageLayer) {
    static void onModify(auto& self) {
        // Lo mas tarde posible: queremos ver el estado despues de que todos los
        // mods (incluido el resto de Paimbnails) hayan tocado la capa.
        paimon::hooks::afterAllPaimonUiOrVeryLate(self, "GJGarageLayer::init");
    }

    $override
    bool init() {
        if (!GJGarageLayer::init()) return false;

        auto* menu = this->getChildByIDRecursive(kStatsMenuID);
        if (!menu) {
            log::info("[stats-probe] no encontre '{}' en el garage", kStatsMenuID);
            // Volcar los hijos directos para ver con que id quedo.
            if (auto* kids = this->getChildren()) {
                for (int i = 0; i < kids->count(); ++i) {
                    log::info("[stats-probe]   hijo directo [{}] {}",
                        i, describe(static_cast<CCNode*>(kids->objectAtIndex(i))));
                }
            }
            return true;
        }

        log::info("[stats-probe] menu encontrado -> {}", describe(menu));
        log::info("[stats-probe] padre -> {}", describe(menu->getParent()));

        auto* probe = StatsProbe::create(menu);
        probe->setID("paimbnails/stats-probe"_spr);
        probe->setVisible(false);
        this->addChild(probe, -1000);
        return true;
    }
};
