#include "LeaderboardLayoutPopup.hpp"
#include "ScoreCellSettingsPopup.hpp"
#include "../LeaderboardLayoutSettings.hpp"
#include "../ScoreCellRefresh.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>

using namespace geode::prelude;

namespace paimon::scorecell {

LeaderboardLayoutPopup* LeaderboardLayoutPopup::create() {
    if (!paimon::modules::isEnabled("paimbnails.leaderboardcells.browser")) return nullptr;

    auto ret = new LeaderboardLayoutPopup();
    if (ret && ret->initContents()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LeaderboardLayoutPopup::initContents() {
    if (!Popup::init(460.f, 390.f)) return false;
    this->setTitle("Leaderboard Layout");

    auto size = m_mainLayer->getContentSize();
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    auto hint = CCLabelBMFont::create("Native alignment is preserved. The active stat always stays visible.", "bigFont.fnt");
    hint->setScale(0.34f);
    hint->setColor({190, 205, 225});
    hint->setPosition({size.width / 2.f, size.height - 52.f});
    m_mainLayer->addChild(hint);

    m_presetLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_presetLabel->setScale(0.42f);
    m_presetLabel->setPosition({size.width / 2.f, size.height - 76.f});
    m_mainLayer->addChild(m_presetLabel);

    float presetY = size.height - 105.f;
    float presetGap = 92.f;
    float presetStart = size.width / 2.f - presetGap * 1.5f;
    for (size_t i = 0; i < kLeaderboardPresets.size(); ++i) {
        auto const& preset = kLeaderboardPresets[i];
        auto sprite = ButtonSprite::create(std::string(preset.name).c_str(), "bigFont.fnt", "GJ_button_05.png", 0.6f);
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(LeaderboardLayoutPopup::onPreset));
        button->setTag(static_cast<int>(i));
        button->setScale(0.68f);
        button->setPosition({presetStart + presetGap * static_cast<float>(i), presetY});
        menu->addChild(button);
    }

    auto modulesTitle = CCLabelBMFont::create("Modules", "goldFont.fnt");
    modulesTitle->setScale(0.48f);
    modulesTitle->setAnchorPoint({0.f, 0.5f});
    modulesTitle->setPosition({28.f, size.height - 138.f});
    m_mainLayer->addChild(modulesTitle);

    m_moduleToggles.resize(kLeaderboardModules.size());
    constexpr size_t kRows = 5;
    float columnWidth = 205.f;
    float startX = 28.f;
    float startY = size.height - 169.f;

    for (size_t i = 0; i < kLeaderboardModules.size(); ++i) {
        auto const& info = kLeaderboardModules[i];
        size_t column = i / kRows;
        size_t row = i % kRows;
        float x = startX + columnWidth * static_cast<float>(column);
        float y = startY - 29.f * static_cast<float>(row);

        auto label = CCLabelBMFont::create(std::string(info.name).c_str(), "bigFont.fnt");
        label->setScale(0.37f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({x, y});
        m_mainLayer->addChild(label);

        auto toggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(LeaderboardLayoutPopup::onModule), 0.55f
        );
        toggle->setTag(static_cast<int>(i));
        toggle->setPosition({x + 167.f, y});
        menu->addChild(toggle);
        m_moduleToggles[i] = toggle;
    }

    auto effectsSprite = ButtonSprite::create("Effects", "bigFont.fnt", "GJ_button_04.png", 0.7f);
    auto effectsButton = CCMenuItemSpriteExtra::create(
        effectsSprite, this, menu_selector(LeaderboardLayoutPopup::onEffects)
    );
    effectsButton->setPosition({size.width / 2.f - 70.f, 28.f});
    menu->addChild(effectsButton);

    auto doneSprite = ButtonSprite::create("Done", "bigFont.fnt", "GJ_button_01.png", 0.7f);
    auto doneButton = CCMenuItemSpriteExtra::create(
        doneSprite, this, menu_selector(LeaderboardLayoutPopup::onClose)
    );
    doneButton->setPosition({size.width / 2.f + 70.f, 28.f});
    menu->addChild(doneButton);

    refreshControls();
    this->setID("leaderboard-layout-popup"_spr);
    paimon::markDynamicPopup(this);
    return true;
}

void LeaderboardLayoutPopup::refreshControls() {
    auto preset = leaderboardPreset();
    auto info = presetInfo(preset);
    auto name = info ? std::string(info->name) : std::string("Custom");
    if (m_presetLabel) m_presetLabel->setString(fmt::format("Preset: {}", name).c_str());

    for (size_t i = 0; i < m_moduleToggles.size(); ++i) {
        if (auto toggle = m_moduleToggles[i]) {
            toggle->toggle(moduleEnabled(kLeaderboardModules[i].module));
        }
    }
}

void LeaderboardLayoutPopup::onPreset(CCObject* sender) {
    auto index = static_cast<size_t>(sender->getTag());
    if (index >= kLeaderboardPresets.size()) return;
    applyLeaderboardPreset(kLeaderboardPresets[index].key);
    refreshControls();
    refreshAllCells();
}

void LeaderboardLayoutPopup::onModule(CCObject* sender) {
    auto index = static_cast<size_t>(sender->getTag());
    if (index >= kLeaderboardModules.size()) return;
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    setModuleEnabled(kLeaderboardModules[index].module, !toggle->isToggled());
    if (m_presetLabel) m_presetLabel->setString("Preset: Custom");
    refreshAllCells();
}

void LeaderboardLayoutPopup::onEffects(CCObject*) {
    auto popup = ScoreCellSettingsPopup::create();
    if (!popup) return;
    popup->setOnClose([] {
        refreshAllCells();
    });
    popup->show();
}

void LeaderboardLayoutPopup::onClose(CCObject* sender) {
    auto callback = std::move(m_onCloseCallback);
    Popup::onClose(sender);
    if (callback) callback();
}

} // namespace paimon::scorecell
