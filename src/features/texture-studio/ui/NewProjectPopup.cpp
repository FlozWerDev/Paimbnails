#include "NewProjectPopup.hpp"

#include "../../../ui/PaimonUI.hpp"
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
    if (!PaimonPopup::init(320.f, 170.f)) return false;
    paimon::markDynamicPopup(this);
    m_onCreated = std::move(cb);
    setTitle("New color slot");
    this->addInfoButton("New color slot",
        "Name your texture slot. You pick its <cy>palette</c> and colors on the "
        "next screen. <cg>Create slot</c> saves it and opens the editor.");

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    CCSize const panelSize{content.width - 36.f, 86.f};
    auto panel = paimon::ui::makePanel(panelSize, "Slot name");
    panel->setPosition({cx - panelSize.width / 2.f, 44.f});
    m_mainLayer->addChild(panel);

    float const panelCx = panelSize.width / 2.f;

    m_nameInput = TextInput::create(panelSize.width - 24.f, "My slot");
    m_nameInput->setString("My slot");
    m_nameInput->setMaxCharCount(40);
    m_nameInput->setPosition({panelCx, panelSize.height - paimon::ui::kPanelHeader - 14.f});
    panel->addChild(m_nameInput);

    auto* hint = paimon::ui::makeText("Choose your palette in the next screen.",
        panelSize.width - 24.f, 0.45f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
    hint->setPosition({panelCx, 16.f});
    panel->addChild(hint);

    auto* create = paimon::ui::makeButton("Create slot",
        [this] { this->onCreateClicked(nullptr); }, paimon::ui::Btn::Green, 0.f, 0.6f);
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
