#include "CreatePostPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using paimon::forum::ForumApi;
using paimon::forum::Post;
using paimon::forum::CreatePostRequest;

namespace {
    constexpr float POPUP_W = 460.f;
    constexpr float POPUP_H = 340.f;
}

bool CreatePostPopup::init(
    std::vector<std::string> availableTags,
    CopyableFunction<void(Post const&)> onCreated
) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;

    m_availableTags = std::move(availableTags);
    m_onCreated = std::move(onCreated);

    this->setTitle("Create New Post");

    auto contentSize = m_mainLayer->getContentSize();
    float cx = contentSize.width / 2.f;

    // ── Title input ───────────────────────────────────────────────────
    auto titleLbl = CCLabelBMFont::create("Title", "bigFont.fnt");
    titleLbl->setScale(0.35f);
    titleLbl->setAnchorPoint({0.f, 0.5f});
    titleLbl->setPosition({18.f, contentSize.height - 38.f});
    titleLbl->setColor({200, 200, 220});
    m_mainLayer->addChild(titleLbl);

    m_titleInput = TextInput::create(420.f, "Post title...", "chatFont.fnt");
    m_titleInput->setCommonFilter(CommonFilter::Any);
    m_titleInput->setMaxCharCount(80);
    m_titleInput->setPosition({cx, contentSize.height - 56.f});
    m_titleInput->setScale(0.85f);
    m_mainLayer->addChild(m_titleInput);

    // ── Description input ─────────────────────────────────────────────
    auto descLbl = CCLabelBMFont::create("Description", "bigFont.fnt");
    descLbl->setScale(0.35f);
    descLbl->setAnchorPoint({0.f, 0.5f});
    descLbl->setPosition({18.f, contentSize.height - 82.f});
    descLbl->setColor({200, 200, 220});
    m_mainLayer->addChild(descLbl);

    m_descInput = TextInput::create(420.f, "Description...", "chatFont.fnt");
    m_descInput->setCommonFilter(CommonFilter::Any);
    m_descInput->setMaxCharCount(500);
    m_descInput->setPosition({cx, contentSize.height - 100.f});
    m_descInput->setScale(0.85f);
    m_mainLayer->addChild(m_descInput);

    // ── Tags section ──────────────────────────────────────────────────
    auto tagsLbl = CCLabelBMFont::create("Tags", "bigFont.fnt");
    tagsLbl->setScale(0.32f);
    tagsLbl->setAnchorPoint({0.f, 0.5f});
    tagsLbl->setPosition({18.f, contentSize.height - 128.f});
    tagsLbl->setColor({200, 200, 220});
    m_mainLayer->addChild(tagsLbl);

    // grid auto-wrap de chips (compact)
    m_tagMenu = CCMenu::create();
    m_tagMenu->setID("create-post-tags"_spr);
    m_tagMenu->setContentSize({contentSize.width - 60.f, 80.f});
    m_tagMenu->setAnchorPoint({0.f, 1.f});
    m_tagMenu->setPosition({16.f, contentSize.height - 142.f});
    m_tagMenu->setLayout(
        RowLayout::create()
            ->setGap(3.f)
            ->setGrowCrossAxis(true)
            ->setCrossAxisOverflow(false)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Start)
    );
    m_mainLayer->addChild(m_tagMenu);

    // boton + para tag custom (a la derecha del grid)
    auto plusSpr = ButtonSprite::create("+", "bigFont.fnt", "GJ_button_06.png", 0.8f);
    plusSpr->setScale(0.4f);
    auto plusBtn = CCMenuItemSpriteExtra::create(plusSpr, this, menu_selector(CreatePostPopup::onAddCustomTag));
    plusBtn->setPosition({contentSize.width - 24.f, contentSize.height - 138.f});
    m_buttonMenu->addChild(plusBtn);

    m_tagsHint = CCLabelBMFont::create("No tags — tap + to add", "bigFont.fnt");
    m_tagsHint->setScale(0.26f);
    m_tagsHint->setColor({140, 140, 160});
    m_tagsHint->setPosition({cx, contentSize.height - 158.f});
    m_mainLayer->addChild(m_tagsHint);

    rebuildTagChips();

    // ── Cooldown label ──────────────────────────────────────────────
    m_cooldownLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_cooldownLabel->setScale(0.45f);
    m_cooldownLabel->setPosition({284.5f, 75.f});
    m_cooldownLabel->setColor({255, 180, 80});
    m_cooldownLabel->setVisible(false);
    m_mainLayer->addChild(m_cooldownLabel, 10);
    updateCooldownLabel();

    // ── Submit button ─────────────────────────────────────────────────
    auto postSpr = ButtonSprite::create("Post", "goldFont.fnt", "GJ_button_01.png", 0.9f);
    postSpr->setScale(0.85f);
    auto postBtn = CCMenuItemSpriteExtra::create(postSpr, this, menu_selector(CreatePostPopup::onSubmit));
    postBtn->setPosition({cx, 8.f});
    m_buttonMenu->addChild(postBtn);

    paimon::markDynamicPopup(this);
    this->scheduleUpdate();
    return true;
}

