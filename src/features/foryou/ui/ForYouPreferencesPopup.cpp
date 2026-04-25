#include "ForYouPreferencesPopup.hpp"
#include "../services/ForYouTracker.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/InfoButton.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::foryou;

// ── constants ────────────────────────────────────────────
static constexpr float POPUP_W = 340.f;
static constexpr float POPUP_H = 250.f;

static constexpr ccColor3B kColorSelected = {100, 200, 255};
static constexpr ccColor3B kColorIdle     = {255, 255, 255};
static constexpr ccColor3B kColorDimmed   = {170, 170, 170};

// ── helpers (file-local) ─────────────────────────────────
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

// Info for each rating tier. Each entry lists sprite-frame candidates
// tried in order so older GD installs (without epicCoin2/3) still show
// *some* icon instead of the magenta fallback.
struct RatingTierInfo {
    const char* labelKey;
    std::vector<const char*> spriteFrames;
    float scale;
};

RatingTierInfo const& ratingTierInfo(int tier) {
    static std::vector<RatingTierInfo> const infos = {
        // 0 — Star Rated
        { "foryou.prefs_rating_star",
          { "GJ_starsIcon_001.png", "GJ_bigStar_001.png" },
          0.85f },
        // 1 — Featured
        { "foryou.prefs_rating_featured",
          { "GJ_featuredCoin_001.png" },
          0.70f },
        // 2 — Epic
        { "foryou.prefs_rating_epic",
          { "GJ_epicCoin_001.png" },
          0.70f },
        // 3 — Legendary
        { "foryou.prefs_rating_legendary",
          { "GJ_epicCoin2_001.png", "GJ_epicCoin_001.png" },
          0.70f },
        // 4 — Mythic
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

// Recursively reset all CCLabelBMFont children to white so the text
// stays readable when the ButtonSprite background is tinted cyan.
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

} // namespace

// ── create ───────────────────────────────────────────────
ForYouPreferencesPopup* ForYouPreferencesPopup::create(std::function<void()> onConfirm) {
    auto ret = new ForYouPreferencesPopup();
    if (ret && ret->init(std::move(onConfirm))) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ── init ─────────────────────────────────────────────────
bool ForYouPreferencesPopup::init(std::function<void()> onConfirm) {
    if (!Popup::init(POPUP_W, POPUP_H)) return false;

    m_onConfirm = std::move(onConfirm);

    auto const content = m_mainLayer->getContentSize();
    float const cx  = content.width / 2.f;
    float const top = content.height;

    this->setTitle(Localization::get().getString("foryou.prefs_title").c_str());

    // ── local helpers ────────────────────────────────────
    auto addSectionHeader = [&](CCNode* parent, const char* labelKey,
                                 const char* infoTitleKey, const char* infoDescKey,
                                 float posY, float labelScale = 0.46f) {
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
            this, 0.36f);
        if (iBtn) {
            float halfW = lbl->getContentSize().width * labelScale * 0.5f;
            iBtn->setPosition({cx + halfW + 10.f, posY + 1.f});
            infoMenu->addChild(iBtn);
        }
    };

    auto makeRowMenu = [&](CCNode* parent, float posY, float gap) {
        auto menu = CCMenu::create();
        menu->setPosition({cx, posY});
        menu->setContentSize({POPUP_W - 20.f, 28.f});
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

    // ── Section 1: Difficulty ────────────────────────────
    addSectionHeader(m_mainLayer, "foryou.prefs_difficulty",
        "foryou.info_difficulty_title", "foryou.info_difficulty_desc",
        top - 38.f);

    auto diffMenu = makeRowMenu(m_mainLayer, top - 62.f, 3.f);
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

    // ── Section 2: Demon Difficulty (overlay, visible only for Demon) ──
    m_demonRow = CCNode::create();
    m_demonRow->setContentSize(content);
    m_demonRow->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_demonRow);

    addSectionHeader(m_demonRow, "foryou.prefs_demon_diff",
        "foryou.info_demon_title", "foryou.info_demon_desc",
        top - 82.f, 0.40f);

    auto demonMenu = makeRowMenu(m_demonRow, top - 98.f, 2.f);
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
    m_demonRow->setVisible(m_difficulty == 60);

    // ── Section 3: Game Mode ─────────────────────────────
    addSectionHeader(m_mainLayer, "foryou.prefs_gamemode",
        "foryou.info_mode_title", "foryou.info_mode_desc",
        top - 118.f);

    auto modeMenu = makeRowMenu(m_mainLayer, top - 136.f, 4.f);
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

    // ── Section 4: Length ────────────────────────────────
    addSectionHeader(m_mainLayer, "foryou.prefs_length",
        "foryou.info_length_title", "foryou.info_length_desc",
        top - 152.f);

    auto lenMenu = makeRowMenu(m_mainLayer, top - 170.f, 2.f);
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

    // ── Section 5: Rating tier cycle + Confirm (bottom row) ──
    float const bottomY = 18.f;

    // label + info on the left
    auto ratingLbl = CCLabelBMFont::create(
        Localization::get().getString("foryou.prefs_rating").c_str(),
        "goldFont.fnt");
    ratingLbl->setScale(0.40f);
    ratingLbl->setAnchorPoint({0.f, 0.5f});
    ratingLbl->setPosition({14.f, bottomY + 30.f});
    m_mainLayer->addChild(ratingLbl);

    {
        auto infoMenu = CCMenu::create();
        infoMenu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(infoMenu);
        auto iBtn = PaimonInfo::createInfoBtn(
            Localization::get().getString("foryou.info_rating_title"),
            Localization::get().getString("foryou.info_rating_desc"),
            this, 0.32f);
        if (iBtn) {
            float lblW = ratingLbl->getContentSize().width * 0.40f;
            iBtn->setPosition({14.f + lblW + 7.f, bottomY + 31.f});
            infoMenu->addChild(iBtn);
        }
    }

    // cycling rating icon
    {
        auto ratingMenu = CCMenu::create();
        ratingMenu->setPosition({0.f, 0.f});
        m_mainLayer->addChild(ratingMenu);

        // container holds all 5 tier sprites stacked; we toggle visibility
        auto container = CCNode::create();
        container->setContentSize({26.f, 26.f});
        container->setAnchorPoint({0.5f, 0.5f});

        m_ratingSprites.reserve(static_cast<int>(RatingTier::Count));
        for (int t = 0; t < static_cast<int>(RatingTier::Count); ++t) {
            auto spr = createRatingSprite(t);
            if (spr) {
                spr->setPosition({13.f, 13.f});
                container->addChild(spr);
            }
            m_ratingSprites.push_back(spr);
        }

        auto ratingBtn = CCMenuItemSpriteExtra::create(
            container, this,
            menu_selector(ForYouPreferencesPopup::onRatingCycle));
        ratingBtn->setPosition({28.f, bottomY + 4.f});
        ratingMenu->addChild(ratingBtn);

        // tier name label to the right of the icon
        m_ratingName = CCLabelBMFont::create(
            Localization::get().getString("foryou.prefs_rating_star").c_str(),
            "bigFont.fnt");
        m_ratingName->setScale(0.34f);
        m_ratingName->setAnchorPoint({0.f, 0.5f});
        m_ratingName->setPosition({46.f, bottomY + 4.f});
        m_mainLayer->addChild(m_ratingName);
    }

    // confirm button on the right
    {
        auto confirmSpr = ButtonSprite::create(
            Localization::get().getString("foryou.prefs_confirm").c_str(),
            60, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.6f);
        if (confirmSpr) confirmSpr->setScale(0.70f);
        auto confirmBtn = CCMenuItemSpriteExtra::create(confirmSpr, this,
            menu_selector(ForYouPreferencesPopup::onConfirm));
        confirmBtn->setPosition({POPUP_W - 42.f, bottomY + 4.f});
        m_buttonMenu->addChild(confirmBtn);
    }

    // initial visual state
    refreshDifficultyButtons();
    refreshDemonButtons();
    refreshGameModeButtons();
    refreshLengthButtons();
    refreshRatingTier();

    return true;
}

// ── input callbacks ──────────────────────────────────────
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

void ForYouPreferencesPopup::onConfirm(CCObject*) {
    float platformerRatio = 0.f;
    if (m_gameMode == 1) platformerRatio = 1.f;
    else if (m_gameMode == 2) platformerRatio = 0.5f;

    // progressive mapping — each tier implies the previous ones
    bool const starRated = m_ratingTier >= static_cast<int>(RatingTier::StarRated);
    bool const featured  = m_ratingTier >= static_cast<int>(RatingTier::Featured);
    bool const epic      = m_ratingTier >= static_cast<int>(RatingTier::Epic);

    ForYouTracker::get().seedPreferences(
        m_difficulty, platformerRatio,
        m_length, starRated, featured, epic, m_demonDiff
    );
    ForYouTracker::get().save();

    this->onClose(nullptr);
    if (m_onConfirm) m_onConfirm();
}

// ── visual refreshers ────────────────────────────────────
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
    if (m_demonRow) m_demonRow->setVisible(m_difficulty == 60);
}
