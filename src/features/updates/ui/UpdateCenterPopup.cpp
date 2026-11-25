#include "UpdateCenterPopup.hpp"
#include "UpdateProgressPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../services/UpdateChecker.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/MDPopup.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/ui/Scrollbar.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::updates {

namespace {

// the popup is kWidth x kHeight. Cards share one left and one right edge, text starts at
// kTextX and actions end at kRight.
constexpr float kWidth  = 420.f;
constexpr float kHeight = 300.f;
constexpr float kLeft   = 12.f;
constexpr float kCardW  = 396.f;
constexpr float kTextX  = 22.f;
constexpr float kRight  = 398.f;

// status card: installed version, status line and the primary action.
constexpr float kHeroY       = 206.f;
constexpr float kHeroH       = 48.f;
constexpr float kPrimaryW    = 100.f;
constexpr float kStatusScale = 0.3f;
constexpr float kStatusMaxW  = 260.f;

// history panel: a header band (paimon::ui::kPanelHeader) above the scrollable list.
constexpr float kPanelY      = 44.f;
constexpr float kPanelH      = 156.f;
constexpr float kListX       = kLeft + 6.f;
constexpr float kListY       = kPanelY + 6.f;
constexpr float kListW       = kCardW - 12.f;
constexpr float kListH       = kPanelH - paimon::ui::kPanelHeader - 12.f;
constexpr float kRowH        = 30.f;
constexpr float kRowW        = kListW - 14.f; // leaves room for the scrollbar
constexpr float kActionW     = 64.f;
constexpr float kActionScale = 0.5f;
// LATEST badge: GD's cyan button with the gold font, about as tall as the version text.
constexpr float kLatestW     = 44.f;
constexpr float kLatestScale = 0.44f;

// footer: auto update on the left, secondary actions on the right.
constexpr float kFooterY        = 22.f;
constexpr float kFooterBtnW     = 80.f;
constexpr float kFooterBtnScale = 0.5f;
// geode's GitHub icon, drawn as tall as the footer buttons (15 pt).
constexpr char const* kGithubFrame = "geode.loader/github.png";
constexpr float kGithubHeight   = 15.f;

using paimon::loc::tr;

// UpdateChecker stores the bare version ("1.2.5"), so the "v" prefix is added here.
std::string versionText() {
    return fmt::format("v{}", UpdateChecker::get().localVersion());
}

std::string formatSize(uint64_t bytes) {
    if (bytes == 0) return "";
    return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
}

CCNode* makeBadge(std::string const& text, ccColor3B color) {
    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(0.2f);

    float w = label->getScaledContentSize().width + 10.f;
    auto chip = paimon::SpriteHelper::createColorPanel(w, 13.f, color, 210, 3.f);
    label->setPosition({w / 2.f, 6.5f});
    chip->addChild(label);
    return chip;
}

} // namespace

