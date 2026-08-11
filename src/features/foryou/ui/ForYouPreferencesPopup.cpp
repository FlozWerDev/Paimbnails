#include "ForYouPreferencesPopup.hpp"
#include "TagPreferencesPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../services/TasteProfile.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::foryou;

static constexpr float POPUP_W = 384.f;
static constexpr float POPUP_H = 288.f;

static constexpr ccColor3B kColorSelected = {100, 200, 255};
static constexpr ccColor3B kColorIdle     = {255, 255, 255};
static constexpr ccColor3B kColorDimmed   = {170, 170, 170};

namespace {

int difficultySpriteValueFor(int difficulty) {
    switch (difficulty) {
        case 10: return static_cast<int>(GJDifficulty::Easy);
        case 20: return static_cast<int>(GJDifficulty::Normal);
        case 30: return static_cast<int>(GJDifficulty::Hard);
        case 40: return static_cast<int>(GJDifficulty::Harder);
        case 50: return static_cast<int>(GJDifficulty::Insane);
        case 60: return static_cast<int>(GJDifficulty::Demon);
        default: return static_cast<int>(GJDifficulty::NA);
    }
}

// Try fallbacks so older GD installs still show rating icons.
struct RatingTierInfo {
    const char* labelKey;
    std::vector<const char*> spriteFrames;
    float scale;
};

RatingTierInfo const& ratingTierInfo(int tier) {
    static std::vector<RatingTierInfo> const infos = {
        { "foryou.prefs_rating_star",
          { "GJ_starsIcon_001.png", "GJ_bigStar_001.png" },
          0.85f },
        { "foryou.prefs_rating_featured",
          { "GJ_featuredCoin_001.png" },
          0.70f },
        { "foryou.prefs_rating_epic",
          { "GJ_epicCoin_001.png" },
          0.70f },
        { "foryou.prefs_rating_legendary",
          { "GJ_epicCoin2_001.png", "GJ_epicCoin_001.png" },
          0.70f },
        { "foryou.prefs_rating_mythic",
          { "GJ_epicCoin3_001.png", "GJ_epicCoin2_001.png", "GJ_epicCoin_001.png" },
          0.70f },
    };
    if (tier < 0 || tier >= static_cast<int>(infos.size())) return infos[0];
    return infos[tier];
}

CCSprite* createRatingSprite(int tier) {
    auto const& info = ratingTierInfo(tier);
    CCSprite* spr = nullptr;
    for (auto const* frameName : info.spriteFrames) {
        spr = paimon::SpriteHelper::safeCreateWithFrameName(frameName);
        if (spr) break;
    }
    if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_bigStar_001.png");
    if (spr) spr->setScale(info.scale);
    return spr;
}

// Keep labels readable on tinted buttons.
static void resetLabelsWhite(CCNode* node) {
    if (!node) return;
    if (auto* lbl = typeinfo_cast<CCLabelBMFont*>(node)) {
        lbl->setColor({255, 255, 255});
    }
    for (auto* child : CCArrayExt<CCNode*>(node->getChildren())) {
        resetLabelsWhite(child);
    }
}

static void setBtnColor(CCMenuItemSpriteExtra* btn, ccColor3B color) {
    if (!btn) return;
    btn->setColor(color);
    resetLabelsWhite(btn);
}

}

