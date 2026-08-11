#include "LevelStatsPopup.hpp"
#include "ExtendedInfoPopup.hpp"
#include "InfoBlocks.hpp"
#include "StatsChartNode.hpp"
#include "../InfoModule.hpp"
#include "../services/LevelFacts.hpp"
#include "../../thumbnails/services/ThumbnailLoader.hpp"
#include "../../../blur/BlurSystem.hpp"
#include "../../../framework/ModEvents.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Shaders.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <algorithm>

using namespace geode::prelude;
using namespace paimon::info::blocks;

namespace paimon::info {

namespace {

constexpr float kPopupW = 375.f;
constexpr float kPopupH = 280.f;
constexpr float kMargin = 8.f;
constexpr float kInnerW = kPopupW - kMargin * 2.f;

constexpr float kTileGap = 4.f;
constexpr float kTileH = 34.f;
constexpr float kTileRowY = 211.f;
constexpr float kChartH = 52.f;

constexpr float kDeathHeaderY = 160.f;
constexpr float kDeathChartY = 100.f;
constexpr float kJumpHeaderY = 87.f;
constexpr float kJumpChartY = 27.f;
constexpr float kToggleY = 15.f;

// Five bars span each 20% axis interval.
constexpr int kDeathBins = 25;
// Keep the thumbnail recognizable behind the panels.
constexpr float kBlurIntensity = 2.f;

constexpr ccColor3B kJumpColor{120, 190, 255};

// Shared percent binning keeps bars and highlight aligned.
int deathBin(int percent) {
    return std::clamp(percent * kDeathBins / 100, 0, kDeathBins - 1);
}

std::vector<float> binDeaths(std::array<uint32_t, kPercentBuckets> const& buckets) {
    std::vector<float> out(kDeathBins, 0.f);
    for (int i = 0; i < kPercentBuckets; i++) {
        out[deathBin(i)] += static_cast<float>(buckets[i]);
    }
    return out;
}

}

LevelStatsPopup* LevelStatsPopup::create(GJGameLevel* level) {
    auto ret = new LevelStatsPopup();
    if (ret && ret->init(level)) { ret->autorelease(); return ret; }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool LevelStatsPopup::init(GJGameLevel* level) {
    if (!level) return false;
    if (!Popup::init(kPopupW, kPopupH)) return false;

    paimon::markDynamicPopup(this);

    m_level = level;
    if (auto const* stored = ProgressTracker::get().find(level->m_levelID.value())) {
        m_progress = *stored;
    }

    this->setTitle(std::string(level->m_levelName).c_str(), "goldFont.fnt", 0.5f, 17.f);
    if (m_title) m_title->limitLabelWidth(kPopupW - 90.f, 0.5f, 0.1f);

    if (auto spr = SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, menu_selector(LevelStatsPopup::onFullInfo));
        btn->setPosition({kPopupW - 18.f, kPopupH - 18.f});
        m_buttonMenu->addChild(btn);
    }

    buildTiles();

    m_chartLayer = CCNode::create();
    m_chartLayer->setContentSize({kPopupW, kPopupH});
    m_mainLayer->addChild(m_chartLayer, 3);
    buildCharts();

    requestThumbnail();
    return true;
}

CCNode* LevelStatsPopup::makeTile(float width, float height, std::vector<char const*> const& frames,
                                  std::string const& value, char const* caption,
                                  ccColor3B valueColor) {
    auto tile = makeBlock(width, height);

    float textX = 10.f;
    if (auto icon = firstFrame(frames)) {
        icon->setScale(0.42f);
        icon->setAnchorPoint({0.f, 0.5f});
        icon->setPosition({8.f, height / 2.f});
        tile->addChild(icon);
        textX = 8.f + icon->getScaledContentSize().width + 5.f;
    }

    addText(tile, value.c_str(), "bigFont.fnt", 0.38f, valueColor,
            {textX, height - 14.f}, {0.f, 0.5f}, width - textX - 6.f);
    addText(tile, caption, "chatFont.fnt", 0.32f, kLabel,
            {textX, 11.f}, {0.f, 0.5f}, width - textX - 6.f);
    return tile;
}

void LevelStatsPopup::buildTiles() {
    auto* level = m_level.data();

    // GD counts practice attempts in this total; the next tile shows their share.
    int attempts = std::max(level->m_attempts.value(),
                            m_progress.attempts + m_progress.practiceAttempts);
    int bestNormal = std::max(level->m_normalPercent.value(), m_progress.bestNormal);
    int bestPractice = std::max(level->m_practicePercent, m_progress.bestPractice);
    // Use GD's jump total only when the mod has no level-specific value.
    bool trackedJumps = m_progress.jumpsNormal + m_progress.jumpsPractice > 0;
    int jumpsNormal = trackedJumps ? m_progress.jumpsNormal : level->m_jumps.value();

    struct TileDef {
        std::vector<char const*> frames;
        std::string value;
        char const* caption;
        int amount;   // 0 → drawn dimmed, so an empty stat is not shouting
    };

    std::vector<TileDef> const tiles = {
        {{"GJ_replayBtn_001.png"}, formatThousands(attempts), "intentos totales", attempts},
        {{"GJ_completesIcon_001.png"}, fmt::format("{}%", bestNormal), "best run", bestNormal},
        {{"PBtn_Jump_001.png", "GJ_arrow_02_001.png"}, formatThousands(jumpsNormal),
         "saltos normal", jumpsNormal},
        {{"GJ_practiceBtn_001.png"}, formatThousands(m_progress.practiceAttempts),
         "intentos practica", m_progress.practiceAttempts},
        {{"GJ_practiceBtn_001.png"}, fmt::format("{}%", bestPractice), "best practica",
         bestPractice},
        {{"PBtn_Jump_001.png", "GJ_arrow_02_001.png"},
         formatThousands(m_progress.jumpsPractice), "saltos practica", m_progress.jumpsPractice},
    };

    float tileW = (kInnerW - kTileGap * 2.f) / 3.f;
    for (size_t i = 0; i < tiles.size(); i++) {
        auto const& def = tiles[i];
        ccColor3B color = def.amount > 0 ? (i == 1 ? kAccent : kValue) : ccColor3B{125, 125, 125};
        auto* tile = makeTile(tileW, kTileH, def.frames, def.value, def.caption, color);
        float x = kMargin + static_cast<float>(i % 3) * (tileW + kTileGap);
        float y = i < 3u ? kTileRowY : kTileRowY - kTileH - kTileGap;
        tile->setPosition({x, y});
        m_mainLayer->addChild(tile, 3);

        tile->setScale(0.86f);
        tile->runAction(CCSequence::create(
            CCDelayTime::create(0.03f * static_cast<float>(i)),
            CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.f)),
            nullptr));
    }
}

