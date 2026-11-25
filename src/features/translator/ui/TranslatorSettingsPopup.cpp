#include "TranslatorSettingsPopup.hpp"

#include "TranslatorLanguageGrid.hpp"
#include "../TranslatorConfig.hpp"
#include "../TranslatorLanguages.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <algorithm>
#include <functional>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::translate {
namespace {

// fits a 320 point tall screen, so PaimonPopup::show does not shrink it.
constexpr float kWidth = 440.f;
constexpr float kHeight = 290.f;
constexpr float kMargin = 20.f;
constexpr float kPanelWidth = kWidth - kMargin * 2.f;
constexpr float kTextLeft = 36.f;

// three stacked sections, bottom-left origins in popup space
constexpr float kSwitchesBottom = 198.f;
constexpr float kSwitchRowHeight = 27.f;
constexpr float kSwitchesHeight = kSwitchRowHeight * 2.f;
constexpr float kLanguagesBottom = 86.f;
constexpr float kLanguagesHeight = 106.f;
constexpr float kEndpointBottom = 30.f;
constexpr float kEndpointHeight = 50.f;

constexpr float kTileWidth = 120.f;
constexpr float kTileScale = .78f;
constexpr float kColumnStep = 128.f;
constexpr float kRowStep = 26.f;
constexpr float kFirstRowY = 70.f;

constexpr float kInputWidth = 270.f;
constexpr float kInputScale = .55f;
constexpr float kRowY = 14.f;

std::string tr(char const* key, char const* fallback) {
    return paimon::loc::tr(key, fallback);
}

struct Section {
    CCNode* node = nullptr;
    CCLabelBMFont* heading = nullptr;
};

// dark inset with a gold heading and divider, same look as paimon::ui::makePanel,
// but the heading is returned so the header can share its row.
Section makeSection(CCSize size, std::string const& title) {
    Section section;
    section.node = CCNode::create();
    section.node->setAnchorPoint({0.f, 0.f});
    section.node->setContentSize(size);
    section.node->addChild(paimon::ui::makeInset(size, 90), -1);

    section.heading = paimon::ui::makeTitle(title.c_str(), size.width - 20.f, .5f);
    section.heading->setAnchorPoint({0.f, .5f});
    section.heading->setPosition({10.f, size.height - 12.f});
    section.node->addChild(section.heading);

    auto* line = paimon::ui::makeDivider(size.width - 16.f, paimon::ui::palette::gold, 90);
    line->setPosition({size.width * .5f, size.height - paimon::ui::kPanelHeader + 1.f});
    section.node->addChild(line);
    return section;
}

// checkbox on the left like GD's own options, title and one line of help beside it.
CCMenuItemToggler* addSwitchRow(CCNode* card, CCMenu* menu, float midY, std::string const& title,
    std::string const& help, bool value, std::function<void(bool)> onChange) {
    auto* toggle = paimon::ui::makeToggle(value, std::move(onChange), .55f);
    toggle->setPosition({18.f, midY});
    menu->addChild(toggle);

    float const textWidth = kPanelWidth - kTextLeft - 12.f;
    auto* label = paimon::ui::makeLabel(title.c_str(), textWidth, .36f);
    label->setAnchorPoint({0.f, .5f});
    label->setPosition({kTextLeft, midY + 5.f});
    card->addChild(label);

    auto* sub = CCLabelBMFont::create(help.c_str(), "chatFont.fnt");
    sub->limitLabelWidth(textWidth, .42f, .1f);
    sub->setColor(paimon::ui::palette::muted);
    sub->setAnchorPoint({0.f, .5f});
    sub->setPosition({kTextLeft, midY - 6.5f});
    card->addChild(sub);
    return toggle;
}

}