ForYouPreferencesPopup* ForYouPreferencesPopup::create(std::function<void()> onConfirm) {
    if (!paimon::modules::isEnabled("paimbnails.foryou.browser")) return nullptr;

    auto ret = new ForYouPreferencesPopup();
    if (ret && ret->init(std::move(onConfirm))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ForYouPreferencesPopup::init(std::function<void()> onConfirm) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;
    paimon::markDynamicPopup(this);

    m_onConfirm = std::move(onConfirm);

    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle(Localization::get().getString("foryou.prefs_title").c_str());

    // Recessed panels separate the control groups.
    auto addPanel = [&](float top, float height) {
        float const width = POPUP_W - 26.f;
        if (auto* panel = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
            panel->setContentSize({width, height});
            panel->setAnchorPoint({0.5f, 1.f});
            panel->setPosition({cx, top});
            panel->setColor({26, 20, 14});
            panel->setOpacity(120);
            m_mainLayer->addChild(panel, -1);
        }
        if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
            border->setContentSize({width, height});
            border->setAnchorPoint({0.5f, 1.f});
            border->setPosition({cx, top});
            border->setOpacity(70);
            m_mainLayer->addChild(border, -1);
        }
    };

    auto addSectionHeader = [&](CCNode* parent, const char* labelKey,
                                 const char* infoTitleKey, const char* infoDescKey,
                                 float posY, float labelScale = 0.42f) {
        auto lbl = CCLabelBMFont::create(
            Localization::get().getString(labelKey).c_str(), "goldFont.fnt");
        lbl->setScale(labelScale);
        lbl->setPosition({cx, posY});
        parent->addChild(lbl);

        auto infoMenu = CCMenu::create();
        infoMenu->setPosition({0.f, 0.f});
        parent->addChild(infoMenu);

        auto iBtn = PaimonInfo::createInfoBtn(
            Localization::get().getString(infoTitleKey),
            Localization::get().getString(infoDescKey),
            this, 0.32f);
        if (iBtn) {
            float halfW = lbl->getContentSize().width * labelScale * 0.5f;
            iBtn->setPosition({cx + halfW + 9.f, posY + 1.f});
            infoMenu->addChild(iBtn);
        }
    };

    auto makeRowMenu = [&](CCNode* parent, float posY, float gap) {
        auto menu = CCMenu::create();
        menu->setPosition({cx, posY});
        menu->setContentSize({POPUP_W - 34.f, 28.f});
        menu->setLayout(
            RowLayout::create()
                ->setGap(gap)
                ->setAxisAlignment(AxisAlignment::Center));
        parent->addChild(menu);
        return menu;
    };

    auto addPillBtn = [&](CCMenu* menu, const char* text, int tag,
                           SEL_MenuHandler sel,
                           std::vector<CCMenuItemSpriteExtra*>& out) {
        auto spr = ButtonSprite::create(
            text, 0, false, "bigFont.fnt",
            "GJ_button_04.png", 22.f, 0.7f);
        if (spr) spr->setScale(0.55f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, sel);
        btn->setTag(tag);
        menu->addChild(btn);
        out.push_back(btn);
    };

    addPanel(content.height - 26.f, 84.f);

    addSectionHeader(m_mainLayer, "foryou.prefs_difficulty",
        "foryou.info_difficulty_title", "foryou.info_difficulty_desc",
        content.height - 38.f);

    auto diffMenu = makeRowMenu(m_mainLayer, content.height - 61.f, 3.f);
    for (int v : {10, 20, 30, 40, 50, 60}) {
        auto holder = CCNode::create();
        holder->setContentSize({32.f, 32.f});

        auto diffSpr = GJDifficultySprite::create(
            difficultySpriteValueFor(v), GJDifficultyName::Short);
        if (diffSpr) {
            diffSpr->setScale(0.55f);
            diffSpr->setPosition({16.f, 16.f});
            holder->addChild(diffSpr);
        }

        auto btn = CCMenuItemSpriteExtra::create(
            holder, this,
            menu_selector(ForYouPreferencesPopup::onDifficultySelect));
        btn->setTag(v);
        diffMenu->addChild(btn);
        m_diffButtons.push_back(btn);
    }
    diffMenu->updateLayout();

    m_demonRow = CCNode::create();
    m_demonRow->setContentSize(content);
    m_demonRow->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_demonRow);

    auto demonMenu = makeRowMenu(m_demonRow, content.height - 87.f, 2.f);
    struct DemonOpt { const char* lbl; int v; };
    std::vector<DemonOpt> const demons = {
        {"Any", 0}, {"Easy", 1}, {"Medium", 2},
        {"Hard", 3}, {"Insane", 4}, {"Extreme", 5}
    };
    for (auto const& d : demons) {
        addPillBtn(demonMenu, d.lbl, d.v,
            menu_selector(ForYouPreferencesPopup::onDemonDiffSelect),
            m_demonButtons);
    }
    demonMenu->updateLayout();

    m_demonHint = CCLabelBMFont::create(
        Localization::get().getString("foryou.prefs_demon_hint").c_str(), "chatFont.fnt");
    m_demonHint->setScale(0.36f);
    m_demonHint->setOpacity(120);
    m_demonHint->setPosition({cx, content.height - 87.f});
    m_mainLayer->addChild(m_demonHint);

    addPanel(content.height - 118.f, 50.f);

    addSectionHeader(m_mainLayer, "foryou.prefs_gamemode",
        "foryou.info_mode_title", "foryou.info_mode_desc",
        content.height - 130.f);

    auto modeMenu = makeRowMenu(m_mainLayer, content.height - 152.f, 4.f);
    struct KV { const char* key; int v; };
    std::vector<KV> const modes = {
        {"foryou.prefs_classic", 0},
        {"foryou.prefs_platformer", 1},
        {"foryou.prefs_both", 2}
    };
    for (auto const& m : modes) {
        addPillBtn(modeMenu, Localization::get().getString(m.key).c_str(), m.v,
            menu_selector(ForYouPreferencesPopup::onGameModeSelect),
            m_modeButtons);
    }
    modeMenu->updateLayout();

    addPanel(content.height - 176.f, 50.f);

    addSectionHeader(m_mainLayer, "foryou.prefs_length",
        "foryou.info_length_title", "foryou.info_length_desc",
        content.height - 188.f);

    auto lenMenu = makeRowMenu(m_mainLayer, content.height - 210.f, 2.f);
    std::vector<KV> const lengths = {
        {"foryou.prefs_tiny", 0}, {"foryou.prefs_short", 1},
        {"foryou.prefs_medium", 2}, {"foryou.prefs_long", 3},
        {"foryou.prefs_xl", 4}, {"foryou.prefs_any_length", 5}
    };
    for (auto const& l : lengths) {
        addPillBtn(lenMenu, Localization::get().getString(l.key).c_str(), l.v,
            menu_selector(ForYouPreferencesPopup::onLengthSelect),
            m_lengthButtons);
    }
    lenMenu->updateLayout();

    float const bottomY = 26.f;
    addPanel(content.height - 232.f, 44.f);

    {
        auto ratingMenu = CCMenu::create();
        ratingMenu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(ratingMenu);

        auto container = CCNode::create();
        container->setContentSize({26.f, 26.f});
        container->setAnchorPoint({0.5f, 0.5f});

        m_ratingSprites.reserve(static_cast<int>(RatingTier::Count));
        for (int t = 0; t < static_cast<int>(RatingTier::Count); ++t) {
            auto spr = createRatingSprite(t);
            if (spr) {
                spr->setScale(spr->getScale() * 0.85f);
                spr->setPosition({13.f, 13.f});
                container->addChild(spr);
            }
            m_ratingSprites.push_back(spr);
        }

        auto ratingBtn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(ForYouPreferencesPopup::onRatingCycle));
        ratingBtn->setPosition({28.f, bottomY});
        ratingBtn->m_scaleMultiplier = 1.06f;
        ratingMenu->addChild(ratingBtn);

        m_ratingName = CCLabelBMFont::create(
            Localization::get().getString("foryou.prefs_rating_star").c_str(),
            "goldFont.fnt");
        m_ratingName->setScale(0.34f);
        m_ratingName->setAnchorPoint({0.f, 0.5f});
        m_ratingName->setPosition({45.f, bottomY});
        m_mainLayer->addChild(m_ratingName);

        auto iBtn = PaimonInfo::createInfoBtn(
            Localization::get().getString("foryou.info_rating_title"),
            Localization::get().getString("foryou.info_rating_desc"),
            this, 0.28f);
        if (iBtn) {
            iBtn->setPosition({45.f + 74.f, bottomY + 1.f});
            ratingMenu->addChild(iBtn);
        }
    }

    {
        auto tagsSpr = ButtonSprite::create(
            Localization::get().getString("foryou.tags_button").c_str(),
            0, false, "bigFont.fnt", "GJ_button_05.png", 22.f, 0.6f);
        if (tagsSpr) tagsSpr->setScale(0.6f);
        auto tagsBtn = CCMenuItemSpriteExtra::create(tagsSpr, this,
            menu_selector(ForYouPreferencesPopup::onTagPreferences));
        tagsBtn->setPosition({POPUP_W - 118.f, bottomY});
        tagsBtn->m_scaleMultiplier = 1.04f;
        m_buttonMenu->addChild(tagsBtn);
    }

    {
        auto confirmSpr = ButtonSprite::create(
            Localization::get().getString("foryou.prefs_confirm").c_str(),
            70, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.6f);
        if (confirmSpr) confirmSpr->setScale(0.68f);
        auto confirmBtn = CCMenuItemSpriteExtra::create(confirmSpr, this,
            menu_selector(ForYouPreferencesPopup::onConfirm));
        confirmBtn->setPosition({POPUP_W - 48.f, bottomY});
        confirmBtn->m_scaleMultiplier = 1.04f;
        m_buttonMenu->addChild(confirmBtn);
    }

    refreshDifficultyButtons();
    refreshDemonButtons();
    refreshGameModeButtons();
    refreshLengthButtons();
    refreshRatingTier();
    refreshDemonRowVisibility();

    return true;
}