void LevelStatsPopup::buildCharts() {
    if (!m_chartLayer) return;
    m_chartLayer->removeAllChildren();

    auto addChart = [&](float headerY, float chartBottom, char const* title,
                        std::string const& right, std::vector<float> const& values,
                        ChartOptions const& options) {
        addText(m_chartLayer, title, "chatFont.fnt", 0.46f, kAccent,
                {kMargin, headerY}, {0.f, 0.5f}, kInnerW * 0.44f);
        addText(m_chartLayer, right.c_str(), "chatFont.fnt", 0.4f, kValue,
                {kPopupW - kMargin, headerY}, {1.f, 0.5f}, kInnerW * 0.54f);

    // The chart owns its empty state.
        if (auto* chart = StatsChartNode::create(values, {kInnerW, kChartH}, options)) {
            chart->setPosition({kPopupW / 2.f, chartBottom + kChartH / 2.f});
            m_chartLayer->addChild(chart);
        }
    };

    std::string noData = moduleEnabled("info-mod-progress")
        ? "Sin datos todavia: juega el nivel"
        : "Activa Progress Tracking para verlo";

    int deaths = m_progress.totalDeaths(m_practice);
    auto worst = m_progress.worstDeath(m_practice);

    ChartOptions deathOpts;
    deathOpts.heat = true;
    deathOpts.axisLabels = {"0%", "20%", "40%", "60%", "80%", "100%"};
    deathOpts.highlight = worst.percent >= 0 ? deathBin(worst.percent) : -1;
    deathOpts.emptyText = noData;

    // Reference line marks progress through the level.
    int best = m_practice
        ? std::max(m_level->m_practicePercent, m_progress.bestPractice)
        : std::max(m_level->m_normalPercent.value(), m_progress.bestNormal);
    if (best > 0 && best < 100) deathOpts.marker = static_cast<float>(best) / 100.f;

    addChart(kDeathHeaderY, kDeathChartY,
             m_practice ? "Donde mueres (practica)" : "Donde mueres",
             deaths > 0
                 ? fmt::format("{} muertes - peor {}% ({})", formatThousands(deaths),
                               worst.percent, formatThousands(worst.count))
                 : "Sin muertes",
             binDeaths(m_practice ? m_progress.deathsPractice : m_progress.deathsNormal),
             deathOpts);

    auto runs = m_progress.recentRuns(m_practice);

    ChartOptions jumpOpts;
    jumpOpts.color = kJumpColor;
    jumpOpts.stretch = false;
    jumpOpts.average = true;
    jumpOpts.showScale = true;
    jumpOpts.axisNote = "mas recientes ->";
    jumpOpts.emptyText = noData;

    std::vector<float> runValues;
    runValues.reserve(runs.size());
    jumpOpts.tints.reserve(runs.size());

    int64_t totalJumps = 0;
    for (auto const& run : runs) {
        runValues.push_back(static_cast<float>(run.jumps));
        totalJumps += run.jumps;
    // Dim early exits so longer runs remain visually distinct.
        jumpOpts.tints.push_back(static_cast<float>(run.percent) / 100.f);
    }

    std::string runsRight = runs.empty()
        ? "Sin intentos"
        : fmt::format("{} intentos - media {:.1f}", runs.size(),
                      static_cast<double>(totalJumps) / static_cast<double>(runs.size()));

    addChart(kJumpHeaderY, kJumpChartY,
             m_practice ? "Saltos por intento (practica)" : "Saltos por intento",
             runsRight, runValues, jumpOpts);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_chartLayer->addChild(menu);

    auto spr = ButtonSprite::create(m_practice ? "Ver normal" : "Ver practica",
                                    "bigFont.fnt", "GJ_button_04.png", 0.7f);
    if (spr) spr->setScale(0.44f);
    auto toggle = CCMenuItemSpriteExtra::create(
        spr, this, menu_selector(LevelStatsPopup::onPracticeToggle));
    toggle->setPosition({kPopupW / 2.f, kToggleY});
    menu->addChild(toggle);
}

