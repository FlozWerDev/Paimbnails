#include "SearchPresetsPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr float kPopupW = 360.f;
constexpr float kPopupH = 250.f;
constexpr float kListW = 326.f;
constexpr float kListH = 140.f;
constexpr float kRowH = 34.f;

} // namespace

SearchPresetsPopup* SearchPresetsPopup::createPicker(
    std::function<void(AdvancedQuery const&)> onPick) {
    auto ret = new SearchPresetsPopup();
    if (ret && ret->init(false, {}, std::move(onPick), nullptr)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

SearchPresetsPopup* SearchPresetsPopup::createSaveDialog(
    AdvancedQuery query, std::function<void()> onSaved) {
    auto ret = new SearchPresetsPopup();
    if (ret && ret->init(true, std::move(query), nullptr, std::move(onSaved))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SearchPresetsPopup::init(bool saveMode, AdvancedQuery query,
                              std::function<void(AdvancedQuery const&)> onPick,
                              std::function<void()> onSaved) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_saveMode = saveMode;
    m_query = std::move(query);
    m_onPick = std::move(onPick);
    m_onSaved = std::move(onSaved);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle(m_saveMode ? "Guardar preset" : "Presets guardados");
    this->addInfoButton(m_saveMode ? "Guardar preset" : "Presets guardados",
        m_saveMode
            ? "Dale un <cy>nombre</c> a la busqueda actual para reutilizarla luego "
              "con un toque desde <cg>Presets guardados</c>."
            : "Tus busquedas guardadas. <cg>Usar</c> aplica el preset; "
              "el icono <cr>papelera</c> lo borra.");

    float listTop = content.height - 46.f;

    if (m_saveMode) {
        CCSize const panelSize{kListW, 92.f};
        auto panel = paimon::ui::makePanel(panelSize, "Nuevo preset");
        panel->setPosition({cx - panelSize.width / 2.f, listTop - panelSize.height});
        m_mainLayer->addChild(panel);

        float const panelCx = panelSize.width / 2.f;

        auto summary = paimon::ui::makeText(describeQuery(m_query).c_str(),
            panelSize.width - 24.f, 0.44f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
        summary->setPosition({panelCx, panelSize.height - paimon::ui::kPanelHeader - 14.f});
        panel->addChild(summary);

        m_nameInput = TextInput::create(kListW - 40.f, "Nombre del preset", "chatFont.fnt");
        m_nameInput->setMaxCharCount(28);
        m_nameInput->setPosition({panelCx, 24.f});
        panel->addChild(m_nameInput);

        auto saveBtn = paimon::ui::makeButton("Guardar",
            [this] { this->onSave(nullptr); }, paimon::ui::Btn::Green, 0.f, 0.7f);
        saveBtn->setPosition({cx, 30.f});
        m_buttonMenu->addChild(saveBtn);

        listTop -= 74.f;
    }

    float listH = m_saveMode ? 74.f : kListH;
    float listCenterY = listTop - 10.f - listH / 2.f;

    auto bg = paimon::ui::makeInset({kListW, listH}, 90);
    bg->setPosition({cx - kListW / 2.f, listCenterY - listH / 2.f});
    m_mainLayer->addChild(bg, -1);

    m_scroll = ScrollLayer::create({kListW, listH});
    m_scroll->setPosition({cx - kListW / 2.f, listCenterY - listH / 2.f});
    m_mainLayer->addChild(m_scroll);

    m_emptyLabel = paimon::ui::makeText("Todavia no guardaste ninguno",
        kListW - 20.f, 0.46f, paimon::ui::palette::dim, kCCTextAlignmentCenter);
    m_emptyLabel->setPosition({cx, listCenterY});
    m_mainLayer->addChild(m_emptyLabel);

    rebuildList();
    return true;
}

void SearchPresetsPopup::rebuildList() {
    if (!m_scroll) return;

    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildren();

    auto const& list = presets();
    if (m_emptyLabel) m_emptyLabel->setVisible(list.empty());

    float viewH = m_scroll->getContentSize().height;
    float totalH = std::max(viewH, list.size() * kRowH + 4.f);
    content->setContentSize({kListW, totalH});

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kListW, totalH});
    content->addChild(menu, 2);

    float y = totalH - 2.f;
    for (int i = 0; i < static_cast<int>(list.size()); i++) {
        auto const& preset = list[i];
        y -= kRowH;

        auto band = CCLayerColor::create(ccc4(255, 255, 255, i % 2 == 0 ? 14 : 0));
        band->setContentSize({kListW, kRowH});
        band->setPosition({0.f, y});
        content->addChild(band, 0);

        auto name = CCLabelBMFont::create(preset.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.42f);
        name->limitLabelWidth(kListW - 90.f, 0.42f, 0.2f);
        name->setPosition({10.f, y + kRowH - 11.f});
        content->addChild(name, 1);

        auto desc = CCLabelBMFont::create(describeQuery(preset.query).c_str(), "chatFont.fnt");
        desc->setAnchorPoint({0.f, 0.5f});
        desc->setScale(0.38f);
        desc->limitLabelWidth(kListW - 90.f, 0.38f, 0.18f);
        desc->setColor({165, 165, 165});
        desc->setPosition({10.f, y + 11.f});
        content->addChild(desc, 1);

        if (!m_saveMode) {
            auto useSpr = paimon::ui::makeButtonSprite("Usar", paimon::ui::Btn::Green, 0.f, 0.6f, "bigFont.fnt");
            auto useBtn = CCMenuItemSpriteExtra::create(
                useSpr, this, menu_selector(SearchPresetsPopup::onPick));
            useBtn->setPosition({kListW - 56.f, y + kRowH / 2.f});
            useBtn->setTag(i);
            menu->addChild(useBtn);
        }

        auto delSprite = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteIcon_001.png");
        if (delSprite) {
            delSprite->setScale(0.6f);
            auto delBtn = CCMenuItemSpriteExtra::create(
                delSprite, this, menu_selector(SearchPresetsPopup::onDelete));
            delBtn->setPosition({kListW - 20.f, y + kRowH / 2.f});
            delBtn->setTag(i);
            menu->addChild(delBtn);
        }
    }

    m_scroll->moveToTop();
}

void SearchPresetsPopup::onPick(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    auto const& list = presets();
    if (index < 0 || index >= static_cast<int>(list.size())) return;

    auto query = list[index].query;
    auto callback = m_onPick;
    this->onClose(nullptr);
    if (callback) callback(query);
}

void SearchPresetsPopup::onDelete(CCObject* sender) {
    int index = static_cast<CCNode*>(sender)->getTag();
    removePreset(index);
    rebuildList();
}

void SearchPresetsPopup::onSave(CCObject*) {
    if (!m_nameInput) return;

    std::string name(m_nameInput->getString());
    if (name.empty()) {
        PaimonNotify::create("Ponle un nombre al preset", NotificationIcon::Warning)->show();
        return;
    }

    addPreset(name, m_query);

    auto callback = m_onSaved;
    this->onClose(nullptr);
    if (callback) callback();
}

void SearchPresetsPopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_nameInput);
    m_nameInput = nullptr;
    Popup::onClose(sender);
}

} // namespace paimon::info