void ForYouPreferencesPopup::onDifficultySelect(CCObject* sender) {
    m_difficulty = sender->getTag();
    refreshDifficultyButtons();
    refreshDemonRowVisibility();
}

void ForYouPreferencesPopup::onDemonDiffSelect(CCObject* sender) {
    m_demonDiff = sender->getTag();
    refreshDemonButtons();
}

void ForYouPreferencesPopup::onGameModeSelect(CCObject* sender) {
    m_gameMode = sender->getTag();
    refreshGameModeButtons();
}

void ForYouPreferencesPopup::onLengthSelect(CCObject* sender) {
    m_length = sender->getTag();
    refreshLengthButtons();
}

void ForYouPreferencesPopup::onRatingCycle(CCObject*) {
    m_ratingTier = (m_ratingTier + 1) % static_cast<int>(RatingTier::Count);
    refreshRatingTier();
}

void ForYouPreferencesPopup::onTagPreferences(CCObject*) {
    if (auto popup = TagPreferencesPopup::create()) popup->show();
}

void ForYouPreferencesPopup::onConfirm(CCObject*) {
    float platformerRatio = 0.f;
    if (m_gameMode == 1) platformerRatio = 1.f;
    else if (m_gameMode == 2) platformerRatio = 0.5f;

    // Higher rating tiers include the lower tiers.
    bool const starRated = m_ratingTier >= static_cast<int>(RatingTier::StarRated);
    bool const featured  = m_ratingTier >= static_cast<int>(RatingTier::Featured);
    bool const epic      = m_ratingTier >= static_cast<int>(RatingTier::Epic);

    TasteProfile::get().seedPreferences(
        m_difficulty, platformerRatio,
        m_length, starRated, featured, epic, m_demonDiff
    );
    TasteProfile::get().save();

    this->onClose(nullptr);
    if (m_onConfirm) m_onConfirm();
}

