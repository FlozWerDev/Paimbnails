#include "TimelinePanel.hpp"

#include "AnimatePopups.hpp"
#include "AnimateWidgets.hpp"
#include "../AnimateText.hpp"
#include "../services/AnimateSession.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/ScissorClipNode.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::animate {

namespace {

namespace w = widgets;

constexpr float kW = 384.f;
constexpr float kHeaderH = 24.f;
constexpr float kStripH = 52.f;
constexpr float kToolsH = 30.f;
constexpr float kPad = 6.f;
constexpr float kFullH = kPad + kToolsH + kStripH + kHeaderH;
constexpr float kCollapsedH = kHeaderH + 6.f;

constexpr float kClipInsetX = 3.f;
constexpr float kClipW = kW - 16.f - kClipInsetX * 2.f;
constexpr float kClipH = kStripH - 6.f;
constexpr float kCellW = 26.f;
constexpr float kCellH = 32.f;
constexpr float kStep = 29.f;
constexpr float kLead = 4.f;
constexpr float kCellY = 21.f;

constexpr float kToolSize = 21.f;
constexpr float kToolStep = 23.5f;
constexpr float kToolGap = 5.f;

constexpr int kMenuPriority = -260;
constexpr int kPanelPriority = -250;

constexpr char const* kPosXKey = "editor-animate-pos-x";
constexpr char const* kPosYKey = "editor-animate-pos-y";
constexpr char const* kCollapsedKey = "editor-animate-collapsed";

constexpr ccColor3B kEmptyTint{100, 100, 116};
constexpr ccColor3B kHeadTint{255, 70, 70};
constexpr ccColor3B kBeforeTint{90, 170, 255};
constexpr ccColor3B kAfterTint{120, 235, 120};

AnimateSession& session() {
    return AnimateSession::get();
}

float timeBefore(Clip const& clip, int frame) {
    float time = 0.f;
    for (int i = 0; i < frame && i < static_cast<int>(clip.frames.size()); ++i) {
        auto const& f = clip.frames[static_cast<std::size_t>(i)];
        if (!f.skip) time += frameDuration(clip, f);
    }
    return time;
}

CCLabelBMFont* label(char const* text, char const* font, float scale, float maxWidth) {
    auto* l = CCLabelBMFont::create(text, font);
    l->limitLabelWidth(maxWidth, scale, 0.1f);
    return l;
}

void report(Result<> const& result) {
    if (result.isErr()) w::notifyError(result.unwrapErr());
}

} // namespace

TimelinePanel* TimelinePanel::s_instance = nullptr;

TimelinePanel* TimelinePanel::get() {
    return s_instance;
}

TimelinePanel* TimelinePanel::create(LevelEditorLayer* editor) {
    auto* ret = new TimelinePanel();
    if (ret->init(editor)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool TimelinePanel::init(LevelEditorLayer* editor) {
    if (!CCLayer::init()) return false;
    m_editor = editor;
    s_instance = this;
    setID("animate-timeline"_spr);
    setTouchEnabled(true);

    m_collapsed = Mod::get()->getSavedValue<bool>(kCollapsedKey, false);
    setContentSize({kW, panelHeight()});
    loadPosition();
    m_lastWin = CCDirector::get()->getWinSize();

    rebuild();
    applyVisibility();
    schedule(schedule_selector(TimelinePanel::tick));
    return true;
}

TimelinePanel::~TimelinePanel() {
    if (s_instance == this) s_instance = nullptr;
    session().detach(m_editor);
}

void TimelinePanel::onExit() {
    CCLayer::onExit();
    session().stopPlayback();
}

void TimelinePanel::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, kPanelPriority, true);
}

float TimelinePanel::panelHeight() const {
    return m_collapsed ? kCollapsedH : kFullH;
}

CCRect TimelinePanel::stripRect() const {
    return {8.f, kPad + kToolsH, kW - 16.f, kStripH - 4.f};
}

float TimelinePanel::contentWidth() const {
    auto* clip = session().activeClip();
    int const cells = clip ? static_cast<int>(clip->frames.size()) + 1 : 0;
    return kLead * 2.f + static_cast<float>(cells) * kStep;
}

