#include "UpdateCenterPopup.hpp"
#include "UpdateProgressPopup.hpp"
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

constexpr float kWidth  = 420.f;
constexpr float kHeight = 285.f;
constexpr float kListX  = 20.f;
constexpr float kListY  = 44.f;
constexpr float kListW  = 380.f;
constexpr float kListH  = 130.f;
constexpr float kRowH   = 32.f;

std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') return fallback;
    return value;
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

    m_headerMenu = CCMenu::create();
    m_headerMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_headerMenu, 3);

    this->buildHeader();

    auto historyTitle = CCLabelBMFont::create(
        tr("pai.updates.history", "Version history").c_str(), "goldFont.fnt"
    );
    historyTitle->setScale(0.34f);
    historyTitle->setAnchorPoint({0.f, 0.5f});
    historyTitle->setPosition({20.f, 183.f});
    m_mainLayer->addChild(historyTitle, 2);

    auto betaLabel = CCLabelBMFont::create(tr("pai.updates.betas", "Betas").c_str(), "bigFont.fnt");
    betaLabel->setScale(0.3f);
    betaLabel->setColor({190, 200, 220});
    betaLabel->setAnchorPoint({0.f, 0.5f});
    betaLabel->setPosition({358.f, 183.f});
    m_mainLayer->addChild(betaLabel, 2);

    auto* betaToggle = CCMenuItemExt::createTogglerWithStandardSprites(
        0.4f, [self = WeakRef<UpdateCenterPopup>(this)](CCMenuItemToggler* sender) {
            auto popup = self.lock();
            if (!popup) return;
            popup->m_showPrereleases = !sender->isToggled();
            popup->rebuildHistory();
        }
    );
    betaToggle->setPosition({344.f, 183.f});
    m_headerMenu->addChild(betaToggle);

    auto listBg = paimon::SpriteHelper::createDarkPanel(kListW + 4.f, kListH + 4.f, 100, 5.f);
    listBg->setPosition({kListX - 2.f, kListY - 2.f});
    m_mainLayer->addChild(listBg, 1);

    m_listHolder = CCNode::create();
    m_mainLayer->addChild(m_listHolder, 2);

    auto autoLabel = CCLabelBMFont::create(
        tr("pai.updates.auto", "Auto update").c_str(), "bigFont.fnt"
    );
    autoLabel->setScale(0.3f);
    autoLabel->setColor({190, 200, 220});
    autoLabel->setAnchorPoint({0.f, 0.5f});
    autoLabel->setPosition({40.f, 25.f});
    m_mainLayer->addChild(autoLabel, 2);

    auto* autoToggle = CCMenuItemExt::createTogglerWithStandardSprites(
        0.45f, [](CCMenuItemToggler* sender) {
            bool enabled = !sender->isToggled();
            Mod::get()->setSettingValue<bool>("auto-update", enabled);
            PaimonNotify::show(
                tr(enabled ? "pai.updates.auto.on" : "pai.updates.auto.off",
                   enabled ? "Auto update on" : "Auto update off"),
                enabled ? NotificationIcon::Success : NotificationIcon::None,
                1.6f
            );
        }
    );
    autoToggle->setPosition({26.f, 25.f});
    autoToggle->toggle(paimon::settings::general::autoUpdate());
    m_headerMenu->addChild(autoToggle);

    auto ghSpr = ButtonSprite::create(
        tr("pai.updates.github", "GitHub").c_str(), "bigFont.fnt", "GJ_button_05.png", .8f
    );
    ghSpr->setScale(0.4f);
    auto* ghBtn = CCMenuItemExt::createSpriteExtra(ghSpr, [](CCMenuItemSpriteExtra*) {
        web::openLinkInBrowser("https://github.com/FlozWerDev/Paimbnails/releases");
    });
    ghBtn->setPosition({372.f, 25.f});
    m_headerMenu->addChild(ghBtn);

    this->loadHistory();
    this->schedule(schedule_selector(UpdateCenterPopup::pollState), 0.4f);
    return true;
}

