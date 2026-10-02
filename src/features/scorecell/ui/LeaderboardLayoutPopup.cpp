#include "LeaderboardLayoutPopup.hpp"
#include "ScoreCellSettingsPopup.hpp"
#include "../LeaderboardLayoutSettings.hpp"
#include "../ScoreCellRefresh.hpp"
#include "../../../ui/PaimonUI.hpp"
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
    if (!PaimonPopup::init(460.f, 390.f)) return false;
    this->setTitle("Leaderboard Layout");
    this->addCorners();
    this->addInfoButton("Leaderboard Layout",
        "Choose which stats a leaderboard cell shows.\n\n"
        "<cy>Presets</c> set a whole combination at once; toggling any <cg>module</c> "
        "switches you to <cj>Custom</c>.\n\n"
        "The game's native alignment is kept and the active stat always stays visible. "
        "Open <co>Effects</c> for gradient and hover animations.");

    namespace ui = paimon::ui;
    auto size = m_mainLayer->getContentSize();
    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 5);

    float presetPanelH = 70.f;
    float presetTop = size.height - 44.f;
    if (auto* panel = ui::makePanel({size.width - 32.f, presetPanelH}, "Presets")) {
        panel->setPosition({16.f, presetTop - presetPanelH});
        m_mainLayer->addChild(panel);
    }

    m_presetLabel = ui::makeLabel("", size.width - 60.f, 0.4f, ui::palette::gold);
    m_presetLabel->setPosition({size.width / 2.f, presetTop - ui::kPanelHeader - 6.f});
    m_mainLayer->addChild(m_presetLabel);

    float presetY = presetTop - presetPanelH + 20.f;
    float presetGap = 92.f;
    float presetStart = size.width / 2.f - presetGap * 1.5f;
    for (size_t i = 0; i < kLeaderboardPresets.size(); ++i) {
        auto const& preset = kLeaderboardPresets[i];
        auto sprite = ui::makeButtonSprite(std::string(preset.name).c_str(),
            ui::Btn::Blue, 0.f, 0.6f, "bigFont.fnt");
        auto button = CCMenuItemSpriteExtra::create(sprite, this, menu_selector(LeaderboardLayoutPopup::onPreset));
        button->setTag(static_cast<int>(i));
        button->setScale(0.68f);
        button->setPosition({presetStart + presetGap * static_cast<float>(i), presetY});
        menu->addChild(button);
    }

    float modTop = presetTop - presetPanelH - 10.f;
    float modH = 185.f;
    if (auto* panel = ui::makePanel({size.width - 32.f, modH}, "Modules")) {
        panel->setPosition({16.f, modTop - modH});
        m_mainLayer->addChild(panel);
    }

    m_moduleToggles.resize(kLeaderboardModules.size());
    constexpr size_t kRows = 5;
    float columnWidth = 205.f;
    float startX = 28.f;
    float startY = modTop - ui::kPanelHeader - 12.f;

    for (size_t i = 0; i < kLeaderboardModules.size(); ++i) {
        auto const& info = kLeaderboardModules[i];
        size_t column = i / kRows;
        size_t row = i % kRows;
        float x = startX + columnWidth * static_cast<float>(column);
        float y = startY - 29.f * static_cast<float>(row);

        auto label = ui::makeLabel(std::string(info.name).c_str(), 150.f, 0.37f);
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

    auto effectsButton = ui::makeButton("Effects", [this] { this->onEffects(nullptr); },
        ui::Btn::Cyan, 0.f, 0.7f);
    effectsButton->setPosition({size.width / 2.f - 70.f, 28.f});
    menu->addChild(effectsButton);

    auto doneButton = ui::makeButton("Done", [this] { this->onClose(nullptr); },
        ui::Btn::Green, 0.f, 0.7f);
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