void CreatePostPopup::rebuildTagChips() {
    if (!m_tagMenu) return;
    m_tagMenu->removeAllChildren();

    if (m_tagsHint) {
        m_tagsHint->setVisible(m_availableTags.empty());
    }

    for (size_t i = 0; i < m_availableTags.size(); i++) {
        bool selected = std::find(m_selectedTags.begin(), m_selectedTags.end(),
            m_availableTags[i]) != m_selectedTags.end();
        auto spr = ButtonSprite::create(
            m_availableTags[i].c_str(), "bigFont.fnt",
            selected ? "GJ_button_01.png" : "GJ_button_05.png", 0.8f
        );
        spr->setScale(0.34f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(CreatePostPopup::onToggleTag));
        btn->setTag(static_cast<int>(i));
        m_tagMenu->addChild(btn);
    }
    m_tagMenu->updateLayout();
}

void CreatePostPopup::onToggleTag(CCObject* sender) {
    int idx = static_cast<CCNode*>(sender)->getTag();
    if (idx < 0 || idx >= (int)m_availableTags.size()) return;
    auto const& tag = m_availableTags[idx];
    auto it = std::find(m_selectedTags.begin(), m_selectedTags.end(), tag);
    if (it != m_selectedTags.end()) {
        m_selectedTags.erase(it);
    } else {
        m_selectedTags.push_back(tag);
    }
    rebuildTagChips();
}

void CreatePostPopup::onAddCustomTag(CCObject*) {
    auto alert = FLAlertLayer::create(this, "New Tag", "Enter tag name:", "Add", "Cancel", 200.f);
    if (!alert) return;

    auto contentSize = alert->getContentSize();
    float cx = contentSize.width / 2.f;

    m_newTagInput = TextInput::create(170.f, "Tag name", "chatFont.fnt");
    m_newTagInput->setCommonFilter(CommonFilter::Any);
    m_newTagInput->setMaxCharCount(20);
    m_newTagInput->setPosition({cx, contentSize.height / 2.f + 10.f});
    m_newTagInput->setScale(0.8f);
    alert->addChild(m_newTagInput);

    alert->show();
}

void CreatePostPopup::FLAlert_Click(FLAlertLayer* alert, bool isAdd) {
    if (!isAdd || !m_newTagInput) return;
    std::string val = m_newTagInput->getString();
    if (val.empty()) return;
    bool exists = std::find(m_availableTags.begin(), m_availableTags.end(), val) != m_availableTags.end();
    if (!exists) m_availableTags.push_back(val);
    if (std::find(m_selectedTags.begin(), m_selectedTags.end(), val) == m_selectedTags.end()) {
        m_selectedTags.push_back(val);
    }
    rebuildTagChips();
    m_newTagInput = nullptr;
}

void CreatePostPopup::onSubmit(CCObject*) {
    std::string title = m_titleInput ? m_titleInput->getString() : "";
    std::string desc  = m_descInput  ? m_descInput->getString()  : "";

    if (title.empty()) {
        PaimonNotify::create("Please enter a title", NotificationIcon::Warning)->show();
        return;
    }

    // Check cooldown
    auto cd = ForumApi::get().getPostCooldownRemaining();
    if (cd > 0) {
        PaimonNotify::create(fmt::format("Please wait {} seconds before posting again.", cd).c_str(), NotificationIcon::Warning)->show();
        return;
    }

    CreatePostRequest req;
    req.title = title;
    req.description = desc;
    req.tags = m_selectedTags;

    auto onCreated = m_onCreated;
    WeakRef<CreatePostPopup> self = this;
    ForumApi::get().createPost(req, [self, onCreated](paimon::forum::Result<Post> res) {
        if (!res.ok) {
            std::string msg = res.error;
            if (msg.find("Rate limited") != std::string::npos) {
                PaimonNotify::create("You're posting too fast. Please wait a moment.", NotificationIcon::Warning)->show();
            } else {
                PaimonNotify::create(("Failed: " + msg).c_str(), NotificationIcon::Error)->show();
            }
            return;
        }
        PaimonNotify::create("Post published!", NotificationIcon::Success)->show();
        if (onCreated) onCreated(res.data);
        if (auto popup = self.lock()) popup->onClose(nullptr);
    });
}

void CreatePostPopup::updateCooldownLabel() {
    if (!m_cooldownLabel) return;
    auto cd = ForumApi::get().getPostCooldownRemaining();
    if (cd > 0) {
        m_cooldownLabel->setString(fmt::format("Wait {}s to post", cd).c_str());
        m_cooldownLabel->setVisible(true);
    } else {
        m_cooldownLabel->setString("");
        m_cooldownLabel->setVisible(false);
    }
}

void CreatePostPopup::update(float) {
    updateCooldownLabel();
}

CreatePostPopup* CreatePostPopup::create(
    std::vector<std::string> availableTags,
    CopyableFunction<void(Post const&)> onCreated
) {
    auto ret = new CreatePostPopup();
    if (ret && ret->init(std::move(availableTags), std::move(onCreated))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}