TranslatorSettingsPopup* TranslatorSettingsPopup::create() {
    auto* popup = new TranslatorSettingsPopup();
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool TranslatorSettingsPopup::init() {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    setID("translator-settings-popup"_spr);
    setTitle(tr("translator.title", "Translator").c_str());
    addInfoButton(tr("translator.title", "Translator"), tr("translator.info_body", ""));
    addCorners();

    // every change saves on the spot, so the close button is the only exit.
    buildSwitches();
    buildLanguages();
    buildEndpoint();

    syncFromSettings();
    return true;
}

void TranslatorSettingsPopup::buildSwitches() {
    auto settings = loadSettings();

    auto* card = CCNode::create();
    card->setID("translator-switches"_spr);
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize({kPanelWidth, kSwitchesHeight});
    card->setPosition({kMargin, kSwitchesBottom});
    card->addChild(paimon::ui::makeInset(card->getContentSize(), 90), -1);
    m_mainLayer->addChild(card, 1);

    auto* menu = paimon::ui::makeZeroMenu();
    menu->setContentSize(card->getContentSize());
    card->addChild(menu, 2);

    m_enabledToggle = addSwitchRow(card, menu, kSwitchRowHeight * 1.5f,
        tr("translator.enable", "Enable translator"),
        tr("translator.enable_desc", "Shows the T on comments and descriptions"),
        settings.enabled, [](bool value) {
            saveEnabled(value);
            refreshCommentCells();
        });
    m_enabledToggle->setID("translator-enabled-toggle"_spr);

    m_autoToggle = addSwitchRow(card, menu, kSwitchRowHeight * .5f,
        tr("translator.auto", "Auto translate"),
        tr("translator.auto_desc", "Translates text as soon as it loads"),
        settings.autoTranslate, [](bool value) {
            saveAutoTranslate(value);
            refreshCommentCells();
        });
    m_autoToggle->setID("translator-auto-toggle"_spr);

    auto* line = paimon::ui::makeDivider(kPanelWidth - 24.f, paimon::ui::palette::dim, 60);
    line->setPosition({kPanelWidth * .5f, kSwitchRowHeight});
    card->addChild(line);
}

void TranslatorSettingsPopup::buildLanguages() {
    auto settings = loadSettings();

    auto section = makeSection({kPanelWidth, kLanguagesHeight},
        tr("translator.target", "Target language"));
    section.node->setID("translator-languages"_spr);
    section.node->setPosition({kMargin, kLanguagesBottom});
    m_mainLayer->addChild(section.node, 1);

    // "Comments translate to X" shares the heading row, right aligned.
    m_hint = CCLabelBMFont::create("", "chatFont.fnt");
    m_hint->setColor(paimon::ui::palette::muted);
    m_hint->setAnchorPoint({1.f, .5f});
    m_hint->setPosition({kPanelWidth - 10.f, kLanguagesHeight - 12.f});
    section.node->addChild(m_hint);
    m_hintMaxWidth = std::max(40.f,
        kPanelWidth - 32.f - section.heading->getScaledContentSize().width);

    auto* menu = paimon::ui::makeZeroMenu("translator-language-menu"_spr);
    menu->setContentSize(section.node->getContentSize());
    section.node->addChild(menu, 2);

    auto const& entries = languages();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        auto const& entry = entries[index];
        auto* tile = ui::makeLanguageTile(entry, settings.target == entry.code,
            kTileWidth, kTileScale);
        if (!tile) continue;

        auto* button = CCMenuItemExt::createSpriteExtra(tile,
            [this, code = std::string(entry.code)](CCMenuItemSpriteExtra*) {
                saveTarget(code);
                syncFromSettings();
            });
        button->setID(std::string("translator-language-"_spr) + entry.code);
        button->setPosition({
            kPanelWidth * .5f + (static_cast<float>(index % 3) - 1.f) * kColumnStep,
            kFirstRowY - static_cast<float>(index / 3) * kRowStep,
        });
        menu->addChild(button);
        m_languageButtons.push_back(button);
        m_languageCodes.emplace_back(entry.code);
        paimon::ui::animateIn(button, .015f * static_cast<float>(index), 4.f);
    }
}

void TranslatorSettingsPopup::buildEndpoint() {
    auto section = makeSection({kPanelWidth, kEndpointHeight},
        tr("translator.endpoint", "Your translation endpoint"));
    section.node->setID("translator-endpoint"_spr);
    section.node->setPosition({kMargin, kEndpointBottom});
    m_mainLayer->addChild(section.node, 1);

    auto* menu = paimon::ui::makeZeroMenu("translator-endpoint-menu"_spr);
    menu->setContentSize(section.node->getContentSize());
    section.node->addChild(menu, 2);

    // input, Save and reset share one row so the section stays short.
    m_endpointInput = TextInput::create(kInputWidth / kInputScale, defaultEndpoint(), "chatFont.fnt");
    m_endpointInput->setID("translator-endpoint-input"_spr);
    m_endpointInput->setScale(kInputScale);
    m_endpointInput->setMaxCharCount(300);
    m_endpointInput->setCommonFilter(CommonFilter::Any);
    m_endpointInput->setString(loadSettings().endpoint);
    m_endpointInput->setPosition({10.f + kInputWidth * .5f, kRowY});
    section.node->addChild(m_endpointInput, 1);

    float x = 10.f + kInputWidth + 8.f;
    auto* save = paimon::ui::makeButton(tr("translator.apply", "Save").c_str(), [this] {
        saveEndpoint(m_endpointInput ? m_endpointInput->getString() : std::string());
        if (m_endpointInput) m_endpointInput->setString(loadSettings().endpoint);
    }, paimon::ui::Btn::Green, 64.f, .5f, "bigFont.fnt");
    save->setID("translator-endpoint-save"_spr);
    save->setPosition({x + save->getScaledContentSize().width * .5f, kRowY});
    menu->addChild(save);
    x += save->getScaledContentSize().width + 8.f;

    // restores the built-in endpoint; the round arrows are GD's own refresh icon.
    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png")) {
        icon->setScale(22.f / std::max(1.f, icon->getContentSize().height));
        auto* reset = CCMenuItemExt::createSpriteExtra(icon, [this](CCMenuItemSpriteExtra*) {
            saveEndpoint("");
            if (m_endpointInput) m_endpointInput->setString(defaultEndpoint());
        });
        reset->setID("translator-endpoint-default"_spr);
        reset->setPosition({x + reset->getContentSize().width * .5f, kRowY});
        menu->addChild(reset);
    }
}

void TranslatorSettingsPopup::syncFromSettings() {
    auto settings = loadSettings();

    if (m_enabledToggle) m_enabledToggle->toggle(settings.enabled);
    if (m_autoToggle) m_autoToggle->toggle(settings.autoTranslate);

    for (std::size_t index = 0; index < m_languageButtons.size(); ++index) {
        ui::setLanguageTileSelected(m_languageButtons[index], m_languageCodes[index] == settings.target);
    }

    if (m_hint) {
        auto* entry = findLanguage(settings.target);
        auto name = entry ? std::string(entry->nativeName) : settings.target;
        auto body = fmt::format("{} {}", tr("translator.hint", "Comments translate to"), name);
        m_hint->setString(body.c_str());
        m_hint->limitLabelWidth(m_hintMaxWidth, .45f, .1f);
    }

    refreshCommentCells();
}

}
