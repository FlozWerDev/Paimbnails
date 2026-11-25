#include "ShopPresetPopup.hpp"
#include "ShopUI.hpp"
#include "../services/ShopConfiguration.hpp"
#include "../../community-base/services/CommunityItems.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <memory>

using namespace geode::prelude;

namespace paimon::shop {
ShopPresetPopup* ShopPresetPopup::create(std::string id, Preset preset, std::function<void()> onChanged) {
    auto popup = std::make_unique<ShopPresetPopup>();
    if (!popup->init(std::move(id), std::move(preset), std::move(onChanged))) return nullptr;
    popup->autorelease();
    return popup.release();
}

bool ShopPresetPopup::init(std::string id, Preset preset, std::function<void()> onChanged) {
    if (!PaimonPopup::init(440.f, 280.f)) return false;
    m_id = std::move(id);
    m_preset = std::move(preset);
    m_onChanged = std::move(onChanged);
    setTitle(m_preset.title, "goldFont.fnt", 0.6f);
    auto* preview = makePreview(m_preset.preview, {224.f, 132.f});
    preview->setPosition({16.f, 99.f});
    m_mainLayer->addChild(preview);
    auto addLabel = [this](std::string const& value, float y, ccColor3B color) {
        auto* label = paimon::ui::makeLabel(value.c_str(), 164.f, 0.36f, color);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({252.f, y});
        m_mainLayer->addChild(label);
    };
    addLabel(categoryName(m_preset.category), 224.f, paimon::ui::palette::info);
    addLabel(m_preset.author, 204.f, paimon::ui::palette::gold);
    addLabel(text("Guardado local", "Saved locally"), 186.f, paimon::ui::palette::muted);
    auto* config = paimon::ui::makeText(describeConfiguration(m_preset).c_str(), 164.f, 0.43f);
    config->setAnchorPoint({0.f, 1.f});
    config->setPosition({252.f, 171.f});
    m_mainLayer->addChild(config);
    auto* description = paimon::ui::makeText(m_preset.description.empty() ? text("Sin descripcion", "No description") : m_preset.description.c_str(), 400.f, 0.43f);
    description->setAnchorPoint({0.f, 1.f});
    description->setPosition({20.f, 88.f});
    // long descriptions stay inside their allotted space.
    if (description->getScaledContentSize().height > 38.f) description->setScale(description->getScale() * 38.f / description->getScaledContentSize().height);
    m_mainLayer->addChild(description);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);
    auto* applyButton = paimon::ui::makeButton(text("Aplicar", "Apply"), [this] { apply(); }, paimon::ui::Btn::Green, 96.f, 0.45f, "bigFont.fnt");
    applyButton->setPosition({70.f, 26.f});
    menu->addChild(applyButton);
    auto* exportButton = paimon::ui::makeButton(text("Exportar", "Export"), [this] { exportPreset(); }, paimon::ui::Btn::Cyan, 96.f, 0.45f, "bigFont.fnt");
    exportButton->setPosition({172.f, 26.f});
    menu->addChild(exportButton);
    // paimon base: the preset goes to the moderation queue with its preview.
    auto* publishButton = paimon::ui::makeButton(text("Publicar", "Publish"), [this] {
        PaimonNotify::show(text("Enviando preset...", "Sending preset..."), NotificationIcon::Loading);
        paimon::community::items::publishPreset(m_preset, [](bool ok, std::string const& message) {
            PaimonNotify::show(message, ok ? NotificationIcon::Success : NotificationIcon::Error);
        });
    }, paimon::ui::Btn::Pink, 96.f, 0.45f, "bigFont.fnt");
    publishButton->setPosition({274.f, 26.f});
    menu->addChild(publishButton);
    auto* removeButton = paimon::ui::makeButton(text("Eliminar", "Delete"), [this] { deletePreset(); }, paimon::ui::Btn::Red, 80.f, 0.45f, "bigFont.fnt");
    removeButton->setPosition({372.f, 26.f});
    menu->addChild(removeButton);
    paimon::markDynamicPopup(this);
    return true;
}

void ShopPresetPopup::apply() {
    WeakRef<ShopPresetPopup> weak = this;
    geode::createQuickPopup("Paimon Shop", text(
        "Se reemplazaran los ajustes de esta categoria. Se guardara un <cy>respaldo</c> para restaurarlos desde el Shop. El layout se vera al volver al menu.",
        "This replaces the settings in this category. A <cy>backup</c> lets you restore them from the Shop. Layout changes appear when you return to the menu."),
        text("Cancelar", "Cancel"), text("Aplicar", "Apply"), [weak](auto*, bool confirmed) {
            auto popup = weak.lock();
            if (!confirmed || !popup || !popup->getParent()) return;
            auto result = applyConfiguration(popup->m_preset.category, popup->m_preset.configuration);
            if (!result) { showShopError(result.unwrapErr()); return; }
            PaimonNotify::show(text("Preset aplicado. Respaldo guardado.", "Preset applied. Backup saved."), NotificationIcon::Success);
            if (popup->m_onChanged) popup->m_onChanged();
            popup->onClose(nullptr);
        });
}

void ShopPresetPopup::exportPreset() {
    WeakRef<ShopPresetPopup> weak = this;
    pt::saveJson("paimon-shop-preset.json", [weak](auto result) {
        auto popup = weak.lock();
        if (!popup || !popup->getParent()) return;
        if (!result) { showShopError(result.unwrapErr()); return; }
        if (!result.unwrap()) return;
        if (!paimon::file::writeAtomically(*result.unwrap(), presetToJson(popup->m_preset).dump())) {
            showShopError(text("No se pudo exportar el archivo.", "Could not export file."));
            return;
        }
        PaimonNotify::show(text("Preset exportado con su preview.", "Preset exported with its preview."), NotificationIcon::Success);
    });
}

void ShopPresetPopup::deletePreset() {
    WeakRef<ShopPresetPopup> weak = this;
    geode::createQuickPopup("Paimon Shop", text("Eliminar este preset de la biblioteca local? Los ajustes aplicados se conservan.",
        "Delete this preset from the local library? Applied settings are kept."), text("Cancelar", "Cancel"), text("Eliminar", "Delete"),
        [weak](auto*, bool confirmed) {
            auto popup = weak.lock();
            if (!confirmed || !popup || !popup->getParent()) return;
            auto result = localLibrary().remove(popup->m_id);
            if (!result) { showShopError(result.unwrapErr()); return; }
            if (popup->m_onChanged) popup->m_onChanged();
            popup->onClose(nullptr);
        });
}
} // namespace paimon::shop
