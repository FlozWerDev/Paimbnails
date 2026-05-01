#include "UpdateProgressPopup.hpp"
#include "../services/UpdateChecker.hpp"
#include "../../../utils/Localization.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::updates {

namespace {
std::string tr(char const* key, char const* fallback) {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && *fallback) return fallback;
    return value;
}

std::string formatBytes(uint64_t b) {
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    if (b >= static_cast<uint64_t>(kMB)) return fmt::format("{:.2f} MB", b / kMB);
    if (b >= static_cast<uint64_t>(kKB)) return fmt::format("{:.1f} KB", b / kKB);
    return fmt::format("{} B", b);
}
}

UpdateProgressPopup* UpdateProgressPopup::create() {
    auto ret = new UpdateProgressPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool UpdateProgressPopup::init() {
    if (!Popup::init(360.f, 200.f)) return false;
    this->setTitle(tr("pai.update.title", "Downloading update"));

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto& checker = UpdateChecker::get();

    // ── Linea de versiones ────────────────────────────────────────────
    std::string verLine;
    if (!checker.remoteVersion().empty()) {
        verLine = fmt::format("{} -> {}", checker.localVersion(), checker.remoteVersion());
    } else {
        verLine = checker.localVersion();
    }
    auto versionsLbl = CCLabelBMFont::create(verLine.c_str(), "bigFont.fnt");
    versionsLbl->setScale(0.45f);
    versionsLbl->setPosition({cx, content.height - 40.f});
    versionsLbl->setColor({200, 200, 200});
    m_mainLayer->addChild(versionsLbl);

    // ── Status label ──────────────────────────────────────────────────
    m_statusLabel = CCLabelBMFont::create(
        tr("pai.update.starting", "Starting...").c_str(),
        "bigFont.fnt"
    );
    m_statusLabel->setScale(0.4f);
    m_statusLabel->setPosition({cx, content.height - 65.f});
    m_mainLayer->addChild(m_statusLabel);

    // ── Barra de progreso ─────────────────────────────────────────────
    float barW = 280.f;
    float barH = 16.f;
    float barY = content.height / 2.f - 5.f;

    auto barBg = CCLayerColor::create(ccc4(20, 20, 30, 230));
    barBg->setContentSize({barW, barH});
    barBg->setPosition({cx - barW / 2.f, barY});
    m_mainLayer->addChild(barBg);
    m_barBg = barBg;

    auto barFill = CCLayerColor::create(ccc4(80, 200, 255, 255));
    barFill->setContentSize({0.f, barH});
    barFill->setPosition({0.f, 0.f});
    barBg->addChild(barFill);
    m_barFill = barFill;

    m_percentLabel = CCLabelBMFont::create("0%", "bigFont.fnt");
    m_percentLabel->setScale(0.4f);
    m_percentLabel->setPosition({cx, barY - 18.f});
    m_mainLayer->addChild(m_percentLabel);

    // ── Botones ──────────────────────────────────────────────────────
    m_actionMenu = CCMenu::create();
    m_actionMenu->setPosition({cx, 30.f});
    m_actionMenu->setContentSize({content.width, 40.f});
    m_mainLayer->addChild(m_actionMenu);

    auto cancelSpr = ButtonSprite::create(
        tr("pai.update.cancel", "Cancel").c_str(),
        "bigFont.fnt", "GJ_button_06.png", .8f
    );
    cancelSpr->setScale(0.6f);
    m_cancelBtn = CCMenuItemSpriteExtra::create(
        cancelSpr, this, menu_selector(UpdateProgressPopup::onCancel)
    );
    m_actionMenu->addChild(m_cancelBtn);

    auto restartSpr = ButtonSprite::create(
        tr("pai.update.restart", "Restart").c_str(),
        "goldFont.fnt", "GJ_button_01.png", .8f
    );
    restartSpr->setScale(0.6f);
    m_restartBtn = CCMenuItemSpriteExtra::create(
        restartSpr, this, menu_selector(UpdateProgressPopup::onRestart)
    );
    m_restartBtn->setVisible(false);
    m_actionMenu->addChild(m_restartBtn);

    m_actionMenu->setLayout(
        RowLayout::create()
            ->setGap(20.f)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    m_actionMenu->updateLayout();

    this->setKeyboardEnabled(true);

    // Lanza la descarga al construir
    this->startDownload();
    return true;
}

void UpdateProgressPopup::startDownload() {
    auto& checker = UpdateChecker::get();
    if (checker.downloadUrl().empty()) {
        this->onDone(false, "no download url");
        return;
    }

    Ref<UpdateProgressPopup> self = this;

    checker.downloadUpdate(
        [self](uint64_t received, uint64_t total) {
            if (auto p = self.data()) p->onProgress(received, total);
        },
        [self](bool ok, std::string msg) {
            // queueInMainThread por seguridad: el callback de dispatchOwned
            // ya corre en main thread via async::spawn, pero re-encolamos por
            // si llega una respuesta sincronica antes de terminar el init.
            Loader::get()->queueInMainThread([self, ok, msg]() {
                if (auto p = self.data()) p->onDone(ok, msg);
            });
        }
    );
}

void UpdateProgressPopup::onProgress(uint64_t received, uint64_t total) {
    if (m_finished) return;

    float ratio = (total > 0) ? static_cast<float>(received) / static_cast<float>(total) : 0.f;
    if (ratio < 0.f) ratio = 0.f;
    if (ratio > 1.f) ratio = 1.f;

    if (m_barBg && m_barFill) {
        float barW = m_barBg->getContentSize().width;
        m_barFill->setContentSize({barW * ratio, m_barFill->getContentSize().height});
    }

    if (m_percentLabel) {
        m_percentLabel->setString(fmt::format("{:.1f}%", ratio * 100.f).c_str());
    }

    if (m_statusLabel) {
        std::string s;
        if (total > 0) {
            s = fmt::format("{} / {}", formatBytes(received), formatBytes(total));
        } else {
            s = fmt::format("{}", formatBytes(received));
        }
        m_statusLabel->setString(s.c_str());
    }
}

void UpdateProgressPopup::onDone(bool ok, std::string const& msgOrPath) {
    m_finished = true;
    m_succeeded = ok;

    if (ok) {
        if (m_statusLabel) {
            m_statusLabel->setString(tr("pai.update.done", "Update installed. Restart to apply.").c_str());
            m_statusLabel->setColor({120, 255, 120});
        }
        if (m_barFill && m_barBg) {
            m_barFill->setContentSize(m_barBg->getContentSize());
        }
        if (m_percentLabel) m_percentLabel->setString("100%");
        if (m_cancelBtn) m_cancelBtn->setVisible(false);
        if (m_restartBtn) m_restartBtn->setVisible(true);
    } else {
        if (m_statusLabel) {
            m_statusLabel->setString(
                fmt::format("{}: {}", tr("pai.update.failed", "Failed"), msgOrPath).c_str()
            );
            m_statusLabel->setColor({255, 110, 110});
        }
        if (m_cancelBtn) {
            // re-etiquetamos cancelar como "cerrar"
            if (auto spr = typeinfo_cast<ButtonSprite*>(m_cancelBtn->getNormalImage())) {
                spr->setString(tr("pai.update.close", "Close").c_str());
            }
        }
    }

    if (m_actionMenu) m_actionMenu->updateLayout();
}

void UpdateProgressPopup::onCancel(CCObject*) {
    if (!m_finished) {
        UpdateChecker::get().cancelDownload();
    }
    this->onClose(nullptr);
}

void UpdateProgressPopup::onRestart(CCObject*) {
    log::info("[UpdateChecker] User requested restart");
    geode::utils::game::restart(true);
}

void UpdateProgressPopup::onClose(CCObject* sender) {
    Popup::onClose(sender);
}

} // namespace paimon::updates
