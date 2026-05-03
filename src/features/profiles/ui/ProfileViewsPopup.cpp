#include "ProfileViewsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/PaimonLoadingOverlay.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/InfoButton.hpp"
#include <Geode/binding/GameManager.hpp>
#include <algorithm>
#include <unordered_map>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace geode::prelude;
using paimon::forum::ProfileView;

static std::vector<ProfileView> dedupeViewsByLatest(std::vector<ProfileView> const& views) {
    std::unordered_map<std::string, ProfileView> latestByUser;
    latestByUser.reserve(views.size());

    for (auto const& view : views) {
        std::string key;
        if (view.viewerAccountID > 0) {
            key = fmt::format("id:{}", view.viewerAccountID);
        } else {
            key = fmt::format("name:{}", view.viewerUsername);
        }

        auto it = latestByUser.find(key);
        if (it == latestByUser.end() || view.viewedAt >= it->second.viewedAt) {
            latestByUser[key] = view;
        }
    }

    std::vector<ProfileView> uniqueViews;
    uniqueViews.reserve(latestByUser.size());
    for (auto const& [_, view] : latestByUser) {
        uniqueViews.push_back(view);
    }

    std::sort(uniqueViews.begin(), uniqueViews.end(), [](ProfileView const& a, ProfileView const& b) {
        return a.viewedAt > b.viewedAt;
    });
    return uniqueViews;
}

ProfileViewsPopup* ProfileViewsPopup::create(int accountID) {
    auto ret = new ProfileViewsPopup();
    if (ret && ret->init(accountID)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ProfileViewsPopup::init(int accountID) {
    if (!Popup::init(380.f, 260.f)) return false;

    m_accountID = accountID;
    this->setTitle("Profile Views");

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    // Info button (i)
    auto infoBtn = PaimonInfo::createInfoBtn(
        "Profile Views Info",
        "This shows who has viewed your profile.\n\nThe list displays usernames and when they viewed your profile.",
        this,
        0.9f
    );
    if (infoBtn) {
        infoBtn->setPosition({377.f, 258.f});
        m_buttonMenu->addChild(infoBtn);
    }

    m_countLabel = CCLabelBMFont::create("Loading...", "chatFont.fnt");
    m_countLabel->setScale(0.55f);
    m_countLabel->setPosition({centerX, contentSize.height - 42.f});
    m_countLabel->setColor({180, 180, 180});
    m_mainLayer->addChild(m_countLabel, 2);

    auto separator = paimon::SpriteHelper::createDarkPanel(300.f, 1.5f, 60, 0.f);
    separator->setPosition({centerX - 150.f, contentSize.height - 58.f});
    m_mainLayer->addChild(separator, 1);

    m_spinner = PaimonLoadingOverlay::create("Loading...", 35.f);
    m_spinner->show(m_mainLayer, 10);

    loadViews();
    paimon::markDynamicPopup(this);
    return true;
}

static std::string formatViewTime(int64_t epoch) {
    if (epoch <= 0) return "unknown";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

void ProfileViewsPopup::loadViews() {
    uint64_t gen = ++m_requestGeneration;
    WeakRef<ProfileViewsPopup> self = this;
    paimon::forum::ForumApi::get().getProfileViews(m_accountID,
        [self, gen](paimon::forum::Result<std::vector<ProfileView>> result) {
            auto popup = self.lock();
            if (!popup || gen != popup->m_requestGeneration) return;
            if (popup->m_spinner) {
                popup->m_spinner->dismiss();
                popup->m_spinner = nullptr;
            }
            if (!result.ok) {
                // Show friendly message depending on error type
                std::string msg = "Failed to load views";
                if (result.error.find("not available") != std::string::npos) {
                    msg = "Profile views not available";
                } else if (result.error.find("Rate limited") != std::string::npos) {
                    msg = "Rate limited — try again later";
                }
                if (popup->m_countLabel) popup->m_countLabel->setString(msg.c_str());
                return;
            }
            popup->buildViewsList(result.data);
        });
}

void ProfileViewsPopup::buildViewsList(const std::vector<ProfileView>& views) {
    auto uniqueViews = dedupeViewsByLatest(views);
    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    if (m_countLabel) {
        if (uniqueViews.empty()) {
            m_countLabel->setString("No views yet");
        } else {
            m_countLabel->setString(fmt::format("{} view{}", uniqueViews.size(), uniqueViews.size() == 1 ? "" : "s").c_str());
        }
    }

    float scrollW = contentSize.width - 30.f;
    float scrollH = contentSize.height - 80.f;
    float scrollX = 15.f;
    float scrollY = 10.f;

    std::vector<CCNode*> cells;
    for (auto const& v : uniqueViews) {
        auto cell = createViewCell(v.viewerUsername, v.viewedAt, scrollW);
        if (cell) cells.push_back(cell);
    }

    if (cells.empty()) {
        auto noViews = CCLabelBMFont::create("No views yet", "chatFont.fnt");
        noViews->setScale(0.6f);
        noViews->setColor({150, 150, 150});
        noViews->setPosition({centerX, contentSize.height / 2.f - 20.f});
        noViews->setOpacity(0);
        noViews->runAction(CCFadeIn::create(0.5f));
        m_mainLayer->addChild(noViews);
        return;
    }

    float totalH = 0.f;
    for (auto& c : cells) {
        totalH += c->getContentHeight() + 4.f;
    }
    totalH = std::max(totalH, scrollH);

    auto container = CCLayer::create();
    container->setContentSize({scrollW, totalH});

    float yPos = totalH;
    for (auto& c : cells) {
        yPos -= c->getContentHeight() + 4.f;
        c->setPosition({0.f, yPos});
        container->addChild(c);
    }

    auto scroll = geode::ScrollLayer::create({scrollW, scrollH});
    scroll->setPosition({scrollX, scrollY});
    scroll->m_contentLayer->addChild(container);
    scroll->m_contentLayer->setContentSize({scrollW, totalH});
    scroll->scrollToTop();
    m_mainLayer->addChild(scroll);
    m_scrollView = scroll;
}

CCNode* ProfileViewsPopup::createViewCell(std::string const& username, int64_t viewedAt, float width) {
    auto node = CCNode::create();
    float height = 26.f;
    node->setContentSize({width, height});

    auto nameLabel = CCLabelBMFont::create(username.c_str(), "chatFont.fnt");
    nameLabel->setScale(0.55f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({8.f, height / 2.f});
    nameLabel->setColor({220, 220, 240});
    node->addChild(nameLabel);

    auto timeStr = formatViewTime(viewedAt);
    auto timeLabel = CCLabelBMFont::create(timeStr.c_str(), "chatFont.fnt");
    timeLabel->setScale(0.5f);
    timeLabel->setAnchorPoint({1.f, 0.5f});
    timeLabel->setPosition({width - 8.f, height / 2.f});
    timeLabel->setColor({160, 160, 180});
    node->addChild(timeLabel);

    auto line = paimon::SpriteHelper::createDarkPanel(width - 10.f, 0.8f, 40, 0.f);
    line->setPosition({5.f, 0.f});
    node->addChild(line, -1);

    return node;
}