void TimelinePanel::clampScroll() {
    m_scroll = std::clamp(m_scroll, 0.f, std::max(0.f, contentWidth() - kClipW));
}

void TimelinePanel::ensureVisible(int frame) {
    if (frame < 0) return;
    float const left = kLead + static_cast<float>(frame) * kStep;
    float const right = left + kCellW;
    if (left - kStep * 0.5f < m_scroll) m_scroll = left - kStep * 0.5f;
    else if (right + kStep * 0.5f > m_scroll + kClipW) m_scroll = right + kStep * 0.5f - kClipW;
    clampScroll();
}

int TimelinePanel::cellAt(float localX) const {
    float const offset = localX - kLead + m_scroll;
    if (offset < 0.f) return -1;
    int const index = static_cast<int>(offset / kStep);
    if (offset - static_cast<float>(index) * kStep > kCellW + 1.f) return -1;
    return index;
}

void TimelinePanel::rebuild() {
    if (m_root) m_root->removeFromParent();
    m_root = CCNode::create();
    addChild(m_root);
    m_cells = nullptr;
    m_infoLabel = nullptr;
    m_timeLabel = nullptr;
    m_playButton = nullptr;

    float const h = panelHeight();
    setContentSize({kW, h});

    auto* bg = NineSlice::create("GJ_square02.png");
    bg->setScaleMultiplier(0.45f);
    bg->setContentSize({kW, h});
    bg->setAnchorPoint({0.f, 0.f});
    bg->setOpacity(240);
    m_root->addChild(bg, -2);

    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize({kW, h});
    m_menu->setTouchPriority(kMenuPriority);
    m_root->addChild(m_menu, 10);

    float const headerY = h - kHeaderH / 2.f - 3.f;
    buildHeader(headerY);
    if (!m_collapsed) {
        auto* clip = session().activeClip();
        auto* line = ui::makeDivider(kW - 20.f, clip ? w::clipColor(clip->color) : ui::palette::gold, 170);
        line->setPosition({kW / 2.f, headerY - kHeaderH / 2.f + 1.f});
        m_root->addChild(line, 1);
        buildStrip();
        buildTools(kPad + kToolsH / 2.f);
    }

    m_shownRevision = session().revision();
    clampScroll();
    refreshDynamic();
}

