// Scrollable font picker overlay (BetterEdit-inspired).
// Hooks SelectFontLayer to add a scroll grid of fonts instead of only prev/next.

#include "../EditorModule.hpp"

#include <Geode/binding/SelectFontLayer.hpp>
#include <Geode/modify/SelectFontLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include "../../../framework/HookConventions.hpp"

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {
bool on() { return moduleEnabled("editor-mod-better-font-select"); }
// GD has a fixed set of fonts (bigFont, chatFont, ... + custom font files).
// We expose indices 0..59 for the vanilla font cycle.
constexpr int kFontCount = 60;
}

class $modify(PaimonBetterFont, SelectFontLayer) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "SelectFontLayer::init");
    }

    void onPickFont(CCObject* sender) {
        auto* item = typeinfo_cast<CCMenuItem*>(sender);
        if (!item) return;
        m_font = item->getTag();
        this->updateFontLabel();
        // Apply change through the same path as arrows when possible
        this->onChangeFont(sender);
    }

    $override
    bool init(LevelEditorLayer* layer) {
        if (!SelectFontLayer::init(layer)) return false;
        if (!on() || !m_mainLayer) return true;

        auto size = m_mainLayer->getContentSize();
        auto* scroll = ScrollLayer::create({size.width - 40.f, 110.f});
        scroll->setID("paimbnails/font-scroll");
        scroll->setPosition({20.f, 36.f});
        scroll->m_contentLayer->setLayout(
            RowLayout::create()
                ->setGrowCrossAxis(true)
                ->setCrossAxisOverflow(true)
                ->setGap(4.f)
                ->setAutoScale(false)
        );

        auto* menu = CCMenu::create();
        menu->setContentSize({size.width - 50.f, 0.f});
        menu->setLayout(
            RowLayout::create()
                ->setGrowCrossAxis(true)
                ->setCrossAxisOverflow(true)
                ->setGap(4.f)
                ->setAutoScale(false)
        );

        for (int i = 0; i < kFontCount; ++i) {
            auto label = fmt::format("F{}", i);
            auto* spr = ButtonSprite::create(label.c_str(), "chatFont.fnt", "GJ_button_04.png", 0.4f);
            auto* btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(PaimonBetterFont::onPickFont)
            );
            btn->setTag(i);
            if (i == m_font) spr->setColor({100, 255, 100});
            menu->addChild(btn);
        }
        menu->updateLayout();
        // Host menu inside scroll content
        menu->setPosition(menu->getContentSize() / 2.f);
        auto* host = CCNode::create();
        host->setContentSize(menu->getContentSize());
        host->addChild(menu);
        scroll->m_contentLayer->setContentSize(host->getContentSize());
        scroll->m_contentLayer->addChild(host);
        scroll->scrollToTop();
        m_mainLayer->addChild(scroll, 20);
        return true;
    }
};
