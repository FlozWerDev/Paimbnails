#include "MyIconSetsPopup.hpp"

#include "CopyIconsPopup.hpp"
#include "IconSetNamePopup.hpp"
#include "IconSetRow.hpp"
#include "../IconPresetStore.hpp"
#include "../hooks/IconCopyGarageGlue.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr float kWidth = 420.f;
constexpr float kHeight = 280.f;
constexpr float kBodyWidth = 390.f;
constexpr float kBodyHeight = 190.f;
constexpr float kAddButton = 36.f;

// Naming happens next frame: the button that asked for it is still being
// handled by the touch dispatcher right now.
void askName(char const* title, std::string initial, IconSetNamePopup::Callback onConfirm) {
    Loader::get()->queueInMainThread(
        [title, initial = std::move(initial), onConfirm = std::move(onConfirm)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto* popup = IconSetNamePopup::create(title, std::move(initial), std::move(onConfirm));
            if (popup) popup->show();
        });
}

}  // anonymous namespace

MyIconSetsPopup* MyIconSetsPopup::create() {
    auto* popup = new MyIconSetsPopup();
    if (popup->init()) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool MyIconSetsPopup::init() {
    if (!Popup::init(kWidth, kHeight)) return false;

    this->setTitle("My Icon Sets");
    this->setID("my-icon-sets-popup"_spr);
    paimon::markDynamicPopup(this);

    auto const content = m_mainLayer->getContentSize();

    m_body = CCNode::create();
    m_body->setContentSize({kBodyWidth, kBodyHeight});
    m_body->setPosition({(content.width - kBodyWidth) / 2.f, 52.f});
    m_mainLayer->addChild(m_body);

    CCNode* addSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
    if (addSpr) {
        float const dim = std::max(addSpr->getContentWidth(), addSpr->getContentHeight());
        if (dim > 0.f) addSpr->setScale(kAddButton / dim);
    } else {
        addSpr = ButtonSprite::create("+", "bigFont.fnt", "GJ_button_01.png", 0.7f);
    }
    if (addSpr) {
        auto* btn = CCMenuItemSpriteExtra::create(
            addSpr, this, menu_selector(MyIconSetsPopup::onAdd));
        btn->setID("save-current-button"_spr);
        btn->setPosition({content.width / 2.f, 28.f});
        m_buttonMenu->addChild(btn);
    }

    rebuild();
    return true;
}

void MyIconSetsPopup::rebuild() {
    if (!m_body) return;
    m_body->removeAllChildren();

    auto const& saved = presets();

    if (saved.empty()) {
        auto* empty = CCLabelBMFont::create("Nothing saved yet", "bigFont.fnt");
        empty->setScale(0.55f);
        empty->setColor({200, 200, 210});
        empty->setPosition({kBodyWidth / 2.f, kBodyHeight / 2.f + 10.f});
        m_body->addChild(empty);

        auto* hint = CCLabelBMFont::create(
            "Press + to save the icons you are wearing", "chatFont.fnt");
        hint->setScale(0.45f);
        hint->setColor({150, 150, 170});
        hint->setPosition({kBodyWidth / 2.f, kBodyHeight / 2.f - 12.f});
        m_body->addChild(hint);
        return;
    }

    float const totalHeight = std::max(static_cast<float>(saved.size()) * kRowHeight, kBodyHeight);

    auto* container = CCLayer::create();
    container->setContentSize({kBodyWidth, totalHeight});

    for (std::size_t index = 0; index < saved.size(); ++index) {
        auto* row = makeRow(saved[index], index, kBodyWidth);
        if (!row) continue;
        row->setPosition({0.f, totalHeight - kRowHeight * (index + 1)});
        container->addChild(row);
    }

    auto* scroll = ScrollLayer::create({kBodyWidth, kBodyHeight});
    scroll->m_contentLayer->setContentSize({kBodyWidth, totalHeight});
    scroll->m_contentLayer->addChild(container);
    scroll->scrollToTop();
    m_body->addChild(scroll);
}

CCNode* MyIconSetsPopup::makeRow(IconPreset const& preset, std::size_t index, float width) {
    WeakRef<MyIconSetsPopup> self = this;

    std::vector<RowAction> actions{
        {makeRowTextFace("Use", "GJ_button_01.png"),
         [self, preset] { if (auto popup = self.lock()) popup->use(preset); }},
        {makeRowTextFace("Icons", "GJ_button_04.png"),
         [self, preset] { if (auto popup = self.lock()) popup->showIcons(preset); }},
        {makeRowIconFace("GJ_editBtn_001.png"),
         [self, preset] { if (auto popup = self.lock()) popup->rename(preset); }},
        {makeRowIconFace("GJ_trashBtn_001.png"),
         [self, preset] { if (auto popup = self.lock()) popup->erase(preset); }},
    };

    return makeSetRow(preset.set, formatSetDate(preset.savedAt), index, width, actions);
}

void MyIconSetsPopup::onAdd(CCObject*) {
    if (presets().size() >= kMaxPresets) {
        PaimonNotify::show(fmt::format("You can keep {} sets at most", kMaxPresets),
                           NotificationIcon::Warning);
        return;
    }

    auto const set = currentPlayerSet();
    WeakRef<MyIconSetsPopup> self = this;
    askName("Save Icon Set", suggestPresetName(), [self, set](std::string const& name) {
        if (addPreset(name, set) == 0) {
            PaimonNotify::show("Could not save the set", NotificationIcon::Error);
            return;
        }
        PaimonNotify::show(fmt::format("Saved {}", name), NotificationIcon::Success);
        if (auto popup = self.lock()) popup->rebuild();
    });
}

void MyIconSetsPopup::use(IconPreset const& preset) {
    apply(preset.set);
    garage::refreshVisibleGarage();
    PaimonNotify::show(fmt::format("Now using {}", preset.name()), NotificationIcon::Success);
    this->onClose(nullptr);
}

void MyIconSetsPopup::showIcons(IconPreset const& preset) {
    auto const set = preset.set;
    Loader::get()->queueInMainThread([set] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (auto* popup = CopyIconsPopup::create(set, true)) popup->show();
    });
}

void MyIconSetsPopup::rename(IconPreset const& preset) {
    WeakRef<MyIconSetsPopup> self = this;
    uint32_t const id = preset.id;
    askName("Rename Icon Set", preset.name(), [self, id](std::string const& name) {
        renamePreset(id, name);
        if (auto popup = self.lock()) popup->rebuild();
    });
}

void MyIconSetsPopup::erase(IconPreset const& preset) {
    WeakRef<MyIconSetsPopup> self = this;
    uint32_t const id = preset.id;
    geode::createQuickPopup(
        "Delete Set",
        fmt::format("Delete <cr>{}</c>?", preset.name()),
        "Cancel", "Delete",
        [self, id](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            removePreset(id);
            if (auto popup = self.lock()) popup->queueRebuild();
        });
}

// Rebuilding tears down the buttons we are being called from, so wait until the
// touch dispatcher is done with them.
void MyIconSetsPopup::queueRebuild() {
    WeakRef<MyIconSetsPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (auto popup = self.lock()) popup->rebuild();
    });
}

}  // namespace paimon::iconcopy