void TimelinePanel::buildHeader(float y) {
    auto& s = session();
    auto* clip = s.activeClip();

    auto* film = w::fitted(w::icon("paim_anim_film.png", {"GJ_hammerIcon_001.png"}), 13.f);
    film->setColor(ui::palette::gold);
    film->setPosition({13.f, y});
    m_root->addChild(film, 2);

    auto* prev = w::iconButton(w::icon(nullptr, {"edit_leftBtn_001.png"}), 8.f, [] {
        session().selectClip(session().activeIndex() - 1);
    });
    prev->setPosition({28.f, y});
    prev->setEnabled(clip && s.activeIndex() > 0);
    if (!prev->isEnabled()) prev->setOpacity(90);
    m_menu->addChild(prev);

    auto* chip = CCNode::create();
    auto* swatch = w::fitted(w::icon(nullptr, {"GJ_colorBtn_001.png"}), 11.f);
    swatch->setColor(clip ? w::clipColor(clip->color) : kEmptyTint);
    auto* name = label(clip ? clip->name.c_str() : "Paimon Animate", "bigFont.fnt", 0.36f, 92.f);
    float const chipW = 15.f + name->getScaledContentSize().width;
    chip->setContentSize({chipW, 16.f});
    chip->setAnchorPoint({0.5f, 0.5f});
    swatch->setPosition({5.5f, 8.f});
    name->setAnchorPoint({0.f, 0.5f});
    name->setPosition({14.f, 8.5f});
    chip->addChild(swatch);
    chip->addChild(name);
    auto* chipButton = CCMenuItemExt::createSpriteExtra(chip, [](auto*) { openClips(); });
    chipButton->m_scaleMultiplier = 1.06f;
    chipButton->setPosition({36.f + chipW / 2.f, y});
    m_menu->addChild(chipButton);

    float const nextX = 36.f + chipW + 8.f;
    auto* nextSprite = w::icon(nullptr, {"edit_leftBtn_001.png"});
    nextSprite->setFlipX(true);
    auto* next = w::iconButton(nextSprite, 8.f, [] {
        session().selectClip(session().activeIndex() + 1);
    });
    next->setPosition({nextX, y});
    next->setEnabled(clip && s.activeIndex() + 1 < static_cast<int>(s.doc().clips.size()));
    if (!next->isEnabled()) next->setOpacity(90);
    m_menu->addChild(next);

    auto* layers = w::iconButton(w::icon("paim_anim_layers.png", {"GJ_listAddBtn_001.png"}), 13.f,
        [] { openClips(); });
    layers->setPosition({nextX + 15.f, y});
    m_menu->addChild(layers);

    float const rightStart = kW - 118.f;
    float const leftEnd = nextX + 24.f;
    float const infoX = (leftEnd + rightStart) / 2.f;
    float const infoW = std::max(40.f, rightStart - leftEnd - 4.f);
    m_infoWidth = infoW;
    m_infoLabel = label("", "bigFont.fnt", 0.26f, infoW);
    m_infoLabel->setPosition({infoX, y + 5.f});
    m_root->addChild(m_infoLabel, 2);
    m_timeLabel = label("", "chatFont.fnt", 0.42f, infoW);
    m_timeLabel->setColor(ui::palette::muted);
    m_timeLabel->setPosition({infoX, y - 5.f});
    m_root->addChild(m_timeLabel, 2);

    if (clip) {
        auto* mode = w::icon(w::modeIcon(clip->settings.mode), {"GJ_updateBtn_001.png"});
        mode->setColor(ui::palette::info);
        auto* modeButton = w::iconButton(mode, 14.f, [this] { cycleMode(); });
        modeButton->setPosition({kW - 104.f, y});
        m_menu->addChild(modeButton);

        auto* pill = CCNode::create();
        pill->setContentSize({40.f, 16.f});
        pill->setAnchorPoint({0.5f, 0.5f});
        pill->addChild(ui::makeInset({40.f, 16.f}, 150), -1);
        auto* fps = label(fmt::format("{:g}fps", clip->settings.fps).c_str(), "goldFont.fnt", 0.42f, 34.f);
        fps->setPosition({20.f, 8.5f});
        pill->addChild(fps);
        auto* fpsButton = CCMenuItemExt::createSpriteExtra(pill, [](auto*) {
            openCenter(CenterTab::Playback);
        });
        fpsButton->m_scaleMultiplier = 1.08f;
        fpsButton->setPosition({kW - 76.f, y});
        m_menu->addChild(fpsButton);
    }

    auto* gear = w::iconButton(w::icon("paim_anim_gear.png", {"GJ_optionsBtn_001.png"}), 14.f, [] {
        openCenter(CenterTab::Playback);
    });
    gear->setPosition({kW - 47.f, y});
    m_menu->addChild(gear);

    auto* fold = w::iconButton(
        w::icon(nullptr, {m_collapsed ? "edit_downBtn_001.png" : "edit_upBtn_001.png"}), 9.f,
        [this] { toggleCollapsed(); });
    fold->setPosition({kW - 29.f, y});
    m_menu->addChild(fold);

    auto* close = w::iconButton(w::icon(nullptr, {"GJ_closeBtn_001.png"}), 13.f, [] {
        session().setOpen(false);
    });
    close->setPosition({kW - 12.f, y});
    m_menu->addChild(close);
}

