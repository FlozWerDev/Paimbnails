#include "UserReportsPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../ui/PaimonUI.hpp"

using namespace geode::prelude;
using namespace cocos2d;

bool UserReportsPopup::init(std::string const& reportedUsername, std::vector<ReportEntry> const& reports) {
    if (!PaimonPopup::init(380.f, 210.f)) return false;

    m_reports = reports;
    m_reportedUsername = reportedUsername;

    this->setTitle(fmt::format("Reports: {}", m_reportedUsername).c_str());
    this->addInfoButton("User Reports",
        "Each report filed against this user. Use the <cy>arrows</c> to move between them; "
        "<cg>By</c> is who filed it and the text below is their note.");

    auto contentSize = m_mainLayer->getContentSize();
    float centerX = contentSize.width / 2.f;

    CCSize insetSize = {contentSize.width - 70.f, 96.f};
    auto inset = paimon::ui::makeInset(insetSize);
    inset->setPosition({centerX - insetSize.width / 2.f, contentSize.height / 2.f - 42.f});
    m_mainLayer->addChild(inset);

    m_reporterLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_reporterLabel->setScale(0.4f);
    m_reporterLabel->setPosition({centerX, contentSize.height - 60.f});
    m_mainLayer->addChild(m_reporterLabel);

    m_noteLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_noteLabel->setScale(0.45f);
    m_noteLabel->setPosition({centerX, contentSize.height - 92.f});
    m_noteLabel->setColor({220, 220, 220});
    m_noteLabel->setWidth(insetSize.width - 20.f);
    m_noteLabel->setAlignment(CCTextAlignment::kCCTextAlignmentCenter);
    m_mainLayer->addChild(m_noteLabel, 1);

    m_counterLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_counterLabel->setScale(0.4f);
    m_counterLabel->setPosition({centerX, 36.f});
    m_counterLabel->setColor({180, 180, 180});
    m_mainLayer->addChild(m_counterLabel);

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    navMenu->setContentSize(contentSize);
    m_mainLayer->addChild(navMenu, 10);

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    prevSpr->setScale(0.5f);
    m_prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(UserReportsPopup::onPrev));
    m_prevBtn->setPosition({30.f, contentSize.height / 2.f});
    navMenu->addChild(m_prevBtn);

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    nextSpr->setFlipX(true);
    nextSpr->setScale(0.5f);
    m_nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(UserReportsPopup::onNext));
    m_nextBtn->setPosition({contentSize.width - 30.f, contentSize.height / 2.f});
    navMenu->addChild(m_nextBtn);

    updateDisplay();
    paimon::markDynamicPopup(this);
    return true;
}

void UserReportsPopup::updateDisplay() {
    if (m_reports.empty()) return;
    auto& report = m_reports[m_currentIndex];

    m_reporterLabel->setString(fmt::format("By: {}", report.reporter).c_str());
    m_noteLabel->setString(report.note.c_str());
    m_counterLabel->setString(fmt::format("{}/{}", m_currentIndex + 1, m_reports.size()).c_str());

    m_prevBtn->setVisible(m_currentIndex > 0);
    m_nextBtn->setVisible(m_currentIndex < (int)m_reports.size() - 1);
}

void UserReportsPopup::onPrev(CCObject*) {
    if (m_currentIndex > 0) {
        m_currentIndex--;
        updateDisplay();
    }
}

void UserReportsPopup::onNext(CCObject*) {
    if (m_currentIndex < (int)m_reports.size() - 1) {
        m_currentIndex++;
        updateDisplay();
    }
}

UserReportsPopup* UserReportsPopup::create(std::string const& reportedUsername, std::vector<ReportEntry> const& reports) {
    auto ret = new UserReportsPopup();
    if (ret && ret->init(reportedUsername, reports)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
