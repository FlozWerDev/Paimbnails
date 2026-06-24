#include "YtDlpInstallPopup.hpp"

#include "../services/YtDlpBootstrap.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menumusic {

static constexpr float kPopupW = 400.f;
static constexpr float kPopupH = 220.f;

YtDlpInstallPopup* YtDlpInstallPopup::create(std::function<void(bool)> onFinished) {
    auto ret = new YtDlpInstallPopup();
    if (ret && ret->init(std::move(onFinished))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool YtDlpInstallPopup::init(std::function<void(bool)> onFinished) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    m_onFinished = std::move(onFinished);
    this->setTitle("Installing yt-dlp");

    auto content = m_mainLayer->getContentSize();
    const float cx = content.width / 2.f;

    // Info
    m_infoLabel = CCLabelBMFont::create(
        "Downloading the audio downloader (one-time, ~17 MB)",
        "chatFont.fnt");
    if (m_infoLabel) {
        m_infoLabel->setScale(0.5f);
        m_infoLabel->setColor({220, 220, 220});
        m_infoLabel->setPosition({cx, content.height - 44.f});
        m_mainLayer->addChild(m_infoLabel, 3);
    }

    // Status (bytes)
    m_statusLabel = CCLabelBMFont::create("Preparing...", "bigFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.42f);
        m_statusLabel->setColor({255, 235, 150});
        m_statusLabel->setPosition({cx, content.height - 70.f});
        m_mainLayer->addChild(m_statusLabel, 3);
    }

    // Barra de progreso
    const float barW = 320.f;
    const float barH = 18.f;
    const float barY = content.height / 2.f - 4.f;

    auto barBg = CCLayerColor::create(ccc4(18, 18, 28, 230));
    barBg->setContentSize({barW, barH});
    barBg->setPosition({cx - barW / 2.f, barY});
    m_mainLayer->addChild(barBg, 3);
    m_barBg = barBg;

    auto barFill = CCLayerColor::create(ccc4(120, 220, 255, 255));
    barFill->setContentSize({0.f, barH});
    barFill->setPosition({0.f, 0.f});
    barBg->addChild(barFill);
    m_barFill = barFill;

    // Borde visual encima de la barra para hacerla destacar.
    auto border = CCLayerColor::create(ccc4(255, 255, 255, 60));
    border->setContentSize({barW, 1.f});
    border->setPosition({cx - barW / 2.f, barY});
    m_mainLayer->addChild(border, 4);
    auto borderTop = CCLayerColor::create(ccc4(255, 255, 255, 60));
    borderTop->setContentSize({barW, 1.f});
    borderTop->setPosition({cx - barW / 2.f, barY + barH - 1.f});
    m_mainLayer->addChild(borderTop, 4);

    m_percentLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    if (m_percentLabel) {
        m_percentLabel->setScale(0.42f);
        m_percentLabel->setPosition({cx, barY - 18.f});
        m_mainLayer->addChild(m_percentLabel, 3);
    }

    // Path donde va a quedar
    auto destPath = YtDlpBootstrap::get().bundledPath();
    auto destStr = geode::utils::string::pathToString(destPath);
    // Acortar visualmente si es muy largo
    std::string displayPath = destStr;
    if (displayPath.size() > 60) {
        displayPath = "..." + displayPath.substr(displayPath.size() - 57);
    }
    m_pathLabel = CCLabelBMFont::create(
        fmt::format("Destination: {}", displayPath).c_str(),
        "chatFont.fnt");
    if (m_pathLabel) {
        m_pathLabel->setScale(0.32f);
        m_pathLabel->setColor({170, 170, 180});
        m_pathLabel->setPosition({cx, 52.f});
        m_mainLayer->addChild(m_pathLabel, 3);
    }

    // Boton dismiss (oculto mientras descarga)
    auto dismissSpr = ButtonSprite::create("Close", 80, true, "bigFont.fnt", "GJ_button_06.png", 24.f, 0.6f);
    if (dismissSpr) {
        m_dismissBtn = CCMenuItemSpriteExtra::create(
            dismissSpr, this, menu_selector(YtDlpInstallPopup::onDismiss));
        if (m_dismissBtn) {
            auto menu = CCMenu::create();
            menu->setPosition({cx, 24.f});
            menu->addChild(m_dismissBtn);
            m_dismissBtn->setVisible(false);
            m_mainLayer->addChild(menu, 5);
        }
    }

    // Arrancar la descarga inmediatamente tras construir el popup.
    this->startInstall();
    return true;
}

void YtDlpInstallPopup::onExit() {
    *m_alive = false;
    // Si el popup se cierra antes de terminar, notificar al caller.
    if (!m_finished && m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(false);
    }
    Popup::onExit();
}

void YtDlpInstallPopup::startInstall() {
    auto& boot = YtDlpBootstrap::get();

    // Fast path: ya instalado (el usuario podria haber copiado el binario).
    if (boot.exists()) {
        this->finishSuccess();
        return;
    }

    boot.ensureInstalled(
        [this, alive = m_alive](BootstrapProgress p) {
            if (!alive->load()) return;
            geode::Loader::get()->queueInMainThread([this, alive, p]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!alive->load()) return;
                this->updateProgress(p.percent, p.message);
            });
        },
        [this, alive = m_alive](bool ok, std::string msg) {
            if (!alive->load()) return;
            geode::Loader::get()->queueInMainThread([this, alive, ok, msg]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (!alive->load()) return;
                if (ok) this->finishSuccess();
                else this->finishError(msg);
            });
        }
    );
}