void TimelinePanel::buildStrip() {
    auto const rect = stripRect();
    auto* inset = ui::makeInset(rect.size, 130);
    inset->setPosition(rect.origin);
    m_root->addChild(inset, 0);

    auto* clip = session().activeClip();
    if (!clip) {
        auto* hint = label(tr("Group frames into an animation to start.",
            "Agrupa frames en una animacion para empezar."), "chatFont.fnt", 0.48f, rect.size.width - 20.f);
        hint->setColor(ui::palette::muted);
        hint->setPosition({kW / 2.f, rect.getMaxY() - 11.f});
        m_root->addChild(hint, 2);

        auto* create = ui::makeButton(tr("New animation", "Nueva animacion"),
            [] { createWithPrompt(false); }, ui::Btn::Green, 130.f, 0.5f);
        create->setPosition({kW / 2.f - 70.f, rect.origin.y + 15.f});
        m_menu->addChild(create);
        auto* fromSelection = ui::makeButton(tr("From selection", "Desde seleccion"),
            [] { createWithPrompt(true); }, ui::Btn::Blue, 130.f, 0.5f);
        fromSelection->setPosition({kW / 2.f + 70.f, rect.origin.y + 15.f});
        m_menu->addChild(fromSelection);
        return;
    }

    auto* clipNode = paimon::ScissorClipNode::create();
    clipNode->setContentSize({kClipW, kClipH});
    clipNode->setPosition({rect.origin.x + kClipInsetX, rect.origin.y + 1.f});
    m_root->addChild(clipNode, 1);
    m_cells = CCNode::create();
    clipNode->addChild(m_cells);
}

void TimelinePanel::buildTools(float y) {
    auto& s = session();
    auto const& prefs = s.prefs();
    auto* clip = s.activeClip();

    struct Tool {
        CCSprite* top;
        EditorBaseColor color;
        std::function<void()> action;
        bool play = false;
    };
    auto gd = [](char const* frame) { return w::icon(nullptr, {frame}); };
    auto mod = [](char const* frame, char const* fallback) { return w::icon(frame, {fallback}); };

    std::vector<std::vector<Tool>> groups = {
        {
            {gd("edit_leftBtn2_001.png"), EditorBaseColor::Gray, [] {
                session().stopPlayback();
                session().firstFrame();
            }},
            {gd("edit_leftBtn_001.png"), EditorBaseColor::Gray, [] {
                session().stopPlayback();
                session().stepFrame(-1);
            }},
            {nullptr, EditorBaseColor::Green, [] { session().togglePlay(); }, true},
            {gd("edit_rightBtn_001.png"), EditorBaseColor::Gray, [] {
                session().stopPlayback();
                session().stepFrame(1);
            }},
            {gd("edit_rightBtn2_001.png"), EditorBaseColor::Gray, [] {
                session().stopPlayback();
                session().lastFrame();
            }},
        },
        {
            {mod("paim_anim_frameAdd.png", "GJ_plusBtn_001.png"), EditorBaseColor::Green, [] {
                session().stopPlayback();
                report(session().insertFrame(true));
            }},
            {mod("paim_anim_frameDup.png", "GJ_duplicateBtn_001.png"), EditorBaseColor::Green, [] {
                session().stopPlayback();
                report(session().duplicateFrame());
            }},
            {mod("paim_anim_frameDel.png", "GJ_deleteIcon_001.png"), EditorBaseColor::Salmon,
                [this] { deleteCurrentFrame(); }},
            {mod("paim_anim_tag.png", "GJ_editBtn_001.png"), EditorBaseColor::Orange, [] { openFrame(); }},
        },
        {
            {mod("paim_anim_assign.png", "GJ_downloadBtn_001.png"), EditorBaseColor::LightBlue, [] {
                auto result = session().assignSelection();
                if (result.isErr()) return w::notifyError(result.unwrapErr());
                w::notifyOk(fmt::format("{} {} {}", result.unwrap(),
                    tr("objects -> frame", "objetos -> frame"), session().currentFrame() + 1));
            }},
            {mod("paim_anim_select.png", "edit_findBtn_001.png"), EditorBaseColor::LightBlue, [] {
                auto result = session().selectFrameObjects();
                if (result.isErr()) return w::notifyError(result.unwrapErr());
                if (result.unwrap() == 0) {
                    w::notifyError(tr("This frame is empty.", "Este frame esta vacio."));
                }
            }},
        },
        {
            {mod("paim_anim_onion.png", "GJ_sMagicIcon_001.png"),
                prefs.onion ? EditorBaseColor::Teal : EditorBaseColor::DarkGray, [] {
                    session().prefs().onion = !session().prefs().onion;
                    session().prefsChanged();
                }},
            {mod("paim_anim_ghost.png", "GJ_infoIcon_001.png"),
                prefs.ghosts == Ghosts::Visible ? EditorBaseColor::Cyan
                    : prefs.ghosts == Ghosts::Dim ? EditorBaseColor::Aqua : EditorBaseColor::DarkGray,
                [this] { cycleGhosts(); }},
            {mod("paim_anim_loop.png", "GJ_updateBtn_001.png"),
                prefs.previewLoop ? EditorBaseColor::Teal : EditorBaseColor::DarkGray, [] {
                    session().prefs().previewLoop = !session().prefs().previewLoop;
                    session().prefsChanged();
                }},
        },
        {
            {mod("paim_anim_bake.png", "GJ_starsIcon_001.png"), EditorBaseColor::Magenta,
                [this] { bakeActive(); }},
        },
    };

    std::size_t tools = 0;
    for (auto const& group : groups) tools += group.size();
    float const total = static_cast<float>(tools) * kToolStep +
        static_cast<float>(groups.size() - 1) * kToolGap;
    float x = (kW - total) / 2.f + kToolStep / 2.f;

    for (auto& group : groups) {
        for (auto& tool : group) {
            CCMenuItemSpriteExtra* button = nullptr;
            if (tool.play) {
                button = w::iconButton(w::icon(nullptr, {"GJ_playBtn2_001.png"}), kToolSize + 3.f,
                    std::move(tool.action));
                m_playButton = button;
            } else {
                button = w::toolButton(tool.top, tool.color, kToolSize, std::move(tool.action));
            }
            if (!button) continue;
            button->setPosition({x, y});
            m_menu->addChild(button);

            if (tool.color == EditorBaseColor::Magenta && clip) {
                auto const& c = clip->compiled;
                ccColor3B const status = !c.present() ? ccColor3B{255, 210, 70}
                    : c.stale ? ccColor3B{255, 110, 80} : ccColor3B{120, 240, 110};
                auto* dot = w::fitted(w::icon("paim_anim_key.png", {"GJ_colorBtn_001.png"}), 7.f);
                dot->setColor(status);
                dot->setPosition({x + 8.f, y + 8.f});
                m_root->addChild(dot, 12);
            }
            x += kToolStep;
        }
        x += kToolGap;
    }
}

