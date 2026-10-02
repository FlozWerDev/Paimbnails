#include "NewProjectPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../engine/PackMetadataBuilder.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/LiveSlotRuntime.hpp"

using namespace geode::prelude;

namespace paimon::texture_studio {

NewProjectPopup* NewProjectPopup::create(SlotCreatedCallback cb) {
    auto* popup = new NewProjectPopup;
    if (popup->init(std::move(cb))) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool NewProjectPopup::init(SlotCreatedCallback cb) {
    if (!PaimonPopup::init(320.f, 160.f)) return false;
    paimon::markDynamicPopup(this);
    m_onCreated = std::move(cb);
    setTitle("New color slot");
    m_nameInput = TextInput::create(260.f, "My slot");
    m_nameInput->setString("My slot");
    m_nameInput->setMaxCharCount(40);
    m_mainLayer->addChildAtPosition(m_nameInput, Anchor::Center, {0, 10.f});
    auto* hint = CCLabelBMFont::create("Choose your palette in the next screen.", "chatFont.fnt");
    hint->setScale(.55f);
    m_mainLayer->addChildAtPosition(hint, Anchor::Center, {0, -20.f});
    auto* sprite = ButtonSprite::create("Create slot", "goldFont.fnt", "GJ_button_01.png", .55f);
    auto* create = CCMenuItemExt::createSpriteExtra(sprite,
        [this](CCMenuItemSpriteExtra*) { onCreateClicked(nullptr); });
    m_buttonMenu->addChildAtPosition(create, Anchor::Bottom, {0, 24.f});
    return true;
}

void NewProjectPopup::onCreateClicked(CCObject*) {
    std::string name = m_nameInput->getString();
    if (name.find_first_not_of(" \t\r\n") == std::string::npos) {
        Notification::create("Enter a slot name.", NotificationIcon::Warning)->show();
        return;
    }
    TextureProject project;
    project.name = name;
    project.id = PackMetadataBuilder::buildPackId(name);
    project.author = "Paimbnails";
    project.createdAt = project.modifiedAt = nowUnixMs();
    LiveSlotRuntime::normalize(project);
    auto created = SlotStore::get().createSlot(project);
    if (!created) {
        Notification::create("Create failed: " + created.unwrapErr(), NotificationIcon::Error)->show();
        return;
    }
    auto callback = m_onCreated;
    auto id = created.unwrap();
    onClose(nullptr);
    if (callback) callback(id);
}

}
