#include "TagPreferencesPopup.hpp"

#include "LevelTagsGatePopup.hpp"
#include "../services/LevelTagsClient.hpp"
#include "../services/TasteProfile.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::foryou {

namespace {

constexpr float kPopupWidth = 420.f;
constexpr float kPopupHeight = 280.f;
constexpr float kScrollWidth = 388.f;
constexpr float kScrollHeight = 178.f;
constexpr float kChipHeight = 19.f;
constexpr float kChipScale = 0.55f;
constexpr float kChipGap = 5.f;
constexpr float kRowGap = 4.f;

constexpr ccColor3B kNeutralTint = {96, 100, 118};
constexpr ccColor3B kAvoidTint = {198, 62, 78};

char const* categoryLabelKey(TagCategory category) {
    switch (category) {
        case TagCategory::Style:    return "foryou.tags_cat_style";
        case TagCategory::Theme:    return "foryou.tags_cat_theme";
        case TagCategory::Meta:     return "foryou.tags_cat_meta";
        case TagCategory::Gameplay: return "foryou.tags_cat_gameplay";
        default:                    return "foryou.tags_cat_style";
    }
}

} // namespace

TagPreferencesPopup* TagPreferencesPopup::create() {
    auto ret = new TagPreferencesPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TagPreferencesPopup::init() {
    if (!Popup::init(kPopupWidth, kPopupHeight)) return false;
    paimon::markDynamicPopup(this);

    auto& loc = Localization::get();
    auto const content = m_mainLayer->getContentSize();
    float const cx = content.width / 2.f;

    this->setTitle(loc.getString("foryou.tags_popup_title").c_str());

    auto hint = CCLabelBMFont::create(loc.getString("foryou.tags_popup_hint").c_str(), "chatFont.fnt");
    hint->setScale(0.42f);
    hint->setOpacity(160);
    hint->setPosition({cx, content.height - 46.f});
    m_mainLayer->addChild(hint);

    // State legend, so the three-way chip cycle is discoverable without
    // trial and error.
    {
        auto legend = CCNode::create();
        legend->setContentSize({kScrollWidth, 14.f});
        legend->setPosition({cx, content.height - 62.f});
        m_mainLayer->addChild(legend);

        float x = -86.f;
        auto addKey = [&](ccColor3B color, char const* labelKey) {
            if (auto* swatch = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
                swatch->setContentSize({12.f, 12.f});
                swatch->setAnchorPoint({0.f, 0.5f});
                swatch->setPosition({x, 0.f});
                swatch->setColor(color);
                swatch->setOpacity(230);
                legend->addChild(swatch);
            }
            auto label = CCLabelBMFont::create(
                Localization::get().getString(labelKey).c_str(), "chatFont.fnt");
            label->setScale(0.34f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({x + 16.f, 0.f});
            label->setOpacity(175);
            legend->addChild(label);
            x += 16.f + label->getScaledContentSize().width + 14.f;
        };
        addKey({120, 210, 120}, "foryou.tags_legend_love");
        addKey(kAvoidTint, "foryou.tags_legend_avoid");
        addKey(kNeutralTint, "foryou.tags_legend_neutral");
    }

    m_scroll = ScrollLayer::create({kScrollWidth, kScrollHeight});
    m_scroll->setPosition({cx - kScrollWidth / 2.f, 34.f});
    m_mainLayer->addChild(m_scroll);

    if (auto* scrollBg = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
        scrollBg->setContentSize({kScrollWidth, kScrollHeight});
        scrollBg->setAnchorPoint({0.f, 0.f});
        scrollBg->setPosition({cx - kScrollWidth / 2.f, 34.f});
        scrollBg->setColor({22, 17, 12});
        scrollBg->setOpacity(150);
        m_mainLayer->addChild(scrollBg, -1);
    }

    // The rounded comment borders GD frames all of its lists with.
    if (auto* borders = geode::ListBorders::create()) {
        borders->setContentSize({kScrollWidth, kScrollHeight});
        borders->setPosition({cx, 34.f + kScrollHeight / 2.f});
        m_mainLayer->addChild(borders, 3);
    }

    buildContent();

    if (!LevelTagsClient::isAvailable()) return true;

    // The catalog is what gives us tag names and their colours; fetch it if the
    // feed hasn't already.
    if (!LevelTagsClient::get().hasCatalog()) {
        Ref<TagPreferencesPopup> self = this;
        LevelTagsClient::get().loadCatalog([self](bool ok) {
            if (!ok || !self->getParent()) return;
            self->buildContent();
        });
    }

    return true;
}

void TagPreferencesPopup::buildContent() {
    if (!m_scroll) return;

    m_chips.clear();
    m_scroll->m_contentLayer->removeAllChildren();

    auto& loc = Localization::get();
    auto& client = LevelTagsClient::get();

    if (!LevelTagsClient::isAvailable() || !client.hasCatalog()) {
        auto message = CCLabelBMFont::create(
            loc.getString(LevelTagsClient::isAvailable()
                ? "foryou.tags_popup_loading"
                : "foryou.tags_popup_unavailable").c_str(),
            "chatFont.fnt");
        message->setScale(0.55f);
        message->setOpacity(170);
        message->setPosition({kScrollWidth / 2.f, kScrollHeight / 2.f + 14.f});

        m_scroll->m_contentLayer->setContentSize({kScrollWidth, kScrollHeight});
        m_scroll->m_contentLayer->addChild(message);

        if (!LevelTagsClient::isAvailable()) {
            auto menu = CCMenu::create();
            menu->setPosition({kScrollWidth / 2.f, kScrollHeight / 2.f - 16.f});
            m_scroll->m_contentLayer->addChild(menu);

            auto spr = ButtonSprite::create(
                loc.getString("foryou.tags_gate_install").c_str(),
                90, true, "bigFont.fnt", "GJ_button_01.png", 24.f, 0.6f);
            if (spr) {
                spr->setScale(0.65f);
                menu->addChild(CCMenuItemSpriteExtra::create(
                    spr, this, menu_selector(TagPreferencesPopup::onInstallLevelTags)));
            }
        }

        m_scroll->moveToTop();
        return;
    }

    // Measure everything first so the content layer can be sized before the
    // chips are placed — the scroll layer works top-down.
    struct Row { std::vector<TagInfo> tags; };
    struct Section { TagCategory category; std::vector<Row> rows; };

    std::vector<Section> sections;
    for (auto category : {TagCategory::Gameplay, TagCategory::Style,
                          TagCategory::Theme, TagCategory::Meta}) {
        auto tags = client.catalogFor(category);
        if (tags.empty()) continue;

        Section section{category, {}};
        Row row;
        float rowWidth = 0.f;
        for (auto const& info : tags) {
            auto probe = ButtonSprite::create(
                info.name.c_str(), 0, false, "bigFont.fnt", "GJ_button_04.png", 20.f, 0.42f);
            if (!probe) continue;
            probe->setScale(kChipScale);
            float chipWidth = probe->getScaledContentSize().width;

            if (rowWidth + chipWidth > kScrollWidth - 16.f && !row.tags.empty()) {
                section.rows.push_back(std::move(row));
                row = Row{};
                rowWidth = 0.f;
            }
            row.tags.push_back(info);
            rowWidth += chipWidth + kChipGap;
        }
        if (!row.tags.empty()) section.rows.push_back(std::move(row));
        sections.push_back(std::move(section));
    }

    float totalHeight = 8.f;
    for (auto const& section : sections) {
        totalHeight += 18.f + section.rows.size() * (kChipHeight + kRowGap);
    }
    totalHeight = std::max(totalHeight, kScrollHeight);

    auto layer = m_scroll->m_contentLayer;
    layer->setContentSize({kScrollWidth, totalHeight});

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({kScrollWidth, totalHeight});
    layer->addChild(menu, 5);

    float y = totalHeight - 16.f;
    for (auto const& section : sections) {
        auto header = CCLabelBMFont::create(
            loc.getString(categoryLabelKey(section.category)).c_str(), "goldFont.fnt");
        header->setScale(0.36f);
        header->setAnchorPoint({0.f, 0.5f});
        header->setPosition({8.f, y});
        layer->addChild(header, 6);
        y -= 16.f;

        for (auto const& row : section.rows) {
            float x = 8.f;
            for (auto const& info : row.tags) {
                auto pill = ButtonSprite::create(
                    info.name.c_str(), 0, false, "bigFont.fnt", "GJ_button_04.png", 20.f, 0.42f);
                if (!pill) continue;
                pill->setScale(kChipScale);
                float chipWidth = pill->getScaledContentSize().width;

                auto button = CCMenuItemSpriteExtra::create(
                    pill, this, menu_selector(TagPreferencesPopup::onTagToggle));
                button->setAnchorPoint({0.f, 0.5f});
                button->setPosition({x, y});
                button->setUserObject(CCString::create(info.name));
                button->m_scaleMultiplier = 1.06f;
                PaimonButtonHighlighter::registerButton(button);
                menu->addChild(button);

                Chip chip{info.name, info.color, pill};
                refreshChip(chip);
                m_chips.push_back(std::move(chip));

                x += chipWidth + kChipGap;
            }
            y -= kChipHeight + kRowGap;
        }
    }

    m_scroll->moveToTop();
}

void TagPreferencesPopup::refreshChip(Chip const& chip) const {
    if (!chip.pill) return;

    int vote = TasteProfile::get().pinnedTagVote(chip.tag);
    if (vote > 0) {
        // Loved: the tag's own Level Tags colour, at full strength.
        chip.pill->setColor(chip.color);
        chip.pill->setOpacity(255);
    } else if (vote < 0) {
        chip.pill->setColor(kAvoidTint);
        chip.pill->setOpacity(235);
    } else {
        chip.pill->setColor(kNeutralTint);
        chip.pill->setOpacity(150);
    }
}

void TagPreferencesPopup::onTagToggle(CCObject* sender) {
    auto button = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!button) return;
    auto tagObject = typeinfo_cast<CCString*>(button->getUserObject());
    if (!tagObject) return;

    std::string tag = tagObject->getCString();
    auto& profile = TasteProfile::get();

    // neutral -> love -> avoid -> neutral
    int vote = profile.pinnedTagVote(tag);
    int next = vote == 0 ? 1 : (vote > 0 ? -1 : 0);
    profile.setPinnedTag(tag, next);
    profile.save();

    for (auto const& chip : m_chips) {
        if (chip.tag == tag) refreshChip(chip);
    }
}

void TagPreferencesPopup::onInstallLevelTags(CCObject*) {
    LevelTagsGatePopup::openModPage();
}

} // namespace paimon::foryou