void TimelinePanel::refreshDynamic() {
    auto& s = session();
    m_shownFrame = s.currentFrame();
    m_shownHead = s.displayedFrame();
    m_shownPlaying = s.playing();
    m_cellsRevision = s.revision();
    refreshCells();
    refreshInfo();
    if (m_playButton) {
        auto* sprite = w::fitted(w::icon(nullptr,
            {m_shownPlaying ? "GJ_pauseBtn_001.png" : "GJ_playBtn2_001.png"}), kToolSize + 3.f);
        m_playButton->setSprite(sprite);
    }
}

void TimelinePanel::refreshInfo() {
    if (!m_infoLabel || !m_timeLabel) return;
    auto& s = session();
    auto* clip = s.activeClip();
    if (!clip) {
        m_infoLabel->setString(tr("Frame by frame", "Frame a frame"));
        m_infoLabel->limitLabelWidth(m_infoWidth, 0.26f, 0.1f);
        m_timeLabel->setString(tr("No animations yet", "Aun no hay animaciones"));
        m_timeLabel->limitLabelWidth(m_infoWidth, 0.42f, 0.2f);
        return;
    }

    int const count = static_cast<int>(clip->frames.size());
    int const frame = s.playing() && m_shownHead >= 0 ? m_shownHead : s.currentFrame();
    m_infoLabel->setString(fmt::format("{} {} / {}", tr("Frame", "Frame"), frame + 1, count).c_str());
    m_infoLabel->limitLabelWidth(m_infoWidth, 0.26f, 0.1f);

    float const total = sequenceDuration(buildSequence(*clip));
    float const at = s.playing() && total > 0.f ? std::fmod(s.playTime(), total)
        : timeBefore(*clip, frame);
    std::string line = fmt::format("{} / {}", w::seconds(at), w::seconds(total));
    if (auto* current = s.frameAt(frame); current && !current->label.empty()) {
        line = fmt::format("\"{}\"  {}", current->label, line);
    }
    m_timeLabel->setString(line.c_str());
    m_timeLabel->limitLabelWidth(m_infoWidth, 0.42f, 0.2f);
}