UpdateCenterPopup* UpdateCenterPopup::create() {
    auto ret = new UpdateCenterPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool UpdateCenterPopup::init() {
    if (!PaimonPopup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle(tr("pai.updates.center.title", "Updates"));
    this->addInfoButton(tr("pai.updates.center.title", "Updates"),
        "Check for and install Paimbnails updates. The top card shows your <cy>current version</c> and "
        "status; the list is the full <cy>version history</c> (tap the info icon for release notes). "
        "<cg>Install</c> is green, <cr>Revert</c> to an older build is red. Toggle <co>Betas</c> to "
        "include prereleases and <co>Auto update</c> to update on launch. <cy>Refresh</c> downloads "
        "and reinstalls the latest release, even if its version number has not changed.");

    m_headerMenu = CCMenu::create();
    m_headerMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_headerMenu, 3);

    this->buildHeader();
    this->buildHistoryPanel();
    this->buildFooter();
    this->refreshHeader();

    this->loadHistory();
    this->schedule(schedule_selector(UpdateCenterPopup::pollState), 0.4f);
    return true;
}

void UpdateCenterPopup::buildHeader() {
    auto card = paimon::ui::makeInset({kCardW, kHeroH}, 120);
    card->setPosition({kLeft, kHeroY});
    m_mainLayer->addChild(card, 1);

    m_versionLabel = CCLabelBMFont::create(versionText().c_str(), "goldFont.fnt");
    m_versionLabel->setScale(0.5f);
    m_versionLabel->setAnchorPoint({0.f, 0.5f});
    m_versionLabel->setPosition({kTextX, kHeroY + kHeroH - 16.f});
    m_mainLayer->addChild(m_versionLabel, 2);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setPosition({kTextX, kHeroY + 14.f});
    m_mainLayer->addChild(m_statusLabel, 2);
}

void UpdateCenterPopup::buildHistoryPanel() {
    auto* panel = paimon::ui::makePanel({kCardW, kPanelH},
        tr("pai.updates.history", "Version history").c_str(), 90);
    panel->setPosition({kLeft, kPanelY});
    m_mainLayer->addChild(panel, 1);

    // the Betas toggle sits in the panel header, on the right.
    float const headerY = kPanelH - paimon::ui::kPanelHeader / 2.f;
    auto* betaLabel = CCLabelBMFont::create(tr("pai.updates.betas", "Betas").c_str(), "bigFont.fnt");
    betaLabel->setScale(0.3f);
    betaLabel->setColor(paimon::ui::palette::muted);
    betaLabel->setAnchorPoint({1.f, 0.5f});
    betaLabel->setPosition({kCardW - 46.f, headerY});
    panel->addChild(betaLabel, 2);

    auto* betaMenu = paimon::ui::makeZeroMenu();
    panel->addChild(betaMenu, 3);
    auto* betaToggle = paimon::ui::makeToggle(m_showPrereleases,
        [self = WeakRef<UpdateCenterPopup>(this)](bool enabled) {
            auto popup = self.lock();
            if (!popup) return;
            popup->m_showPrereleases = enabled;
            popup->rebuildHistory();
        }, 0.4f);
    betaToggle->setPosition({kCardW - 18.f, headerY});
    betaMenu->addChild(betaToggle);

    m_listHolder = CCNode::create();
    m_mainLayer->addChild(m_listHolder, 2);
}

void UpdateCenterPopup::buildFooter() {
    auto* autoToggle = paimon::ui::makeToggle(paimon::settings::general::autoUpdate(), [](bool enabled) {
        Mod::get()->setSettingValue<bool>("auto-update", enabled);
        PaimonNotify::show(
            tr(enabled ? "pai.updates.auto.on" : "pai.updates.auto.off",
               enabled ? "Auto update on" : "Auto update off"),
            enabled ? NotificationIcon::Success : NotificationIcon::None,
            1.6f
        );
    }, 0.45f);
    autoToggle->setPosition({kTextX + 12.f, kFooterY});
    m_headerMenu->addChild(autoToggle);

    auto* autoLabel = CCLabelBMFont::create(tr("pai.updates.auto", "Auto update").c_str(), "bigFont.fnt");
    autoLabel->setScale(0.3f);
    autoLabel->setColor(paimon::ui::palette::muted);
    autoLabel->setAnchorPoint({0.f, 0.5f});
    autoLabel->setPosition({kTextX + 30.f, kFooterY});
    m_mainLayer->addChild(autoLabel, 2);

    // Geode packs this icon at 4x, so its frame is 31 x 30 pt, not the 124 x 120 px file. Measure
    // the frame so the icon stays kGithubHeight tall whichever sheet GD loads.
    float githubScale = kGithubHeight / 30.f;
    if (auto* probe = paimon::SpriteHelper::safeCreateWithFrameName(kGithubFrame)) {
        if (probe->getContentSize().height > 0.f) githubScale = kGithubHeight / probe->getContentSize().height;
    }
    auto* github = paimon::ui::makeFrameButton(kGithubFrame, githubScale, [] {
        web::openLinkInBrowser("https://github.com/FlozWerDev/Paimbnails/releases");
    });
    github->setID("github-button"_spr);
    float const githubW = github->getNormalImage()->getContentSize().width * githubScale;
    github->setPosition({kRight - githubW / 2.f, kFooterY});
    m_headerMenu->addChild(github);

    m_refreshBtn = paimon::ui::makeButton(tr("pai.updates.refresh", "Refresh").c_str(), [this] {
        this->onRefresh(nullptr);
    }, paimon::ui::Btn::Cyan, kFooterBtnW, kFooterBtnScale, "bigFont.fnt");
    m_refreshBtn->setID("reinstall-latest-btn"_spr);
    float const refreshX = kRight - githubW - 8.f - kFooterBtnW / 2.f;
    m_refreshBtn->setPosition({refreshX, kFooterY});
    m_headerMenu->addChild(m_refreshBtn);

    auto* changelogBtn = paimon::ui::makeButton(tr("pai.updates.changelog", "Changelog").c_str(), [this] {
        this->showLocalChangelog();
    }, paimon::ui::Btn::Cyan, kFooterBtnW, kFooterBtnScale, "bigFont.fnt");
    changelogBtn->setID("local-changelog-btn"_spr);
    changelogBtn->setPosition({refreshX - kFooterBtnW - 6.f, kFooterY});
    m_headerMenu->addChild(changelogBtn);
}

void UpdateCenterPopup::refreshHeader() {
    auto& checker = UpdateChecker::get();
    auto state = checker.state();
    bool pending = checker.hasPendingInstall();
    bool downloading = checker.isDownloading();

    m_lastState = state;
    m_lastPending = pending;
    m_lastDownloading = downloading;

    std::string status;
    ccColor3B statusColor = {170, 180, 200};
    std::string btnText = tr("pai.updates.check", "Check");
    auto btnSkin = paimon::ui::Btn::Cyan;

    if (m_refreshing) {
        status = tr("pai.update.checking", "Checking for updates...");
        statusColor = {120, 200, 255};
        btnText = tr("pai.updates.checking.btn", "Wait...");
        btnSkin = paimon::ui::Btn::Gray;
    } else if (downloading) {
        status = tr("pai.update.title", "Downloading update");
        statusColor = {120, 200, 255};
        btnText = tr("pai.updates.checking.btn", "Wait...");
        btnSkin = paimon::ui::Btn::Gray;
    } else if (pending) {
        auto version = checker.pendingVersion();
        if (version.empty()) version = checker.remoteVersion();
        status = fmt::format(
            fmt::runtime(tr("pai.updates.pending", "v{} ready. Restart to apply it.")), version
        );
        statusColor = {120, 255, 140};
        btnText = tr("pai.update.restart", "Restart");
        btnSkin = paimon::ui::Btn::Green;
    } else {
        switch (state) {
            case UpdateChecker::State::Checking:
                status = tr("pai.update.checking", "Checking for updates...");
                statusColor = {120, 200, 255};
                btnText = tr("pai.updates.checking.btn", "Wait...");
                btnSkin = paimon::ui::Btn::Gray;
                break;
            case UpdateChecker::State::UpdateAvailable:
                status = fmt::format(
                    fmt::runtime(tr("pai.updates.available", "Version {} available")),
                    checker.remoteVersion()
                );
                statusColor = {255, 220, 110};
                btnText = tr("pai.updates.update", "Update");
                btnSkin = paimon::ui::Btn::Green;
                break;
            case UpdateChecker::State::UpToDate:
                status = tr("pai.updates.uptodate", "You're up to date");
                statusColor = {130, 230, 150};
                break;
            case UpdateChecker::State::Failed:
                status = fmt::format(
                    fmt::runtime(tr("pai.updates.check_failed", "Check failed: {}")),
                    checker.lastError()
                );
                statusColor = {255, 130, 130};
                break;
            default:
                status = tr("pai.updates.idle", "Press Check to look for updates");
                break;
        }
    }

    if (m_versionLabel) {
        m_versionLabel->setString(versionText().c_str());
    }
    if (m_statusLabel) {
        m_statusLabel->setString(status.c_str());
        m_statusLabel->setColor(statusColor);
        m_statusLabel->setScale(kStatusScale);
        m_statusLabel->limitLabelWidth(kStatusMaxW, kStatusScale, 0.1f);
    }

    if (m_primaryBtn) m_primaryBtn->removeFromParent();
    m_primaryBtn = paimon::ui::makeButton(btnText.c_str(), [this] {
        this->onPrimary(nullptr);
    }, btnSkin, kPrimaryW, 0.55f, "bigFont.fnt");
    m_primaryBtn->setPosition({kRight - kPrimaryW / 2.f, kHeroY + kHeroH / 2.f});
    bool enabled = !m_refreshing && !downloading && state != UpdateChecker::State::Checking;
    m_primaryBtn->setEnabled(enabled);
    m_headerMenu->addChild(m_primaryBtn);

    if (m_refreshBtn) {
        m_refreshBtn->setEnabled(enabled);
        if (auto* sprite = typeinfo_cast<ButtonSprite*>(m_refreshBtn->getNormalImage())) {
            sprite->setOpacity(enabled ? 255 : 120);
        }
    }
}

void UpdateCenterPopup::onPrimary(CCObject*) {
    auto& checker = UpdateChecker::get();
    if (m_refreshing || checker.isDownloading()) return;

    if (checker.hasPendingInstall()) {
        checker.restartToApplyPendingUpdate();
        return;
    }

    switch (checker.state()) {
        case UpdateChecker::State::Checking:
            return;
        case UpdateChecker::State::UpdateAvailable: {
            ReleaseInfo latest;
            latest.version = checker.remoteVersion();
            latest.tag = checker.remoteTag();
            latest.downloadUrl = checker.downloadUrl();
            this->startInstall(latest);
            return;
        }
        default:
            checker.checkAsync(true);
            this->loadHistory();
            this->refreshHeader();
            return;
    }
}

void UpdateCenterPopup::onRefresh(CCObject*) {
    auto& checker = UpdateChecker::get();
    if (m_refreshing || checker.isDownloading() || checker.state() == UpdateChecker::State::Checking) return;

    m_refreshing = true;
    checker.checkAsync(true, [self = WeakRef<UpdateCenterPopup>(this)](bool ok, std::string error) {
        auto popup = self.lock();
        if (!popup || !popup->isRunning()) return;
        popup->m_refreshing = false;
        popup->refreshHeader();
        if (!ok) {
            PaimonNotify::show(
                fmt::format(fmt::runtime(tr("pai.updates.check_failed", "Check failed: {}")), error),
                NotificationIcon::Error
            );
            return;
        }

        auto& checker = UpdateChecker::get();
        ReleaseInfo latest;
        latest.version = checker.remoteVersion();
        latest.tag = checker.remoteTag();
        latest.downloadUrl = checker.downloadUrl();
        popup->startInstall(latest);
    });
    this->refreshHeader();
}

void UpdateCenterPopup::pollState(float) {
    auto& checker = UpdateChecker::get();
    if (checker.state() == m_lastState && checker.hasPendingInstall() == m_lastPending &&
        checker.isDownloading() == m_lastDownloading) return;
    this->refreshHeader();
}

void UpdateCenterPopup::loadHistory() {
    auto& checker = UpdateChecker::get();
    m_historyFailed = false;

    if (checker.releasesLoaded() && !checker.releases().empty()) {
        this->rebuildHistory();
        return;
    }

    this->showHistoryStatus(tr("pai.updates.loading", "Loading history..."), false);
    checker.fetchReleasesAsync([self = WeakRef<UpdateCenterPopup>(this)](bool ok, std::string) {
        auto popup = self.lock();
        if (!popup) return;
        popup->m_historyFailed = !ok;
        popup->rebuildHistory();
    });
}

void UpdateCenterPopup::rebuildHistory() {
    if (!m_listHolder) return;
    m_listHolder->removeAllChildren();
    m_historyScroll = nullptr;

    auto& checker = UpdateChecker::get();
    if (m_historyFailed) {
        this->showHistoryStatus(tr("pai.updates.load_failed", "Couldn't load the history"), true);
        return;
    }

    std::vector<ReleaseInfo> visible;
    for (auto const& release : checker.releases()) {
        if (release.prerelease && !m_showPrereleases) continue;
        visible.push_back(release);
    }

    if (visible.empty()) {
        this->showHistoryStatus(
            checker.releasesLoading()
                ? tr("pai.updates.loading", "Loading history...")
                : tr("pai.updates.empty", "No releases to show"),
            false
        );
        return;
    }

    auto scroll = ScrollLayer::create({kListW, kListH});
    if (scroll) scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({kListX, kListY});
    m_listHolder->addChild(scroll);
    m_historyScroll = scroll;

    float contentH = std::max(static_cast<float>(visible.size()) * kRowH, kListH);
    scroll->m_contentLayer->setContentSize({kListW, contentH});

    float y = contentH;
    for (size_t i = 0; i < visible.size(); ++i) {
        y -= kRowH;
        auto* row = this->buildReleaseRow(visible[i], static_cast<int>(i));
        row->setPosition({0.f, y});
        scroll->m_contentLayer->addChild(row);
    }
    scroll->moveToTop();

    if (auto* bar = Scrollbar::create(scroll)) {
        bar->setContentSize({6.f, kListH - 6.f});
        bar->setPosition({kListX + kListW - 6.f, kListY + kListH / 2.f});
        m_listHolder->addChild(bar);
    }
}

CCNode* UpdateCenterPopup::buildReleaseRow(ReleaseInfo const& release, int index) {
    auto& checker = UpdateChecker::get();
    int cmp = UpdateChecker::compareVersions(checker.localVersion(), release.version);
    bool isCurrent = cmp == 0;
    bool isLatest = index == 0 && !release.prerelease;

    auto row = CCNode::create();
    row->setContentSize({kRowW, kRowH});
    row->setAnchorPoint({0.f, 0.f});

    // the installed build gets a green tint so it is easy to spot in the list.
    auto* bg = isCurrent
        ? paimon::ui::makeInset({kRowW, kRowH - 2.f}, 120, {34, 80, 52})
        : paimon::ui::makeInset({kRowW, kRowH - 2.f}, 75);
    bg->setPosition({0.f, 1.f});
    row->addChild(bg);

    auto versionLbl = CCLabelBMFont::create(fmt::format("v{}", release.version).c_str(), "goldFont.fnt");
    versionLbl->setScale(0.38f);
    versionLbl->setAnchorPoint({0.f, 0.5f});
    versionLbl->setPosition({4.f, kRowH - 10.f});
    row->addChild(versionLbl, 1);

    std::string sub = release.date;
    auto sizeText = formatSize(release.size);
    if (!sizeText.empty()) sub = sub.empty() ? sizeText : fmt::format("{}  -  {}", sub, sizeText);
    if (!sub.empty()) {
        auto subLbl = CCLabelBMFont::create(sub.c_str(), "bigFont.fnt");
        subLbl->setScale(0.22f);
        subLbl->setColor({150, 160, 180});
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setPosition({4.f, 8.f});
        row->addChild(subLbl, 1);
    }

    // badges sit after the version text. LATEST uses GD's cyan button with the gold font, the same
    // skin as the Check and Refresh buttons.
    float badgeX = 4.f + versionLbl->getScaledContentSize().width + 6.f;
    if (isLatest && !isCurrent) {
        auto* latest = paimon::ui::makeButtonSprite(tr("pai.updates.badge.latest", "LATEST").c_str(),
            paimon::ui::Btn::Cyan, kLatestW, kLatestScale, "goldFont.fnt");
        float const latestW = latest->getScaledContentSize().width;
        latest->setPosition({badgeX + latestW / 2.f, kRowH - 10.f});
        row->addChild(latest, 1);
        badgeX += latestW + 6.f;
    }
    if (release.prerelease) {
        auto* beta = makeBadge(tr("pai.updates.badge.beta", "BETA"), {200, 130, 40});
        beta->setPosition({badgeX, kRowH - 16.5f});
        row->addChild(beta, 1);
    }

    // the info icon and the action share one column on the right, so the buttons line up.
    float const actionX = kRowW - 4.f - kActionW / 2.f;
    float const midY = kRowH / 2.f;

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kRowW, kRowH});
    row->addChild(menu, 2);

    if (auto* infoSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        infoSpr->setScale(0.42f);
        auto* infoBtn = CCMenuItemExt::createSpriteExtra(
            infoSpr, [self = WeakRef<UpdateCenterPopup>(this), release](CCMenuItemSpriteExtra*) {
                if (auto popup = self.lock()) popup->showNotes(release);
            }
        );
        infoBtn->setPosition({actionX - kActionW / 2.f - 14.f, midY});
        menu->addChild(infoBtn);
    }

    if (isCurrent) {
        auto* tag = CCLabelBMFont::create(tr("pai.updates.installed", "Installed").c_str(), "bigFont.fnt");
        tag->setScale(0.26f);
        tag->setColor({140, 220, 160});
        tag->setPosition({actionX, midY});
        row->addChild(tag, 1);
        return row;
    }

    if (release.downloadUrl.empty()) {
        auto* tag = CCLabelBMFont::create(tr("pai.updates.no_file", "No file").c_str(), "bigFont.fnt");
        tag->setScale(0.24f);
        tag->setColor({150, 150, 160});
        tag->setPosition({actionX, midY});
        row->addChild(tag, 1);
        return row;
    }

    bool downgrade = cmp < 0;
    auto actionText = downgrade ? tr("pai.updates.revert", "Revert") : tr("pai.updates.install", "Install");
    auto* action = paimon::ui::makeButton(actionText.c_str(),
        [self = WeakRef<UpdateCenterPopup>(this), release] {
            if (auto popup = self.lock()) popup->confirmInstall(release);
        },
        downgrade ? paimon::ui::Btn::Red : paimon::ui::Btn::Green, kActionW, kActionScale, "bigFont.fnt");
    action->setPosition({actionX, midY});
    menu->addChild(action);
    return row;
}

