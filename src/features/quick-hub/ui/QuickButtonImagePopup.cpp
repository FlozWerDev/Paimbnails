#include "QuickButtonImagePopup.hpp"

#include "QuickButtonEditKit.hpp"
#include "RadialVisuals.hpp"
#include "../services/QuickButtonSfx.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/utils/string.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

using namespace geode::prelude;

namespace paimon::quickhub {

QuickButtonImagePopup* QuickButtonImagePopup::create(
    CustomQuickButton* target, std::function<void()> onChanged) {
    if (!target) return nullptr;
    auto* ret = new QuickButtonImagePopup();
    ret->m_target = target;
    ret->m_onChanged = std::move(onChanged);
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void QuickButtonImagePopup::changed() {
    if (m_onChanged) m_onChanged();
    refresh();
}

bool QuickButtonImagePopup::init() {
    if (!Popup::init(360.f, 272.f)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Imagen del boton");

    auto size = m_mainLayer->getContentSize();

    m_thumb = CCNode::create();
    m_thumb->setPosition({size.width * 0.5f, 208.f});
    m_mainLayer->addChild(m_thumb, 2);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize(size);
    m_mainLayer->addChild(m_menu, 3);

    auto addBtn = [&](char const* label, bool selected, CCPoint pos, auto cb) {
        auto* item = makeMiniButton(label, selected, std::move(cb));
        item->setPosition(pos);
        m_menu->addChild(item);
        return item;
    };

    float cx = size.width * 0.5f;
    addBtn("Elegir PNG/JPG", false, {cx - 78.f, 168.f}, [this] { onChooseFile(); });
    addBtn("Frame del juego", false, {cx + 78.f, 168.f}, [this] {
        m_target->imagePath.clear();
        changed();
    });

    auto addStepper = [&](char const* minus, char const* plus, float y,
                          std::function<void(int)> step) {
        addBtn(minus, false, {cx - 90.f, y}, [step] { step(-1); });
        addBtn(plus, false, {cx + 90.f, y}, [step] { step(1); });
    };
    addStepper("-", "+", 138.f, [this](int d) {
        m_target->imageScale = std::clamp(m_target->imageScale + d * 0.1f, 0.2f, 3.f);
        changed();
    });
    addStepper("-15", "+15", 112.f, [this](int d) {
        float r = m_target->imageRotation + d * 15.f;
        while (r > 180.f) r -= 360.f;
        while (r < -180.f) r += 360.f;
        m_target->imageRotation = r;
        changed();
    });

    addBtn("FlipX", m_target->imageFlipX, {cx - 110.f, 86.f}, [this] {
        m_target->imageFlipX = !m_target->imageFlipX;
        changed();
    });
    addBtn("FlipY", m_target->imageFlipY, {cx - 30.f, 86.f}, [this] {
        m_target->imageFlipY = !m_target->imageFlipY;
        changed();
    });
    addBtn("Cero", false, {cx + 48.f, 86.f}, [this] {
        m_target->imageRotation = 0.f;
        m_target->imageScale = 1.f;
        changed();
    });
    addBtn("Quitar", false, {cx + 118.f, 86.f}, [this] {
        m_target->imagePath.clear();
        changed();
    });

    m_scaleValue = CCLabelBMFont::create("", "chatFont.fnt");
    m_scaleValue->setScale(0.5f);
    m_scaleValue->setPosition({cx, 138.f});
    m_mainLayer->addChild(m_scaleValue, 2);

    m_rotValue = CCLabelBMFont::create("", "chatFont.fnt");
    m_rotValue->setScale(0.5f);
    m_rotValue->setPosition({cx, 112.f});
    m_mainLayer->addChild(m_rotValue, 2);

    auto* cap1 = CCLabelBMFont::create("Tamano", "goldFont.fnt");
    cap1->setScale(0.32f);
    cap1->setPosition({cx - 150.f, 138.f});
    cap1->setAnchorPoint({0.f, 0.5f});
    m_mainLayer->addChild(cap1, 2);

    auto* cap2 = CCLabelBMFont::create("Giro", "goldFont.fnt");
    cap2->setScale(0.32f);
    cap2->setPosition({cx - 150.f, 112.f});
    cap2->setAnchorPoint({0.f, 0.5f});
    m_mainLayer->addChild(cap2, 2);

    auto* done = ButtonSprite::create("Listo", "goldFont.fnt", "GJ_button_01.png", .8f);
    done->setScale(0.6f);
    auto* doneBtn = CCMenuItemExt::createSpriteExtra(done, [this](CCMenuItemSpriteExtra*) {
        this->keyBackClicked();
    });
    m_buttonMenu->addChildAtPosition(doneBtn, Anchor::Bottom, ccp(0.f, 22.f));

    refresh();
    return true;
}

void QuickButtonImagePopup::refresh() {
    if (m_thumb) {
        m_thumb->removeAllChildren();
        auto badge = makeRadialBadge(toRadialDef(*m_target), m_target->shape, 52.f);
        if (badge.ring) badge.ring->setVisible(true);
        m_thumb->addChild(badge.root);
    }
    if (m_scaleValue) {
        m_scaleValue->setString(
            fmt::format("{}%", static_cast<int>(std::round(m_target->imageScale * 100.f))).c_str());
    }
    if (m_rotValue) {
        m_rotValue->setString(
            fmt::format("{} deg", static_cast<int>(std::round(m_target->imageRotation))).c_str());
    }
    // El resaltado de FlipX/FlipY se fija al abrir (ver init); el thumb y el
    // preview del padre ya muestran cada cambio en vivo.
}

void QuickButtonImagePopup::onChooseFile() {
    WeakRef<QuickButtonImagePopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto opt = std::move(result).unwrapOr(std::nullopt);
        if (!opt.has_value() || opt->empty()) return;
        auto* p = static_cast<QuickButtonImagePopup*>(popup.data());
        p->importImage(*opt);
    });
}

void QuickButtonImagePopup::importImage(std::filesystem::path const& src) {
    std::string stem = m_target->id.empty() ? m_target->name : m_target->id;
    std::string dest = importFileToConfigDir(
        src, quickHubImagesDir(), stem,
        [](std::filesystem::path const& srcPath, std::string const& destStr) {
            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(srcPath.extension()));
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg" && ext != ".webp") {
                PaimonNotify::create("Usa un PNG o JPG.", NotificationIcon::Warning)->show();
                return false;
            }
            std::error_code ec;
            auto fsize = std::filesystem::file_size(srcPath, ec);
            if (!ec && fsize > 8 * 1024 * 1024) {
                PaimonNotify::create("Imagen muy pesada (max 8 MB).", NotificationIcon::Warning)->show();
                return false;
            }
            // Validar que cocos la cargue de verdad.
            bool ok = false;
            if (auto* tex = cocos2d::CCTextureCache::sharedTextureCache()->addImage(destStr.c_str(), false)) {
                auto px = tex->getContentSizeInPixels();
                ok = px.width > 2.f || px.height > 2.f;
            }
            if (!ok) {
                PaimonNotify::create("Ese archivo no es una imagen valida.", NotificationIcon::Error)->show();
                return false;
            }
            return true;
        });
    if (dest.empty()) return;
    m_target->imagePath = std::move(dest);
    changed();
}

} // namespace paimon::quickhub