void TimelinePanel::refreshCells() {
    if (!m_cells) return;
    m_cells->removeAllChildren();
    auto& s = session();
    auto* clip = s.activeClip();
    if (!clip) return;

    auto const& prefs = s.prefs();
    int const count = static_cast<int>(clip->frames.size());
    int const current = s.currentFrame();
    int const head = s.playing() ? m_shownHead : current;
    int const first = std::max(0, static_cast<int>((m_scroll - kLead) / kStep));
    int const last = std::min(count, static_cast<int>((m_scroll + kClipW) / kStep) + 1);
    auto const tint = w::clipColor(clip->color);

    for (int i = first; i <= last; ++i) {
        float const x = kLead + static_cast<float>(i) * kStep + kCellW / 2.f - m_scroll;

        auto* cell = w::icon("paim_anim_cell.png", {"GJ_button_04.png"});
        cell->setScale(kCellW / std::max(1.f, cell->getContentSize().width));
        cell->setPosition({x, kCellY});
        m_cells->addChild(cell, 1);

        if (i == count) {
            cell->setColor({62, 62, 78});
            cell->setOpacity(150);
            auto* plus = w::fitted(w::icon("paim_anim_frameAdd.png", {"GJ_plusBtn_001.png"}), 12.f);
            plus->setOpacity(190);
            plus->setPosition({x, kCellY});
            m_cells->addChild(plus, 2);
            continue;
        }

        auto const& frame = clip->frames[static_cast<std::size_t>(i)];
        bool const isCurrent = i == current;
        bool const filled = s.objectCount(frame.group) > 0;
        auto color = filled ? tint : kEmptyTint;
        cell->setColor(isCurrent ? color : w::scaled(color, 0.6f));
        if (frame.skip) cell->setOpacity(110);

        if (isCurrent) {
            auto* glow = w::icon("paim_anim_cellGlow.png");
            glow->setScale(cell->getScale());
            glow->setColor(ui::palette::gold);
            glow->setPosition({x, kCellY});
            m_cells->addChild(glow, 0);
        }

        auto* number = label(fmt::format("{}", i + 1).c_str(), "bigFont.fnt", 0.3f, kCellW - 8.f);
        number->setColor(isCurrent ? ui::palette::gold : ui::palette::text);
        number->setPosition({x, kCellY + 1.f});
        m_cells->addChild(number, 3);

        auto* key = w::fitted(w::icon(filled ? "paim_anim_key.png" : "paim_anim_empty.png"), 5.f);
        key->setPosition({x, kCellY + 12.f});
        key->setOpacity(filled ? 255 : 170);
        m_cells->addChild(key, 3);

        if (!frame.label.empty()) {
            auto* tag = w::fitted(w::icon("paim_anim_tag.png"), 6.f);
            tag->setColor(ui::palette::gold);
            tag->setPosition({x - 8.f, kCellY + 12.f});
            m_cells->addChild(tag, 3);
        }
        if (frame.hold > 1) {
            auto* hold = label(fmt::format("x{}", frame.hold).c_str(), "bigFont.fnt", 0.2f, kCellW - 6.f);
            hold->setColor({255, 230, 150});
            hold->setPosition({x + 5.f, kCellY - 11.5f});
            m_cells->addChild(hold, 3);
        }
        if (frame.skip) {
            auto* skip = w::fitted(w::icon("paim_anim_skip.png"), 14.f);
            skip->setColor(ui::palette::danger);
            skip->setOpacity(210);
            skip->setPosition({x, kCellY});
            m_cells->addChild(skip, 4);
        }

        if (prefs.onion && !s.playing() && i != current) {
            int const distance = i - current;
            bool const before = distance < 0 && -distance <= prefs.onionBefore;
            bool const after = distance > 0 && distance <= prefs.onionAfter;
            if (before || after) {
                auto const bar = before ? kBeforeTint : kAfterTint;
                auto* marker = CCLayerColor::create({bar.r, bar.g, bar.b, 220}, kCellW - 8.f, 2.f);
                marker->setPosition({x - (kCellW - 8.f) / 2.f, 1.5f});
                m_cells->addChild(marker, 3);
            }
        }

        if (i == head) {
            auto* playhead = w::fitted(w::icon("paim_anim_playhead.png", {"edit_downBtn_001.png"}), 7.f);
            playhead->setColor(kHeadTint);
            playhead->setPosition({x, kCellY + kCellH / 2.f + 4.5f});
            m_cells->addChild(playhead, 5);
        }
    }
}

