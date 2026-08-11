#include "IconDetailPopup.hpp"

#include "IconSetPreview.hpp"
#include "../IconUnlockInfo.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <string>

using namespace geode::prelude;

namespace paimon::iconcopy {

namespace {

constexpr float kWidth = 440.f;
constexpr float kHeight = 250.f;
constexpr float kPreviewX = 18.f;
constexpr float kPreviewY = 28.f;
constexpr float kPreviewWidth = 118.f;
constexpr float kPreviewHeight = 176.f;
constexpr float kInfoX = 148.f;
constexpr float kInfoWidth = kWidth - kInfoX - 18.f;
constexpr float kArtBox = 80.f;
constexpr float kStatArt = 18.f;

void fitTo(CCNode* node, float target) {
    if (!node) return;
    float const dim = std::max(node->getContentWidth(), node->getContentHeight());
    if (dim > 0.f) node->setScale(target / dim);
}

CCNode* makeStatChip(UnlockRequirement const& requirement) {
    if (!requirement.drawable()) return nullptr;

    auto* chip = CCNode::create();
    float x = 8.f;

    if (auto* art = paimon::SpriteHelper::safeCreateWithFrameName(requirement.sprite.c_str())) {
        fitTo(art, kStatArt);
        art->setPosition({x + kStatArt / 2.f, 11.f});
        chip->addChild(art);
        x += kStatArt + 5.f;
    }

    if (requirement.amount > 0) {
        std::string const text = requirement.sprite.empty()
            ? fmt::format("TARGET {}", requirement.amount)
            : std::to_string(requirement.amount);
        auto* count = CCLabelBMFont::create(
            text.c_str(), requirement.sprite.empty() ? "chatFont.fnt" : "bigFont.fnt");
        count->setAnchorPoint({0.f, 0.5f});
        count->setScale(requirement.sprite.empty() ? 0.42f : 0.36f);
        count->setPosition({x, 11.f});
        chip->addChild(count);
        x += count->getScaledContentSize().width;
    }

    float const width = x + 8.f;
    if (auto* bg = paimon::SpriteHelper::createColorPanel(
            width, 22.f, {73, 50, 24}, 230, 6.f)) {
        chip->addChild(bg, -1);
    }
    chip->setContentSize({width, 22.f});
    return chip;
}

CCNode* makeStatusPill(bool owned) {
    auto* pill = CCNode::create();
    auto* label = CCLabelBMFont::create(owned ? "UNLOCKED" : "LOCKED", "chatFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setScale(0.42f);

    constexpr float iconSize = 11.f;
    float const width = 8.f + iconSize + 5.f + label->getScaledContentWidth() + 8.f;
    ccColor3B const color = owned ? ccColor3B{45, 135, 77} : ccColor3B{145, 55, 55};
    if (auto* bg = paimon::SpriteHelper::createColorPanel(width, 20.f, color, 235, 8.f)) {
        pill->addChild(bg);
    }

    auto const frame = owned ? "GJ_checkOn_001.png" : "GJ_lock_001.png";
    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
        fitTo(icon, iconSize);
        icon->setPosition({8.f + iconSize / 2.f, 10.f});
        pill->addChild(icon, 1);
    }

    label->setPosition({8.f + iconSize + 5.f, 10.f});
    pill->addChild(label, 1);
    pill->setContentSize({width, 20.f});
    return pill;
}

// Smaller sibling of the status pill, for a state that is a footnote to it.
CCNode* makeTinyPill(char const* text, ccColor3B color) {
    auto* pill = CCNode::create();
    auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setScale(0.34f);

    float const width = label->getScaledContentWidth() + 12.f;
    if (auto* bg = paimon::SpriteHelper::createColorPanel(width, 15.f, color, 235, 6.f)) {
        pill->addChild(bg);
    }

    label->setPosition({width / 2.f, 7.5f});
    pill->addChild(label, 1);
    pill->setContentSize({width, 15.f});
    return pill;
}

void addSectionLabel(CCNode* parent, char const* text, CCPoint const& position) {
    auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setScale(0.34f);
    label->setColor({155, 155, 175});
    label->setPosition(position);
    parent->addChild(label, 1);
}

constexpr char kBarSprite[] = "GJ_progressBar_001.png";
constexpr float kBarHeight = 13.f;
constexpr float kBarOutline = 1.5f;
constexpr float kBarInset = 1.5f;

ccColor3B fillColor(bool owned) {
    return owned ? ccColor3B{83, 214, 121} : ccColor3B{255, 181, 61};
}

// One copy of GD's capsule, cropped from the left to `drawn` points the way
// PlayLayer grows m_progressFill, then squashed into a track box.
CCSprite* makeCapsule(CCSize const& tex, float drawn, float boxWidth, float boxHeight) {
    auto* spr = paimon::SpriteHelper::safeCreate(kBarSprite);
    if (!spr) return nullptr;

    spr->setTextureRect(
        {0.f, 0.f, tex.width * std::clamp(drawn / boxWidth, 0.f, 1.f), tex.height});
    spr->setAnchorPoint({0.f, 0.5f});
    spr->setScaleX(boxWidth / tex.width);
    spr->setScaleY(boxHeight / tex.height);
    return spr;
}

// Flat rounded rects, for texture packs that drop the capsule out from under us.
CCNode* makeFlatProgressBar(float width, int value, bool owned) {
    auto* bar = CCNode::create();
    bar->setContentSize({width, kBarHeight});

    if (auto* groove = paimon::SpriteHelper::createColorPanel(
            width, kBarHeight, {14, 16, 23}, 245, kBarHeight / 2.f)) {
        bar->addChild(groove);
    }

    float const trackH = kBarHeight - kBarInset * 2.f;
    float const filled = (width - kBarInset * 2.f) * static_cast<float>(value) / 100.f;
    if (value > 0) {
        if (auto* fill = paimon::SpriteHelper::createColorPanel(
                std::max(filled, trackH), trackH, fillColor(owned), 255, trackH / 2.f)) {
            fill->setPosition({kBarInset, kBarInset});
            bar->addChild(fill, 1);
        }
    }
    return bar;
}

// GD's own progress bar is one white capsule drawn twice: PlayLayer keeps a
// dark m_progressBar for the groove and a tinted m_progressFill over it. Same
// recipe, plus the black rim and the gloss that let it sit on a card.
CCNode* makeProgressBar(float width, int progress, bool owned) {
    int const value = std::clamp(progress, 0, 100);

    auto* groove = paimon::SpriteHelper::safeCreate(kBarSprite);
    auto const tex = groove ? groove->getContentSize() : CCSize{};
    if (!groove || tex.width <= 0.f || tex.height <= 0.f) {
        return makeFlatProgressBar(width, value, owned);
    }

    auto* bar = CCNode::create();
    bar->setContentSize({width, kBarHeight});
    float const midY = kBarHeight / 2.f;

    auto centred = [&](CCSprite* spr, float w, float h) {
        spr->setScaleX(w / tex.width);
        spr->setScaleY(h / tex.height);
        spr->setPosition({width / 2.f, midY});
    };

    if (auto* rim = paimon::SpriteHelper::safeCreate(kBarSprite)) {
        rim->setColor({0, 0, 0});
        centred(rim, width + kBarOutline * 2.f, kBarHeight + kBarOutline * 2.f);
        bar->addChild(rim, 0);
    }

    groove->setColor({14, 16, 23});
    centred(groove, width, kBarHeight);
    bar->addChild(groove, 1);

    float const trackW = width - kBarInset * 2.f;
    float const trackH = kBarHeight - kBarInset * 2.f;

    // Quarter marks stay under the fill, so a finished bar goes solid.
    for (int step = 1; step < 4; ++step) {
        if (auto* tick = paimon::SpriteHelper::createColorPanel(
                1.f, trackH, {255, 255, 255}, 38, 0.f)) {
            tick->setPosition({kBarInset + trackW * static_cast<float>(step) / 4.f, kBarInset});
            bar->addChild(tick, 2);
        }
    }

    if (value <= 0) return bar;

    // Never thinner than the cap is round, so 1% still reads as a nub.
    float const filled = std::max(trackW * static_cast<float>(value) / 100.f, trackH);
    if (auto* fill = makeCapsule(tex, filled, trackW, trackH)) {
        fill->setColor(fillColor(owned));
        fill->setPosition({kBarInset, midY});
        bar->addChild(fill, 3);
    }

    // Highlight along the top of the fill, the shine GD puts on its buttons.
    if (auto* gloss = makeCapsule(tex, filled - 3.f, trackW, trackH * 0.42f)) {
        gloss->setOpacity(60);
        gloss->setPosition({kBarInset + 1.5f, midY + trackH * 0.22f});
        bar->addChild(gloss, 4);
    }
    return bar;
}

std::string statusNote(UnlockInfo const& info) {
    if (info.owned) {
        return info.equipped
            ? "Unlocked, and this is the one your account wears right now."
            : "Unlocked on this save, so the Use button will put it on.";
    }
    if (info.progress >= 100) {
        return "Objective complete, but this icon is still locked on this save.";
    }
    if (info.progress >= 0) return "Finish the objective above to unlock this icon.";
    if (info.source == "Unknown") return "The game exposes no source or tracked progress for this icon.";
    return "This unlock method does not expose tracked progress.";
}

// The line under the bar. The shop fills it in with what you are carrying; the
// rest of the time it only speaks up when there is no bar to explain.
std::string progressHint(UnlockInfo const& info) {
    if (!info.hint.empty()) return info.hint;
    if (info.progress < 0) return "No percentage is exposed for this unlock.";
    return {};
}

}  // anonymous namespace

IconDetailPopup* IconDetailPopup::create(IconSet const& set, IconType type) {
    auto* popup = new IconDetailPopup();
    if (popup->init(set, type)) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool IconDetailPopup::init(IconSet const& set, IconType type) {
    if (!Popup::init(kWidth, kHeight)) return false;

    int const iconID = set.iconFor(type);
    auto const info = unlockInfoFor(iconID, type);

    this->setTitle(fmt::format("{} #{}", gamemodeName(type), iconID));
    this->setID("icon-detail-popup"_spr);
    paimon::markDynamicPopup(this);

    if (auto* card = paimon::SpriteHelper::createDarkPanel(
            kPreviewWidth, kPreviewHeight, 80, 9.f)) {
        card->setID("icon-preview-card"_spr);
        card->setPosition({kPreviewX, kPreviewY});
        m_mainLayer->addChild(card);
    }

    addSectionLabel(m_mainLayer, "ICON PREVIEW", {kPreviewX + 10.f, 191.f});

    constexpr float well = 98.f;
    constexpr float wellX = kPreviewX + (kPreviewWidth - well) / 2.f;
    constexpr float wellY = 76.f;
    if (auto* plate = paimon::SpriteHelper::createColorPanel(
            well, well, {21, 23, 31}, 235, 10.f)) {
        plate->setID("icon-art-plate"_spr);
        plate->setPosition({wellX, wellY});
        m_mainLayer->addChild(plate);
    }

    if (auto* art = makePreview(set, type, kArtBox)) {
        art->setID("icon-art"_spr);
        art->setPosition({kPreviewX + kPreviewWidth / 2.f, wellY + well / 2.f});
        m_mainLayer->addChild(art, 1);
    }

    auto* mode = CCLabelBMFont::create(gamemodeName(type), "bigFont.fnt");
    mode->limitLabelWidth(kPreviewWidth - 18.f, 0.42f, 0.22f);
    mode->setPosition({kPreviewX + kPreviewWidth / 2.f, 58.f});
    m_mainLayer->addChild(mode, 1);

    // Where this one sits in the gamemode's catalogue, which says more than the
    // bare id: #15 of 169 tells you how much of the tab is still ahead of it.
    auto const place = info.total > 0
        ? fmt::format("ICON {} OF {}", iconID, info.total)
        : fmt::format("ICON ID  #{}", iconID);
    auto* id = CCLabelBMFont::create(place.c_str(), "chatFont.fnt");
    id->limitLabelWidth(kPreviewWidth - 16.f, 0.36f, 0.2f);
    id->setColor({170, 170, 190});
    id->setPosition({kPreviewX + kPreviewWidth / 2.f, 42.f});
    m_mainLayer->addChild(id, 1);

    constexpr float summaryY = 168.f;
    constexpr float summaryH = 36.f;
    if (auto* summary = paimon::SpriteHelper::createDarkPanel(
            kInfoWidth, summaryH, 75, 7.f)) {
        summary->setID("unlock-summary-card"_spr);
        summary->setPosition({kInfoX, summaryY});
        m_mainLayer->addChild(summary);
    }

    addSectionLabel(m_mainLayer, "UNLOCK METHOD", {kInfoX + 10.f, summaryY + 26.f});

    auto* state = makeStatusPill(info.owned);
    float const stateX = kInfoX + kInfoWidth - state->getContentWidth() - 8.f;
    state->setPosition({stateX, summaryY + 8.f});
    m_mainLayer->addChild(state, 2);

    // Owning an icon and wearing it are different answers, so they get their
    // own badges instead of one pill trying to say both.
    float sourceEnd = stateX;
    if (info.equipped) {
        auto* worn = makeTinyPill("IN USE", {56, 108, 176});
        sourceEnd = stateX - worn->getContentWidth() - 5.f;
        worn->setPosition({sourceEnd, summaryY + 10.5f});
        m_mainLayer->addChild(worn, 2);
    }

    auto* source = CCLabelBMFont::create(info.source.c_str(), "goldFont.fnt");
    source->setAnchorPoint({0.f, 0.5f});
    source->limitLabelWidth(std::max(sourceEnd - kInfoX - 20.f, 40.f), 0.52f, 0.24f);
    source->setPosition({kInfoX + 10.f, summaryY + 10.f});
    m_mainLayer->addChild(source, 1);

    auto* name = CCLabelBMFont::create(info.name.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->limitLabelWidth(kInfoWidth, 0.47f, 0.24f);
    name->setPosition({kInfoX, 155.f});
    m_mainLayer->addChild(name, 1);

    constexpr float requirementY = 92.f;
    constexpr float requirementH = 50.f;
    if (auto* requirement = paimon::SpriteHelper::createDarkPanel(
            kInfoWidth, requirementH, 65, 7.f)) {
        requirement->setID("unlock-requirement-card"_spr);
        requirement->setPosition({kInfoX, requirementY});
        m_mainLayer->addChild(requirement);
    }
    addSectionLabel(m_mainLayer, "REQUIREMENT", {kInfoX + 10.f, requirementY + 39.f});

    if (auto* chip = makeStatChip(info.requirement)) {
        chip->setPosition({kInfoX + kInfoWidth - chip->getContentWidth() - 8.f,
                           requirementY + 25.f});
        m_mainLayer->addChild(chip, 2);
    }

    constexpr float detailScale = 0.38f;
    auto* detail = CCLabelBMFont::create(
        info.detail.c_str(), "chatFont.fnt",
        (kInfoWidth - 20.f) / detailScale, kCCTextAlignmentLeft);
    detail->setAnchorPoint({0.f, 0.5f});
    detail->setScale(detailScale);
    detail->setColor({195, 195, 213});
    detail->setPosition({kInfoX + 10.f, requirementY + 14.f});
    m_mainLayer->addChild(detail, 1);

    constexpr float progressY = 36.f;
    constexpr float progressH = 50.f;
    if (auto* progressCard = paimon::SpriteHelper::createDarkPanel(
            kInfoWidth, progressH, 65, 7.f)) {
        progressCard->setID("unlock-progress-card"_spr);
        progressCard->setPosition({kInfoX, progressY});
        m_mainLayer->addChild(progressCard);
    }
    addSectionLabel(m_mainLayer, "PROGRESS", {kInfoX + 10.f, progressY + 39.f});

    auto const progressText = info.progress >= 0
        ? fmt::format("{}%", info.progress)
        : std::string{"NOT TRACKED"};
    auto* progressLabel = CCLabelBMFont::create(progressText.c_str(), "chatFont.fnt");
    progressLabel->setAnchorPoint({1.f, 0.5f});
    progressLabel->setScale(0.38f);
    progressLabel->setColor(info.progress >= 0 ? ccColor3B{255, 210, 120}
                                               : ccColor3B{155, 155, 175});
    progressLabel->setPosition({kInfoX + kInfoWidth - 10.f, progressY + 39.f});
    m_mainLayer->addChild(progressLabel, 1);

    // The bar drops down a row to make space when there is a line to sit under
    // it, and centres itself in the card when there is not.
    auto const hint = progressHint(info);
    if (info.progress >= 0) {
        auto* bar = makeProgressBar(kInfoWidth - 22.f, info.progress, info.owned);
        bar->setID("unlock-progress-bar"_spr);
        bar->setPosition({kInfoX + 11.f, progressY + (hint.empty() ? 12.f : 17.f)});
        m_mainLayer->addChild(bar, 1);
    }

    if (!hint.empty()) {
        auto* line = CCLabelBMFont::create(hint.c_str(), "chatFont.fnt");
        line->setAnchorPoint({0.f, 0.5f});
        line->limitLabelWidth(kInfoWidth - 20.f, 0.34f, 0.26f);
        line->setColor({175, 175, 193});
        line->setPosition({kInfoX + 10.f, progressY + (info.progress >= 0 ? 8.f : 16.f)});
        m_mainLayer->addChild(line, 1);
    }

    auto* note = CCLabelBMFont::create(statusNote(info).c_str(), "chatFont.fnt");
    note->setAnchorPoint({0.f, 0.5f});
    note->limitLabelWidth(kInfoWidth, 0.34f, 0.28f);
    note->setColor(info.owned ? ccColor3B{125, 220, 150} : ccColor3B{185, 185, 203});
    note->setPosition({kInfoX, 27.f});
    m_mainLayer->addChild(note, 1);

    return true;
}

}  // namespace paimon::iconcopy