void UpdateCenterPopup::showHistoryStatus(std::string const& text, bool retry) {
    if (!m_listHolder) return;
    m_listHolder->removeAllChildren();

    auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
    label->setScale(0.34f);
    label->setColor({180, 190, 210});
    label->setPosition({kListX + kListW / 2.f, kListY + kListH / 2.f + (retry ? 14.f : 0.f)});
    m_listHolder->addChild(label);

    if (!retry) return;

    auto menu = CCMenu::create();
    menu->setPosition({kListX + kListW / 2.f, kListY + kListH / 2.f - 14.f});
    m_listHolder->addChild(menu);

    auto* retryBtn = paimon::ui::makeButton(tr("pai.updates.retry", "Retry").c_str(),
        [self = WeakRef<UpdateCenterPopup>(this)] {
            auto popup = self.lock();
            if (!popup) return;
            popup->m_historyFailed = false;
            popup->loadHistory();
        }, paimon::ui::Btn::Cyan, kFooterBtnW, kFooterBtnScale, "bigFont.fnt");
    menu->addChild(retryBtn);
}

void UpdateCenterPopup::showNotes(ReleaseInfo const& release) {
    std::string notes = release.notes;
    if (notes.empty()) notes = tr("pai.updates.no_notes", "This release has no notes.");

    std::string title = release.name.empty() ? fmt::format("v{}", release.version) : release.name;
    bool installable = !release.downloadUrl.empty()
        && UpdateChecker::compareVersions(UpdateChecker::get().localVersion(), release.version) != 0;

    if (!installable) {
        if (auto* md = MDPopup::create(title, notes, tr("pai.updates.close", "Close"))) md->show();
        return;
    }

    auto* md = MDPopup::create(
        title, notes,
        tr("pai.updates.close", "Close"),
        tr("pai.updates.install", "Install"),
        [self = WeakRef<UpdateCenterPopup>(this), release](bool install) {
            if (!install) return;
            if (auto popup = self.lock()) popup->confirmInstall(release);
        }
    );
    if (md) md->show();
}