void TimelinePanel::tapCell(int index) {
    auto& s = session();
    auto* clip = s.activeClip();
    if (!clip || index < 0) return;
    int const count = static_cast<int>(clip->frames.size());
    if (index == count) {
        s.stopPlayback();
        s.lastFrame();
        report(s.insertFrame(true));
        return;
    }
    if (index > count) return;
    if (index == s.currentFrame() && !s.playing()) {
        openFrame();
        return;
    }
    s.stopPlayback();
    s.setFrame(index);
}

void TimelinePanel::deleteCurrentFrame() {
    auto& s = session();
    s.stopPlayback();
    auto* clip = s.activeClip();
    auto* frame = s.frameAt(s.currentFrame());
    if (!clip || !frame) return;
    if (clip->frames.size() == 1 && frame->group <= 0) return;

    auto const objects = s.objectCount(frame->group);
    if (objects == 0) {
        report(s.deleteFrame(true));
        return;
    }
    confirm(tr("Delete frame", "Borrar frame"),
        fmt::format("{} <cy>{}</c> {} <cr>{}</c> {}", tr("Frame", "El frame"), s.currentFrame() + 1,
            tr("and its", "y sus"), objects, tr("objects will be deleted.", "objetos se van a borrar.")),
        tr("Delete", "Borrar"),
        [] { report(session().deleteFrame(true)); });
}

void TimelinePanel::bakeActive() {
    session().stopPlayback();
    bake(session().activeIndex());
}

void TimelinePanel::cycleMode() {
    auto* clip = session().activeClip();
    if (!clip) return;
    clip->settings.mode = static_cast<PlayMode>((static_cast<int>(clip->settings.mode) + 1) % 5);
    session().changed(clip);
    w::notifyOk(w::modeName(clip->settings.mode));
}

void TimelinePanel::cycleGhosts() {
    auto& prefs = session().prefs();
    prefs.ghosts = static_cast<Ghosts>((static_cast<int>(prefs.ghosts) + 1) % 3);
    session().prefsChanged();
    char const* names[] = {
        tr("Other frames hidden", "Otros frames ocultos"),
        tr("Other frames dimmed", "Otros frames atenuados"),
        tr("Every frame visible", "Todos los frames visibles"),
    };
    w::notifyOk(names[static_cast<int>(prefs.ghosts)]);
}

void TimelinePanel::toggleCollapsed() {
    float const top = getPositionY() + panelHeight();
    m_collapsed = !m_collapsed;
    Mod::get()->setSavedValue<bool>(kCollapsedKey, m_collapsed);
    setPositionY(top - panelHeight());
    clampToScreen();
    savePosition();
    // the button that called us lives in the menu a rebuild would free.
    m_shownRevision = -1;
}

bool TimelinePanel::handleScroll(float y, float x) {
    if (!isVisible()) return false;
    auto const local = convertToNodeSpace(getMousePos());
    if (local.x < 0.f || local.y < 0.f || local.x > kW || local.y > panelHeight()) return false;
    if (!m_collapsed && m_cells && stripRect().containsPoint(local)) {
        float const amount = std::abs(y) > 0.001f ? y : x;
        m_scroll += amount * 4.f;
        clampScroll();
        refreshCells();
    }
    return true;
}