void LevelStatsPopup::requestThumbnail() {
    int levelID = m_level ? m_level->m_levelID.value() : 0;
    if (levelID <= 0) return;

    // Reuse the level screen's already-loaded thumbnail.
    if (paimon::ThumbnailBackgroundChangedEvent::s_lastLevelID == levelID) {
        if (auto* shown = paimon::ThumbnailBackgroundChangedEvent::getLastTexture()) {
            applyThumbnail(shown);
            return;
        }
    }

    if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
        applyThumbnail(cached);
        return;
    }

    auto self = WeakRef<LevelStatsPopup>(this);
    ThumbnailLoader::get().requestLoad(levelID, fmt::format("{}.png", levelID),
        [self, levelID](CCTexture2D* texture, bool success) {
            if (!success || !texture) {
                log::debug("[Paimbnails] stats popup: sin miniatura para {}", levelID);
                return;
            }
            auto ref = self.lock();
            if (!ref) return;
            static_cast<LevelStatsPopup*>(ref.data())->applyThumbnail(texture);
        },
        ThumbnailLoader::PriorityHero, false, ThumbnailLoader::Quality::High);
}

void LevelStatsPopup::applyThumbnail(CCTexture2D* texture) {
    if (!texture || m_hasThumbnail || !m_mainLayer) return;

    CCSize area{kPopupW - 6.f, kPopupH - 6.f};

    // Try the synchronous cached blur first to avoid a flash.
    if (auto* blurred = Shaders::createPopupPaimonBlurredSprite(texture, area, kBlurIntensity)) {
        installThumbnail(blurred, area, false);
        return;
    }

    Ref<CCTexture2D> raw = texture;
    auto self = WeakRef<LevelStatsPopup>(this);
    // BlurSystem keys by texture so gallery/re-upload changes invalidate naturally.
    BlurSystem::getInstance()->buildPaimonBlurPriority(
        texture, area, kBlurIntensity, std::string{},
        [self, area, raw](CCSprite* blurred) {
            auto ref = self.lock();
            if (!ref) return;
    // Fall back to the unblurred thumbnail.
            auto* sprite = blurred ? blurred : CCSprite::createWithTexture(raw);
            if (!sprite) return;
            static_cast<LevelStatsPopup*>(ref.data())->installThumbnail(sprite, area, true);
        });
}

void LevelStatsPopup::installThumbnail(CCSprite* thumbnail, CCSize const& area, bool fadeIn) {
    if (!thumbnail || m_hasThumbnail || !m_mainLayer) return;

    auto* backdrop = makeBackdrop(thumbnail, area, 120, fadeIn);
    if (!backdrop) return;

    backdrop->setPosition({kPopupW / 2.f, kPopupH / 2.f});
    m_mainLayer->addChild(backdrop, 1);
    m_hasThumbnail = true;
}

void LevelStatsPopup::onPracticeToggle(CCObject*) {
    m_practice = !m_practice;
    buildCharts();

    if (m_chartLayer) {
        m_chartLayer->stopAllActions();
        m_chartLayer->setPositionX(m_practice ? 16.f : -16.f);
        m_chartLayer->runAction(CCEaseOut::create(
            CCMoveTo::create(0.18f, {0.f, 0.f}), 2.f));
    }
}

void LevelStatsPopup::onFullInfo(CCObject*) {
    if (!m_level) return;
    if (auto* popup = ExtendedInfoPopup::create(m_level)) popup->show();
}

}