void UpdateCenterPopup::buildHeader() {
    auto card = paimon::SpriteHelper::createDarkPanel(kListW + 4.f, 44.f, 130, 5.f);
    card->setPosition({kListX - 2.f, 196.f});
    m_mainLayer->addChild(card, 1);

    auto& checker = UpdateChecker::get();

    m_versionLabel = CCLabelBMFont::create(
        fmt::format("v{}", checker.localVersion()).c_str(), "goldFont.fnt"
    );
    m_versionLabel->setScale(0.5f);
    m_versionLabel->setAnchorPoint({0.f, 0.5f});
    m_versionLabel->setPosition({32.f, 226.f});
    m_mainLayer->addChild(m_versionLabel, 2);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setScale(0.3f);
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setPosition({32.f, 208.f});
    m_mainLayer->addChild(m_statusLabel, 2);

    this->refreshHeader();
}

void UpdateCenterPopup::refreshHeader() {
    auto& checker = UpdateChecker::get();
    auto state = checker.state();
    bool pending = checker.hasPendingInstall();

    m_lastState = state;
    m_lastPending = pending;

    std::string status;
    ccColor3B statusColor = {170, 180, 200};
    std::string btnText = tr("pai.updates.check", "Check");
    char const* btnSprite = "GJ_button_02.png";

    if (pending) {
        auto version = checker.pendingVersion();
        if (version.empty()) version = checker.remoteVersion();
        status = fmt::format(
            fmt::runtime(tr("pai.updates.pending", "v{} ready. Restart to apply it.")), version
        );
        statusColor = {120, 255, 140};
        btnText = tr("pai.update.restart", "Restart");
        btnSprite = "GJ_button_01.png";
    } else {
        switch (state) {
            case UpdateChecker::State::Checking:
                status = tr("pai.update.checking", "Checking for updates...");
                statusColor = {120, 200, 255};
                btnText = tr("pai.updates.checking.btn", "Wait...");
                btnSprite = "GJ_button_04.png";
                break;
            case UpdateChecker::State::UpdateAvailable:
                status = fmt::format(
                    fmt::runtime(tr("pai.updates.available", "Version {} available")),
                    checker.remoteVersion()
                );
                statusColor = {255, 220, 110};
                btnText = tr("pai.updates.update", "Update");
                btnSprite = "GJ_button_01.png";
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
        m_versionLabel->setString(fmt::format("v{}", checker.localVersion()).c_str());
    }
    if (m_statusLabel) {
        m_statusLabel->setString(status.c_str());
        m_statusLabel->setColor(statusColor);
    }

    if (m_primaryBtn) m_primaryBtn->removeFromParent();
    auto spr = ButtonSprite::create(btnText.c_str(), "bigFont.fnt", btnSprite, .8f);
    spr->setScale(0.5f);
    m_primaryBtn = CCMenuItemSpriteExtra::create(
        spr, this, menu_selector(UpdateCenterPopup::onPrimary)
    );
    m_primaryBtn->setPosition({340.f, 218.f});
    m_headerMenu->addChild(m_primaryBtn);
}

void UpdateCenterPopup::onPrimary(CCObject*) {
    auto& checker = UpdateChecker::get();

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

void UpdateCenterPopup::pollState(float) {
    auto& checker = UpdateChecker::get();
    if (checker.state() == m_lastState && checker.hasPendingInstall() == m_lastPending) return;
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
    row->setContentSize({kListW, kRowH});
    row->setAnchorPoint({0.f, 0.f});

    ccColor3B rowColor = (index % 2 == 0) ? ccColor3B{26, 28, 42} : ccColor3B{20, 22, 34};
    if (isCurrent) rowColor = {28, 52, 40};
    auto bg = paimon::SpriteHelper::createColorPanel(kListW - 4.f, kRowH - 2.f, rowColor, 235, 4.f);
    bg->setPosition({2.f, 1.f});
    row->addChild(bg);

    auto versionLbl = CCLabelBMFont::create(fmt::format("v{}", release.version).c_str(), "goldFont.fnt");
    versionLbl->setScale(0.38f);
    versionLbl->setAnchorPoint({0.f, 0.5f});
    versionLbl->setPosition({12.f, kRowH - 11.f});
    row->addChild(versionLbl, 1);

    std::string sub = release.date;
    auto sizeText = formatSize(release.size);
    if (!sizeText.empty()) sub = sub.empty() ? sizeText : fmt::format("{}  -  {}", sub, sizeText);
    if (!sub.empty()) {
        auto subLbl = CCLabelBMFont::create(sub.c_str(), "bigFont.fnt");
        subLbl->setScale(0.22f);
        subLbl->setColor({150, 160, 180});
        subLbl->setAnchorPoint({0.f, 0.5f});
        subLbl->setPosition({12.f, 10.f});
        row->addChild(subLbl, 1);
    }

    float badgeX = 14.f + versionLbl->getScaledContentSize().width;
    auto addBadge = [&](std::string const& text, ccColor3B color) {
        auto* badge = makeBadge(text, color);
        badge->setPosition({badgeX, kRowH - 17.5f});
        row->addChild(badge, 1);
        badgeX += badge->getContentSize().width + 4.f;
    };
    if (isCurrent) addBadge(tr("pai.updates.badge.current", "CURRENT"), {60, 160, 90});
    if (isLatest && !isCurrent) addBadge(tr("pai.updates.badge.latest", "LATEST"), {70, 120, 210});
    if (release.prerelease) addBadge(tr("pai.updates.badge.beta", "BETA"), {200, 130, 40});

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kListW, kRowH});
    row->addChild(menu, 2);

    if (auto* infoSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        infoSpr->setScale(0.42f);
        auto* infoBtn = CCMenuItemExt::createSpriteExtra(
            infoSpr, [self = WeakRef<UpdateCenterPopup>(this), release](CCMenuItemSpriteExtra*) {
                if (auto popup = self.lock()) popup->showNotes(release);
            }
        );
        infoBtn->setPosition({294.f, kRowH / 2.f});
        menu->addChild(infoBtn);
    }

    if (isCurrent) {
        auto tag = CCLabelBMFont::create(tr("pai.updates.installed", "Installed").c_str(), "bigFont.fnt");
        tag->setScale(0.26f);
        tag->setColor({140, 220, 160});
        tag->setPosition({338.f, kRowH / 2.f});
        row->addChild(tag, 1);
        return row;
    }

    if (release.downloadUrl.empty()) {
        auto tag = CCLabelBMFont::create(tr("pai.updates.no_file", "No file").c_str(), "bigFont.fnt");
        tag->setScale(0.24f);
        tag->setColor({150, 150, 160});
        tag->setPosition({338.f, kRowH / 2.f});
        row->addChild(tag, 1);
        return row;
    }

    bool downgrade = cmp < 0;
    auto actionSpr = ButtonSprite::create(
        downgrade ? tr("pai.updates.revert", "Revert").c_str()
                  : tr("pai.updates.install", "Install").c_str(),
        "bigFont.fnt",
        downgrade ? "GJ_button_06.png" : "GJ_button_01.png",
        .8f
    );
    actionSpr->setScale(0.42f);
    auto* actionBtn = CCMenuItemExt::createSpriteExtra(
        actionSpr, [self = WeakRef<UpdateCenterPopup>(this), release](CCMenuItemSpriteExtra*) {
            if (auto popup = self.lock()) popup->confirmInstall(release);
        }
    );
    actionBtn->setPosition({340.f, kRowH / 2.f});
    menu->addChild(actionBtn);

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

    auto spr = ButtonSprite::create(
        tr("pai.updates.retry", "Retry").c_str(), "bigFont.fnt", "GJ_button_02.png", .8f
    );
    spr->setScale(0.45f);
    menu->addChild(CCMenuItemExt::createSpriteExtra(
        spr, [self = WeakRef<UpdateCenterPopup>(this)](CCMenuItemSpriteExtra*) {
            auto popup = self.lock();
            if (!popup) return;
            popup->m_historyFailed = false;
            popup->loadHistory();
        }
    ));
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
    if (progress) progress->show();
}

} // namespace paimon::updates