void TimelinePanel::tick(float dt) {
    auto& s = session();
    s.tick(dt);
    // the updatevisibility hook repaints every frame; this only skips the one-frame lag after an edit.
    if (s.needsApply()) s.applyOverrides();
    applyVisibility();
    if (!isVisible()) return;

    auto const win = CCDirector::get()->getWinSize();
    if (!win.equals(m_lastWin)) {
        m_lastWin = win;
        clampToScreen();
    }

    // menus registered while a popup forces priority would sit above that popup.
    bool const popupOpen = CCDirector::get()->getTouchDispatcher()->isUsingForcePrio();
    if (s.revision() != m_shownRevision && !popupOpen) {
        if (s.currentFrame() != m_shownFrame) ensureVisible(s.currentFrame());
        rebuild();
        return;
    }
    int const head = s.displayedFrame();
    bool const moved = s.currentFrame() != m_shownFrame || head != m_shownHead || s.playing() != m_shownPlaying;
    if (moved) ensureVisible(s.playing() && head >= 0 ? head : s.currentFrame());
    if (moved || s.revision() != m_cellsRevision) {
        clampScroll();
        refreshDynamic();
    } else if (s.playing()) {
        refreshInfo();
    }
}

void TimelinePanel::applyVisibility() {
    bool const playtesting = m_editor && m_editor->m_playbackMode != PlaybackMode::Not;
    bool const uiHidden = m_editor && m_editor->m_editorUI && !m_editor->m_editorUI->isVisible();
    setVisible(session().isOpen() && !playtesting && !uiHidden);
}

void TimelinePanel::loadPosition() {
    auto const win = CCDirector::get()->getWinSize();
    auto* mod = Mod::get();
    float const x = static_cast<float>(mod->getSavedValue<double>(kPosXKey, (win.width - kW) / 2.f));
    float const y = static_cast<float>(mod->getSavedValue<double>(kPosYKey,
        win.height - kFullH - 38.f));
    setPosition({x, y});
    clampToScreen();
}

void TimelinePanel::savePosition() {
    auto* mod = Mod::get();
    mod->setSavedValue<double>(kPosXKey, getPositionX());
    mod->setSavedValue<double>(kPosYKey, getPositionY());
}

void TimelinePanel::clampToScreen() {
    auto const win = CCDirector::get()->getWinSize();
    auto pos = getPosition();
    pos.x = std::clamp(pos.x, 0.f, std::max(0.f, win.width - kW));
    pos.y = std::clamp(pos.y, 0.f, std::max(0.f, win.height - panelHeight()));
    setPosition(pos);
}

bool TimelinePanel::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!isVisible()) return false;
    auto const local = convertToNodeSpace(touch->getLocation());
    if (local.x < 0.f || local.y < 0.f || local.x > kW || local.y > panelHeight()) return false;

    m_dragged = false;
    if (!m_collapsed && m_cells && stripRect().containsPoint(local)) {
        m_grab = Grab::Strip;
        m_grabX = touch->getLocation().x;
        m_scrollStart = m_scroll;
    } else {
        m_grab = Grab::Move;
        m_grabOffset = local;
    }
    return true;
}

void TimelinePanel::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_grab == Grab::Strip) {
        float const dx = touch->getLocation().x - m_grabX;
        if (!m_dragged && std::abs(dx) > 6.f) m_dragged = true;
        if (!m_dragged) return;
        m_scroll = m_scrollStart - dx;
        clampScroll();
        refreshCells();
        return;
    }
    if (m_grab == Grab::Move) {
        auto* parent = getParent();
        auto const world = touch->getLocation();
        auto const local = parent ? parent->convertToNodeSpace(world) : world;
        setPosition(local - m_grabOffset);
        clampToScreen();
        m_dragged = true;
    }
}

void TimelinePanel::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_grab == Grab::Strip && !m_dragged) {
        auto const local = convertToNodeSpace(touch->getLocation());
        tapCell(cellAt(local.x - stripRect().origin.x - kClipInsetX));
    }
    if (m_grab == Grab::Move && m_dragged) savePosition();
    m_grab = Grab::None;
}

void TimelinePanel::ccTouchCancelled(CCTouch* touch, CCEvent* event) {
    m_dragged = true;
    ccTouchEnded(touch, event);
}

} // namespace paimon::animate