void UpdateCenterPopup::showLocalChangelog() {
    auto const& changelogOpt = Mod::get()->getMetadata().getChangelog();
    std::string body = (changelogOpt && !changelogOpt->empty())
        ? *changelogOpt
        : tr("pai.updates.no_notes", "This release has no notes.");
    std::string title = fmt::format(
        "{} ({})",
        tr("pai.updates.changelog.title", "Changelog"),
        versionText()
    );
    if (auto* md = MDPopup::create(title, body, tr("pai.updates.close", "Close"))) {
        md->show();
    }
}

void UpdateCenterPopup::confirmInstall(ReleaseInfo const& release) {
    auto& checker = UpdateChecker::get();
    bool downgrade = UpdateChecker::compareVersions(checker.localVersion(), release.version) < 0;

    std::string body = downgrade
        ? fmt::format(
            fmt::runtime(tr("pai.updates.confirm.revert",
                "You are going back to <cy>v{}</c> from <cy>v{}</c>.\nNewer features and settings may stop working until you update again.")),
            release.version, checker.localVersion())
        : fmt::format(
            fmt::runtime(tr("pai.updates.confirm.install",
                "Download and install <cy>v{}</c>?\nIt is applied when you restart the game.")),
            release.version);

    PopupManager::get().quickPopup(
        fmt::format(fmt::runtime(tr("pai.updates.confirm.title", "Install v{}")), release.version),
        body,
        tr("pai.updates.cancel", "Cancel"),
        downgrade ? tr("pai.updates.revert", "Revert") : tr("pai.updates.install", "Install"),
        [self = WeakRef<UpdateCenterPopup>(this), release](auto*, bool confirmed) {
            if (!confirmed) return;
            if (auto popup = self.lock()) popup->startInstall(release);
        }
    ).showInstant();
}

void UpdateCenterPopup::startInstall(ReleaseInfo const& release) {
    if (m_refreshing || UpdateChecker::get().isDownloading()) return;
    if (release.downloadUrl.empty()) {
        PaimonNotify::show(tr("pai.updates.no_file", "No file"), NotificationIcon::Error);
        return;
    }

    auto* progress = UpdateProgressPopup::create(
        release.downloadUrl, release.version,
        [self = WeakRef<UpdateCenterPopup>(this)]() {
            if (auto popup = self.lock()) {
                popup->refreshHeader();
                popup->rebuildHistory();
            }
        }
    );
    if (progress) {
        progress->show();
        this->refreshHeader();
    }
}

} // namespace paimon::updates