void ForYouPreferencesPopup::refreshDifficultyButtons() {
    for (auto* btn : m_diffButtons) {
        bool sel = btn->getTag() == m_difficulty;
        btn->setScale(sel ? 1.15f : 0.95f);
        btn->setColor(sel ? kColorSelected : kColorIdle);
    }
}

void ForYouPreferencesPopup::refreshDemonButtons() {
    for (auto* btn : m_demonButtons) {
        bool sel = btn->getTag() == m_demonDiff;
        btn->setScale(sel ? 1.15f : 1.0f);
        setBtnColor(btn, sel ? kColorSelected : kColorDimmed);
    }
}

void ForYouPreferencesPopup::refreshGameModeButtons() {
    for (auto* btn : m_modeButtons) {
        bool sel = btn->getTag() == m_gameMode;
        btn->setScale(sel ? 1.15f : 1.0f);
        setBtnColor(btn, sel ? kColorSelected : kColorDimmed);
    }
}

void ForYouPreferencesPopup::refreshLengthButtons() {
    for (auto* btn : m_lengthButtons) {
        bool sel = btn->getTag() == m_length;
        btn->setScale(sel ? 1.15f : 1.0f);
        setBtnColor(btn, sel ? kColorSelected : kColorDimmed);
    }
}

void ForYouPreferencesPopup::refreshRatingTier() {
    for (int i = 0; i < static_cast<int>(m_ratingSprites.size()); ++i) {
        if (m_ratingSprites[i]) m_ratingSprites[i]->setVisible(i == m_ratingTier);
    }
    if (m_ratingName) {
        m_ratingName->setString(
            Localization::get().getString(ratingTierInfo(m_ratingTier).labelKey).c_str());
    }
}

void ForYouPreferencesPopup::refreshDemonRowVisibility() {
    bool const isDemon = m_difficulty == 60;
    if (m_demonRow) m_demonRow->setVisible(isDemon);
    if (m_demonHint) m_demonHint->setVisible(!isDemon);
}
