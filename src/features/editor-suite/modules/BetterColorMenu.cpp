// Larger / multi-page friendly color channel strip on ColorSelectPopup.
// BetterEdit-inspired: show more channel previews at once.

#include "../EditorModule.hpp"

#include <Geode/binding/ColorSelectPopup.hpp>
#include <Geode/binding/GJEffectManager.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/ColorSelectPopup.hpp>
#include <array>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-better-color-menu"); }
// Common + first user channels
constexpr std::array kCh = {
    1000, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009,
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
};
}

class $modify(PaimonBetterColor, ColorSelectPopup) {
    struct Fields {
        Ref<CCNode> strip;
    };

    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "ColorSelectPopup::init");
    }

    void onPickChannel(CCObject* sender) {
        auto* item = typeinfo_cast<CCMenuItem*>(sender);
        if (!item) return;
        int id = item->getTag();
        // Prefer public path if available
        m_colorID = id;
        if (auto* lel = LevelEditorLayer::get()) {
            if (lel->m_effectManager) {
                if (auto* ca = lel->m_effectManager->getColorAction(id)) {
                    m_colorAction = ca;
                }
            }
        }
        this->updateColorLabels();
        this->updateOpacity();
        this->updateCopyColor();
    }

    $override
    bool init(EffectGameObject* effect, CCArray* objects, ColorAction* color) {
        if (!ColorSelectPopup::init(effect, objects, color)) return false;
        if (!on() || !m_mainLayer) return true;

        auto win = m_mainLayer->getContentSize();
        auto* strip = CCNode::create();
        strip->setID("paimbnails/color-channel-strip");
        strip->setContentSize({std::min(win.width - 40.f, 360.f), 22.f});
        strip->setPosition({win.width / 2.f, 28.f});
        strip->setAnchorPoint({0.5f, 0.f});

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(strip->getContentSize());
        menu->setLayout(
            RowLayout::create()->setGap(3.f)->setAutoScale(false)
                ->setAxisAlignment(AxisAlignment::Center)
        );

        auto* mgr = LevelEditorLayer::get() ? LevelEditorLayer::get()->m_effectManager : nullptr;
        for (int id : kCh) {
            ccColor3B col{180, 180, 180};
            if (mgr) {
                if (auto* ca = mgr->getColorAction(id)) {
                    col = ca->m_color;
                    if (col.r == 0 && col.g == 0 && col.b == 0) col = ca->m_toColor;
                }
            }
            auto* sw = CCLayerColor::create({col.r, col.g, col.b, 255}, 14.f, 14.f);
            auto* btn = CCMenuItemSpriteExtra::create(
                sw, this, menu_selector(PaimonBetterColor::onPickChannel)
            );
            btn->setTag(id);
            btn->setID(fmt::format("paimbnails/color-ch-{}", id));
            menu->addChild(btn);
        }
        menu->updateLayout();
        strip->addChild(menu);
        m_mainLayer->addChild(strip, 40);
        m_fields->strip = strip;
        return true;
    }
};