void YtDlpInstallPopup::updateProgress(float pct, const std::string& message) {
    if (m_finished) return;
    float clamped = std::clamp(pct, 0.f, 1.f);
    if (m_barBg && m_barFill) {
        float barW = m_barBg->getContentSize().width;
        m_barFill->setContentSize({barW * clamped, m_barFill->getContentSize().height});
    }
    if (m_percentLabel) {
        m_percentLabel->setString(fmt::format("{:.1f}%", clamped * 100.f).c_str());
    }
    if (m_statusLabel && !message.empty()) {
        m_statusLabel->setString(message.substr(0, 80).c_str());
    }
}

void YtDlpInstallPopup::finishSuccess() {
    if (m_finished) return;
    m_finished = true;
    m_success = true;

    if (m_barBg && m_barFill) {
        m_barFill->setContentSize(m_barBg->getContentSize());
    }
    if (m_percentLabel) m_percentLabel->setString("100%");
    if (m_statusLabel) {
        m_statusLabel->setString("yt-dlp installed — starting download...");
        m_statusLabel->setColor({150, 255, 150});
    }

    // Notificar al caller y cerrar.
    if (m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(true);
    }

    // Cerrar con un pequenio delay para que el usuario vea el "complete".
    this->runAction(CCSequence::create(
        CCDelayTime::create(0.35f),
        CCCallFunc::create(this, callfunc_selector(YtDlpInstallPopup::removeFromParent)),
        nullptr
    ));
}

void YtDlpInstallPopup::finishError(const std::string& error) {
    if (m_finished) return;
    m_finished = true;
    m_success = false;

    if (m_statusLabel) {
        // Truncar mensaje largo.
        std::string e = error;
        if (e.size() > 140) e = e.substr(0, 137) + "...";
        m_statusLabel->setString(e.c_str());
        m_statusLabel->setColor({255, 120, 120});
    }
    if (m_percentLabel) m_percentLabel->setString("Failed");

    if (m_dismissBtn) m_dismissBtn->setVisible(true);
}

void YtDlpInstallPopup::onDismiss(CCObject*) {
    // onExit se encarga de invocar m_onFinished(false) si no hubo exito.
    Popup::onClose(nullptr);
}

} // namespace paimon::menumusic
