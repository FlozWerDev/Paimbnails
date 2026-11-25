#include "ButtonActionPopup.hpp"

#include "../ButtonSkinEntry.hpp"
#include "../services/ButtonSkinRuntime.hpp"
#include "../services/ButtonSkinStore.hpp"
#include "../../quick-hub/services/QuickHubButtonCapture.hpp"
#include "../../quick-hub/ui/QuickButtonPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace paimon::buttonskin {

namespace {

namespace ui = paimon::ui;

constexpr float kPopupW = 300.f;
constexpr float kPopupH = 180.f;
constexpr float kPreviewBox = 84.f;
constexpr float kPreviewCx = 18.f + kPreviewBox / 2.f;
constexpr float kPreviewCy = 30.f + kPreviewBox / 2.f;
constexpr float kButtonsCx = 198.f;
constexpr float kButtonW = 168.f;

} // namespace

ButtonActionPopup* ButtonActionPopup::s_instance = nullptr;

bool ButtonActionPopup::isOpen() {
    return s_instance != nullptr;
}

ButtonActionPopup* ButtonActionPopup::create(SkinEditTarget target, std::string skinId,
                                             quickhub::CustomQuickButton quickHubIdentity) {
    auto* ret = new ButtonActionPopup();
    ret->m_target = std::move(target);
    ret->m_quickHubIdentity = std::move(quickHubIdentity);
    if (!skinId.empty()) ret->m_skin = ButtonSkinStore::get().find(skinId);
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ButtonActionPopup::init() {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    s_instance = this;
    ButtonSkinRuntime::markSkinUi(this);
    this->setID("button-actions"_spr);

    auto const& id = m_target.identity;
    this->setTitle(id.name.empty() ? "Boton" : id.name.c_str());
    this->addCorners();

    std::string screen = quickhub::friendlyScreenName(id.ownerClass.empty() ? id.sceneClass : id.ownerClass);
    auto* subtitle = CCLabelBMFont::create(fmt::format("Pantalla: {}", screen).c_str(), "chatFont.fnt");
    subtitle->limitLabelWidth(kPopupW - 40.f, 0.42f, 0.16f);
    subtitle->setColor({190, 200, 220});
    subtitle->setPosition({kPopupW / 2.f, kPopupH - 42.f});
    m_mainLayer->addChild(subtitle, 2);

    if (auto* frame = ui::makeInset({kPreviewBox, kPreviewBox}, 110)) {
        frame->setPosition({kPreviewCx - kPreviewBox / 2.f, kPreviewCy - kPreviewBox / 2.f});
        m_mainLayer->addChild(frame, 0);
    }
    if (m_target.shape) {
        // the button as it looks with its current style.
        auto* preview = buildSkinPreview(m_skin.value_or(ButtonSkin{}), m_target.shape,
                                         kPreviewBox - 10.f, m_target.pointScale, false);
        preview->setPosition({kPreviewCx, kPreviewCy});
        m_mainLayer->addChild(preview, 1);
    }

    std::vector<CCMenuItemSpriteExtra*> buttons;
    float y = m_skin ? 118.f : 104.f;
    auto addAction = [&](char const* text, ui::Btn skin, std::function<void()> cb) {
        auto* button = ui::makeButton(text, std::move(cb), skin, kButtonW, 0.62f, "bigFont.fnt");
        button->setPosition({kButtonsCx, y});
        m_buttonMenu->addChild(button);
        buttons.push_back(button);
        y -= 32.f;
    };
    addAction("Anadir al Quick Hub", ui::Btn::Cyan, [this] { this->onQuickHub(); });
    addAction(m_skin ? "Editar estilo" : "Personalizar boton", ui::Btn::Green,
        [this] { this->onCustomize(); });
    if (m_skin) addAction("Quitar estilo", ui::Btn::Red, [this] { this->onRemove(); });
    ui::matchButtonLabels(buttons);

    auto* hint = CCLabelBMFont::create("Personalizar: imagen, color, forma y sonido.", "chatFont.fnt");
    hint->limitLabelWidth(kPopupW - 40.f, 0.36f, 0.16f);
    hint->setColor({150, 160, 185});
    hint->setPosition({kPopupW / 2.f, 14.f});
    m_mainLayer->addChild(hint, 2);
    return true;
}

void ButtonActionPopup::onExit() {
    if (s_instance == this) s_instance = nullptr;
    PaimonPopup::onExit();
}

void ButtonActionPopup::onQuickHub() {
    auto identity = m_quickHubIdentity;
    this->keyBackClicked();
    quickhub::openQuickHubButtonPopup(std::move(identity));
}

void ButtonActionPopup::onCustomize() {
    auto target = m_target;
    auto skin = m_skin;
    this->keyBackClicked();
    if (ButtonSkinPopup::isOpen()) return;
    if (auto* editor = ButtonSkinPopup::create(std::move(target), std::move(skin))) editor->show();
}

void ButtonActionPopup::onRemove() {
    if (!m_skin) return;
    WeakRef<ButtonActionPopup> self = this;
    geode::createQuickPopup("Quitar estilo",
        m_skin->scope == SkinScope::Exact
            ? std::string("El boton vuelve a verse y sonar como el original.")
            : std::string("Todos los botones con este estilo vuelven a ser los originales."),
        "Cancelar", "Quitar", [self](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup) return;
            auto* chooser = static_cast<ButtonActionPopup*>(popup.data());
            if (chooser->m_skin) ButtonSkinStore::get().remove(chooser->m_skin->id);
            ButtonSkinRuntime::get().requestScan();
            PaimonNotify::show("Estilo quitado.", NotificationIcon::Success);
            chooser->keyBackClicked();
        });
}

bool isButtonSkinUiOpen() {
    return ButtonActionPopup::isOpen() || ButtonSkinPopup::isOpen();
}

void openButtonActions(CCMenuItem* button, CCScene* scene) {
    if (!button || !scene || isButtonSkinUiOpen() || quickhub::QuickButtonPopup::isOpen()) return;

    auto& runtime = ButtonSkinRuntime::get();
    // the quick hub keeps capturing exactly what was clicked (a toggle's inner state included).
    auto* skinItem = ButtonSkinRuntime::skinnableItemFor(button);
    // identity and picture must describe the stock button, not its current style.
    std::string skinId = runtime.release(skinItem);
    auto quickHubIdentity = quickhub::captureButtonIdentity(button, scene);

    SkinEditTarget target;
    target.item = skinItem;
    target.identity = skinItem == button ? quickHubIdentity
                                         : quickhub::captureButtonIdentity(skinItem, scene);
    target.signature = quickhub::buttonVisualSignature(skinItem);
    // the picture comes from the state on screen, which is the button under the cursor.
    if (auto* spriteItem = typeinfo_cast<CCMenuItemSprite*>(button)) {
        if (auto* normal = spriteItem->getNormalImage()) {
            target.shape = snapshotAsset(normal);
            float scale = normal->getScaleX() * button->getScaleX();
            if (skinItem != button) scale *= skinItem->getScaleX();
            target.pointScale = std::max(0.01f, std::abs(scale));
        }
    }
    // the next scan puts the style back on the button behind the popup.
    runtime.requestScan();

    if (auto* popup = ButtonActionPopup::create(std::move(target), std::move(skinId),
                                                std::move(quickHubIdentity))) {
        popup->show();
    }
}

} // namespace paimon::buttonskin
