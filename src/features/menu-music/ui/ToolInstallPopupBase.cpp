#include "../../../utils/FilesystemPath.hpp"
#include "ToolInstallPopupBase.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../services/FfmpegBootstrap.hpp"
#include "../services/YtDlpBootstrap.hpp"

#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::menumusic {

bool ToolInstallPopupBase::initBase(ToolInstallPopupConfig const& config,
                                    std::filesystem::path const& destPath,
                                    std::function<void(bool)> onFinished) {
    if (!PaimonPopup::init(config.popupW, config.popupH)) return false;
    paimon::markDynamicPopup(this);

    m_onFinished = std::move(onFinished);
    m_successMessage = config.successMessage ? config.successMessage : "";
    this->setTitle(config.title);
    this->addInfoButton(config.infoTitle, config.infoBody);

    auto content = m_mainLayer->getContentSize();
    const float cx = content.width / 2.f;

    m_infoLabel = paimon::ui::makeText(
        config.headline,
        content.width - 48.f, 0.5f, paimon::ui::palette::muted,
        kCCTextAlignmentCenter);
    if (m_infoLabel) {
        m_infoLabel->setPosition({cx, content.height - 46.f});
        m_mainLayer->addChild(m_infoLabel, 3);
    }

    const float barW = config.barW;
    const float barH = 20.f;
    const float barY = content.height / 2.f - 2.f;

    auto panel = paimon::ui::makePanel({barW + 24.f, 92.f}, "Progress");
    panel->setPosition({cx - (barW + 24.f) / 2.f, barY - 32.f});
    m_mainLayer->addChild(panel, 2);

    m_statusBounds = CCSize{barW, 24.f};
    m_statusLabel = paimon::ui::makeText(
        "Preparing...", m_statusBounds.width, 0.5f,
        paimon::ui::palette::gold, kCCTextAlignmentCenter);
    if (m_statusLabel) {
        m_statusLabel->setLineBreakWithoutSpace(true);
        m_statusLabel->setAnchorPoint({0.5f, 1.f});
        m_statusLabel->setPosition({(barW + 24.f) / 2.f, 92.f - paimon::ui::kPanelHeader - 6.f});
        panel->addChild(m_statusLabel, 3);
        setStatusMessage("Preparing...");
    }

    auto barBg = paimon::ui::makeInset({barW, barH}, 220);
    barBg->setPosition({12.f, 18.f});
    panel->addChild(barBg, 1);
    m_barBg = barBg;

    auto barFill = CCLayerColor::create(config.fillColor);
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

    std::string displayPath = paimon::paths::toUtf8(destPath);
    if (displayPath.size() > config.pathLimit) {
        displayPath = "..." + displayPath.substr(displayPath.size() - (config.pathLimit - 3));
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

    return true;
}

void ToolInstallPopupBase::onExit() {
    *m_alive = false;
    if (!m_finished && m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(false);
    }
    Popup::onExit();
}

void ToolInstallPopupBase::updateProgress(float pct, const std::string& message) {
    if (m_finished) return;
    float clamped = std::clamp(pct, 0.f, 1.f);
    if (m_barBg && m_barFill) {
        float barW = m_barBg->getContentSize().width;
        m_barFill->setContentSize({barW * clamped, m_barFill->getContentSize().height});
    }
    if (m_percentLabel) {
        m_percentLabel->setString(fmt::format("{:.1f}%", clamped * 100.f).c_str());
    }
    if (!message.empty()) setStatusMessage(message);
}

void ToolInstallPopupBase::setStatusMessage(std::string const& message) {
    if (!m_statusLabel) return;
    m_statusLabel->setString(message.c_str());
    auto const size = m_statusLabel->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return;
    m_statusLabel->setScale(std::min({0.5f,
        m_statusBounds.width / size.width, m_statusBounds.height / size.height}));
}

void ToolInstallPopupBase::finishSuccess() {
    if (m_finished) return;
    m_finished = true;

    if (m_barBg && m_barFill) {
        m_barFill->setContentSize(m_barBg->getContentSize());
    }
    if (m_percentLabel) m_percentLabel->setString("100%");
    setStatusMessage(m_successMessage);
    if (m_statusLabel) {
        m_statusLabel->setColor(paimon::ui::palette::success);
    }

    if (m_onFinished) {
        auto cb = std::move(m_onFinished);
        m_onFinished = nullptr;
        cb(true);
    }

    this->runAction(CCSequence::create(
        CCDelayTime::create(0.35f),
        CCCallFunc::create(this, callfunc_selector(ToolInstallPopupBase::removeFromParent)),
        nullptr
    ));
}

void ToolInstallPopupBase::finishError(const std::string& error) {
    if (m_finished) return;
    m_finished = true;

    setStatusMessage(error);
    if (m_statusLabel) {
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

void ToolInstallPopupBase::onDismiss(CCObject*) {
    // onexit fires m_onfinished(false) unless install succeeded.
    Popup::onClose(nullptr);
}

template <typename Boot>
void ToolInstallPopupBase::runToolInstall(Boot& boot) {
    if (boot.exists()) {
        this->finishSuccess();
        return;
    }

    boot.ensureInstalled(
        [this, alive = m_alive](auto p) {
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

template void ToolInstallPopupBase::runToolInstall<FfmpegBootstrap>(FfmpegBootstrap&);
template void ToolInstallPopupBase::runToolInstall<YtDlpBootstrap>(YtDlpBootstrap&);

} // namespace paimon::menumusic
