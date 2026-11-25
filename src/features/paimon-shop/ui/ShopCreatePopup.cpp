#include "ShopCreatePopup.hpp"
#include "ShopUI.hpp"
#include "../services/ShopConfiguration.hpp"
#include "../../../utils/Base64.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/ui/TextInput.hpp>
#include <chrono>
#include <memory>

using namespace geode::prelude;

namespace paimon::shop {
ShopCreatePopup* ShopCreatePopup::create(std::function<void()> onCreated) {
    auto popup = std::make_unique<ShopCreatePopup>();
    if (!popup->init(std::move(onCreated))) return nullptr;
    popup->autorelease();
    return popup.release();
}

ShopCreatePopup::~ShopCreatePopup() {
    paimon::ui::detachGeodeTextInput(m_name);
    paimon::ui::detachGeodeTextInput(m_description);
}

bool ShopCreatePopup::init(std::function<void()> onCreated) {
    if (!PaimonPopup::init(440.f, 280.f)) return false;
    m_onCreated = std::move(onCreated);
    setTitle(text("Crear preset", "Create preset"));
    auto* hint = paimon::ui::makeLabel(text("Comparte tus ajustes actuales con una preview", "Share your current settings with a preview"), 396.f, 0.32f, paimon::ui::palette::muted);
    hint->setPosition({220.f, 240.f});
    m_mainLayer->addChild(hint);
    m_name = TextInput::create(220.f, text("Nombre del preset", "Preset name"), "chatFont.fnt");
    m_name->setCommonFilter(CommonFilter::Text);
    m_name->setMaxCharCount(48);
    m_name->setPosition({130.f, 207.f});
    m_mainLayer->addChild(m_name);
    m_description = TextInput::create(220.f, text("Descripcion", "Description"), "chatFont.fnt");
    m_description->setCommonFilter(CommonFilter::TextEmojis);
    m_description->setMaxCharCount(400);
    m_description->setPosition({130.f, 169.f});
    m_mainLayer->addChild(m_description);
    auto author = std::string(GameManager::get()->m_playerName);
    auto* authorLabel = paimon::ui::makeLabel((std::string(text("Autor: ", "Author: ")) + author).c_str(), 218.f, 0.3f, paimon::ui::palette::gold);
    authorLabel->setAnchorPoint({0.f, 0.5f});
    authorLabel->setPosition({20.f, 139.f});
    m_mainLayer->addChild(authorLabel);
    m_previewHolder = CCNode::create();
    m_previewHolder->setPosition({258.f, 156.f});
    m_mainLayer->addChild(m_previewHolder);
    m_previewHolder->addChild(makePreview("", {164.f, 76.f}));
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);
    auto* previewButton = paimon::ui::makeButton(text("Elegir PNG", "Choose PNG"), [this] { choosePreview(); }, paimon::ui::Btn::Cyan, 150.f, 0.4f, "bigFont.fnt");
    previewButton->setPosition({340.f, 138.f});
    menu->addChild(previewButton);
    for (int i = 0; i < 4; ++i) {
        auto category = static_cast<Category>(i);
        auto* button = paimon::ui::makeButton(categoryName(category), [this, category] { selectCategory(category); }, paimon::ui::Btn::Gray, 94.f, 0.36f, "bigFont.fnt");
        button->setPosition({58.f + i * 108.f, 106.f});
        menu->addChild(button);
        m_categories.push_back(button);
    }
    m_summary = paimon::ui::makeLabel("", 394.f, 0.29f, paimon::ui::palette::info);
    m_summary->setPosition({220.f, 78.f});
    m_mainLayer->addChild(m_summary);
    auto* local = paimon::ui::makeLabel(text("Solo en este equipo. Exporta el JSON para compartirlo.", "On this device. Export the JSON to share it."), 394.f, 0.28f, paimon::ui::palette::muted);
    local->setPosition({220.f, 57.f});
    m_mainLayer->addChild(local);
    auto* save = paimon::ui::makeButton(text("Guardar preset", "Save preset"), [this] { savePreset(); }, paimon::ui::Btn::Green, 175.f, 0.5f, "bigFont.fnt");
    save->setPosition({220.f, 27.f});
    menu->addChild(save);
    selectCategory(Category::Layout);
    paimon::markDynamicPopup(this);
    return true;
}

void ShopCreatePopup::selectCategory(Category category) {
    m_category = category;
    for (size_t i = 0; i < m_categories.size(); ++i) paimon::ui::setButtonSkin(m_categories[i], i == static_cast<size_t>(category) ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    m_summary->setString(category == Category::Pack ? text("Layout + RTX + Beat Shaders", "Layout + RTX + Beat Shaders")
        : text("Se guardaran los ajustes actuales de esta categoria", "Current settings for this category will be saved"));
}

void ShopCreatePopup::choosePreview() {
    WeakRef<ShopCreatePopup> weak = this;
    pt::pickImage([weak](auto result) {
        auto popup = weak.lock();
        if (!popup || !popup->getParent()) return;
        if (!result) { showShopError(result.unwrapErr()); return; }
        if (!result.unwrap()) return;
        auto bytes = paimon::file::readBytes(*result.unwrap(), kMaxPreviewBytes);
        auto valid = validatePreview(bytes);
        if (!valid) { showShopError(valid.unwrapErr()); return; }
        auto decoded = ImageLoadHelper::loadWithSTBFromMemory(bytes.data(), bytes.size(), false);
        if (!decoded.success || !decoded.texture) { showShopError(text("No se pudo leer el PNG.", "Could not decode PNG.")); return; }
        decoded.texture->release();
        popup->m_preview = paimon::base64Encode(bytes);
        popup->m_previewHolder->removeAllChildren();
        popup->m_previewHolder->addChild(makePreview(popup->m_preview, {164.f, 76.f}));
    });
}

void ShopCreatePopup::savePreset() {
    if (m_saved) return;
    if (m_preview.empty()) { showShopError(text("Elige una captura PNG como vista previa.", "Choose a PNG screenshot as the preview.")); return; }
    Preset preset;
    preset.title = m_name->getString();
    preset.description = m_description->getString();
    preset.author = std::string(GameManager::get()->m_playerName);
    if (preset.author.empty()) preset.author = text("Jugador local", "Local player");
    preset.accountID = std::max(0, GJAccountManager::get()->m_accountID);
    preset.createdAt = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    preset.category = m_category;
    preset.configuration = captureConfiguration(m_category);
    preset.preview = m_preview;
    auto result = localLibrary().add(preset);
    if (!result) { showShopError(result.unwrapErr()); return; }
    m_saved = true;
    PaimonNotify::show(text("Preset guardado en Paimon Shop.", "Preset saved in Paimon Shop."), NotificationIcon::Success);
    if (m_onCreated) m_onCreated();
    onClose(nullptr);
}
} // namespace paimon::shop
