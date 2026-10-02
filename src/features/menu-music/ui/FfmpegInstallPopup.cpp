#include "FfmpegInstallPopup.hpp"

#include "../services/FfmpegBootstrap.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menumusic {

static constexpr float kPopupW = 420.f;
static constexpr float kPopupH = 230.f;

FfmpegInstallPopup* FfmpegInstallPopup::create(std::function<void(bool)> onFinished) {
    auto ret = new FfmpegInstallPopup();
    if (ret && ret->init(std::move(onFinished))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool FfmpegInstallPopup::init(std::function<void(bool)> onFinished) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    m_onFinished = std::move(onFinished);
    this->setTitle("Installing ffmpeg");
    this->addInfoButton("ffmpeg",
        "ffmpeg is the <cy>audio converter</c> Paimbnails uses to decode and re-encode "
        "songs. It downloads <cg>once</c> (~80 MB) and is kept bundled with the mod.");

    auto content = m_mainLayer->getContentSize();
    const float cx = content.width / 2.f;

    m_infoLabel = paimon::ui::makeText(
        "Downloading audio converter (one-time, ~80 MB)",
        content.width - 48.f, 0.5f, paimon::ui::palette::muted,
        kCCTextAlignmentCenter);
    if (m_infoLabel) {
        m_infoLabel->setPosition({cx, content.height - 46.f});
        m_mainLayer->addChild(m_infoLabel, 3);
    }

    const float barW = 340.f;
    const float barH = 20.f;
    const float barY = content.height / 2.f - 2.f;

    auto panel = paimon::ui::makePanel({barW + 24.f, 92.f}, "Progress");
    panel->setPosition({cx - (barW + 24.f) / 2.f, barY - 32.f});
    m_mainLayer->addChild(panel, 2);

    m_statusLabel = CCLabelBMFont::create("Preparing...", "bigFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setScale(0.42f);
        m_statusLabel->setColor(paimon::ui::palette::gold);
        m_statusLabel->setPosition({(barW + 24.f) / 2.f, 92.f - paimon::ui::kPanelHeader - 8.f});
        panel->addChild(m_statusLabel, 3);
    }

    auto barBg = paimon::ui::makeInset({barW, barH}, 220);
    barBg->setPosition({12.f, 18.f});
    panel->addChild(barBg, 1);
    m_barBg = barBg;

    auto barFill = CCLayerColor::create(ccc4(180, 140, 255, 255));
    barFill->setContentSize({0.f, barH});
    barFill->setPosition({0.f, 0.f});
    barBg->addChild(barFill);
    m_barFill = barFill;

    m_percentLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    if (m_percentLabel) {
        m_percentLabel->setScale(0.4f);
        m_percentLabel->setPosition({(barW + 24.f) / 2.f, 28.f});
        panel->addChild(m_percentLabel, 3);
    }

    auto destPath = FfmpegBootstrap::get().bundledPath();
    auto destStr = geode::utils::string::pathToString(destPath);
    std::string displayPath = destStr;
    if (displayPath.size() > 62) {
        displayPath = "..." + displayPath.substr(displayPath.size() - 59);
    }
    m_pathLabel = paimon::ui::makeText(
        fmt::format("Destination: {}", displayPath).c_str(),
        content.width - 48.f, 0.34f, paimon::ui::palette::dim,
        kCCTextAlignmentCenter);
    if (m_pathLabel) {
        m_pathLabel->setPosition({cx, 50.f});
        m_mainLayer->addChild(m_pathLabel, 3);
    }

    m_dismissBtn = paimon::ui::makeButton("Close",
        [this] { this->onDismiss(nullptr); },
        paimon::ui::Btn::Gray, 90.f, 0.6f);
    if (m_dismissBtn) {
        auto menu = CCMenu::create();
        menu->setPosition({cx, 22.f});
        menu->addChild(m_dismissBtn);
        m_dismissBtn->setVisible(false);
        m_mainLayer->addChild(menu, 5);
    }

    this->startInstall();
    return true;
}

void FfmpegInstallPopup::onExit() {
    *m_alive = false;
    if (!m_finished && m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(false);
    }
    Popup::onExit();
}

void FfmpegInstallPopup::startInstall() {
    auto& boot = FfmpegBootstrap::get();

    if (boot.exists()) {
        this->finishSuccess();
        return;
    }

    boot.ensureInstalled(
        [this, alive = m_alive](FfmpegBootstrapProgress p) {
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

void FfmpegInstallPopup::updateProgress(float pct, const std::string& message) {
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

void FfmpegInstallPopup::finishSuccess() {
    if (m_finished) return;
    m_finished = true;

    if (m_barBg && m_barFill) {
        m_barFill->setContentSize(m_barBg->getContentSize());
    }
    if (m_percentLabel) m_percentLabel->setString("100%");
    if (m_statusLabel) {
        m_statusLabel->setString("ffmpeg installed - resuming download...");
        m_statusLabel->setColor(paimon::ui::palette::success);
    }

    if (m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(true);
    }

    this->runAction(CCSequence::create(
        CCDelayTime::create(0.35f),
        CCCallFunc::create(this, callfunc_selector(FfmpegInstallPopup::removeFromParent)),
        nullptr
    ));
}

void FfmpegInstallPopup::finishError(const std::string& error) {
    if (m_finished) return;
    m_finished = true;

    if (m_statusLabel) {
        std::string e = error;
        if (e.size() > 140) e = e.substr(0, 137) + "...";
        m_statusLabel->setString(e.c_str());
        m_statusLabel->setColor(paimon::ui::palette::danger);
    }
    if (m_percentLabel) m_percentLabel->setString("Failed");

    if (m_dismissBtn) m_dismissBtn->setVisible(true);

    if (m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(false);
    }
}

void FfmpegInstallPopup::onDismiss(CCObject*) {
    Popup::onClose(nullptr);
}

} // namespace paimon::menumusic
