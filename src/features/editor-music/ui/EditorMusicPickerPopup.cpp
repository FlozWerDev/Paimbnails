#include "EditorMusicPickerPopup.hpp"

#include "EditorMusicPanel.hpp"
#include "../services/EditorMusicPlayer.hpp"
#include "../../menu-music/services/MenuMusicCopy.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"
#include "../../menu-music/services/SongCoverCache.hpp"
#include "../../menu-music/ui/MenuMusicAddPopup.hpp"
#include "../../menu-music/ui/NewgroundsBrowserPopup.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/FilesystemPath.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/TextureBudget.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/binding/Slider.hpp>
#include <Geode/binding/SliderThumb.hpp>
#include <Geode/binding/SliderTouchLogic.hpp>
#include <Geode/ui/TextInput.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>

using namespace geode::prelude;
using paimon::menumusic::MenuMusicLibrary;
using paimon::menumusic::MenuMusicAddPopup;
using paimon::menumusic::MusicTrack;
using paimon::menumusic::NewgroundsBrowserPopup;
using paimon::menumusic::SongCoverCache;
using paimon::menumusic::TrackSource;

namespace paimon::editormusic {

namespace {

namespace kit = paimon::configkit;
namespace ui = paimon::ui;

constexpr float kPopupW = 432.f;
constexpr float kPopupH = 290.f;
constexpr float kPad = 12.f;
constexpr float kInnerW = kPopupW - kPad * 2.f; // 408
constexpr float kDeckH = 62.f;
constexpr float kDeckY = kPopupH - 31.f - kDeckH; // 197..259
constexpr float kTabBarY = kDeckY - 4.f - kit::kTabBarHeight; // 169..193
constexpr float kContentTop = kTabBarY - 5.f; // 164
constexpr float kContentBottom = 22.f;
constexpr float kRowH = 28.f;

int popupChildPrio(int offset = 2) {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - offset;
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string formatTime(int ms) {
    if (ms <= 0) return "0:00";
    int total = ms / 1000;
    return fmt::format("{}:{:02}", total / 60, total % 60);
}

CCSprite* firstFrame(std::initializer_list<char const*> frames) {
    for (auto const* f : frames) {
        if (auto* s = SpriteHelper::safeCreateWithFrameName(f)) return s;
    }
    return nullptr;
}

void replaceButtonIcon(CCMenuItemSpriteExtra* btn, CCSprite* spr, float size) {
    if (!btn || !spr) return;
    limitNodeSize(spr, {size, size}, 2.f, 0.05f);
    btn->setNormalImage(spr);
    auto const sz = spr->getScaledContentSize();
    btn->setContentSize(sz);
    spr->setAnchorPoint({0.5f, 0.5f});
    spr->setPosition({sz.width * 0.5f, sz.height * 0.5f});
}

char const* filterLabel(int idx) {
    switch (idx) {
        case 1: return "Favoritas";
        case 2: return "GD";
        case 3: return "Locales";
        default: return "Todas";
    }
}

char const* sortLabel(int idx) {
    switch (idx) {
        case 1: return "Recientes";
        case 2: return "Duracion";
        default: return "A-Z";
    }
}

void setButtonCaption(CCMenuItemSpriteExtra* btn, std::string const& text) {
    if (!btn) return;
    auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage());
    if (!spr) return;
    spr->setString(text.c_str());
    if (spr->m_label) {
        float const inner = std::max(8.f, spr->getContentSize().width - 14.f);
        spr->m_label->limitLabelWidth(inner, spr->m_scale, 0.1f);
    }
}

} // namespace

EditorMusicPickerPopup* EditorMusicPickerPopup::create(int initialTab) {
    auto* ret = new EditorMusicPickerPopup();
    if (ret->init(initialTab)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EditorMusicPickerPopup::init(int initialTab) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;

    m_tab = std::clamp(initialTab, 0, 3);
    setID("editor-music-picker"_spr);
    setTitle("Editor Music", "goldFont.fnt", 0.66f, 16.f);
    addCorners(SideArtStyle::PopupGold, 1.f, false);
    addInfoButton("Editor Music",
        "Escucha musica mientras creas en el editor.\n"
        "<cg>Ctrl+M</c> abre o cierra el reproductor flotante.\n"
        "<cy>Canciones</c>: busca, filtra, agrega a la cola o importa desde YouTube / Newgrounds / PC.\n"
        "<cy>Efectos FX</c>: velocidad, graves, agudos, lowpass, reverb y eco en vivo sin afectar el nivel.\n"
        "<cy>Ajustes</c>: comportamiento en playtest, autoplay, estilo GD y posicion.",
        Anchor::TopRight, {-16.f, -16.f});

    MenuMusicLibrary::get().load();
    EditorMusicPlayer::get().refreshQueue();

    buildHeaderDeck();
    buildTabBar();

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    if (m_statusLabel) {
        m_statusLabel->setAnchorPoint({0.5f, 0.5f});
        m_statusLabel->setPosition({kPopupW * 0.5f, 11.f});
        m_statusLabel->setScale(0.42f);
        m_statusLabel->setColor({170, 182, 204});
        m_mainLayer->addChild(m_statusLabel, 4);
    }

    auto& player = EditorMusicPlayer::get();
    m_playerToken = player.addListener([this]() {
        if (m_closing || !this->getParent()) return;
        syncHeaderFromState();
        auto& p = EditorMusicPlayer::get();
        if (m_tab == 0) {
            if (p.trackId() != m_renderedTrackId || p.isPlaying() != m_renderedPlaying) {
                scheduleSongsListRebuild(true);
            }
        } else if (m_tab == 1) {
            if (p.trackId() != m_renderedTrackId
                || p.source() != m_renderedSource
                || p.playlistId() != m_renderedPlaylistId
                || p.queue().size() != m_renderedQueueSize) {
                scheduleRebuild();
            }
        }
    });
    m_libToken = MenuMusicLibrary::get().addListener([this]() {
        if (m_closing || !this->getParent()) return;
        m_playableCache.clear();
        syncHeaderFromState();
        if (m_tab == 0) scheduleSongsListRebuild(true);
        else if (m_tab == 1) scheduleRebuild();
    });

    syncHeaderFromState();
    rebuildTabContent();
    this->schedule(schedule_selector(EditorMusicPickerPopup::tick));
    return true;
}

void EditorMusicPickerPopup::onClose(CCObject* sender) {
    if (m_closing) return;
    m_closing = true;
    ++m_coverGen;
    if (m_volCommitPending) {
        m_volCommitPending = false;
        auto& p = EditorMusicPlayer::get();
        p.setVolume(p.volume(), true);
    }
    if (m_seekCommitPending) {
        m_seekCommitPending = false;
        auto& p = EditorMusicPlayer::get();
        if (int const len = p.lengthMs(); len > 0) {
            p.seekMs(static_cast<int>(m_seekPendingRatio * static_cast<float>(len)));
        }
    }
    this->unschedule(schedule_selector(EditorMusicPickerPopup::tick));
    if (m_playerToken) {
        EditorMusicPlayer::get().removeListener(m_playerToken);
        m_playerToken = 0;
    }
    if (m_libToken) {
        MenuMusicLibrary::get().removeListener(m_libToken);
        m_libToken = 0;
    }
    Popup::onClose(sender);
}

void EditorMusicPickerPopup::onExit() {
    m_closing = true;
    ++m_coverGen;
    this->unschedule(schedule_selector(EditorMusicPickerPopup::tick));
    if (m_playerToken) {
        EditorMusicPlayer::get().removeListener(m_playerToken);
        m_playerToken = 0;
    }
    if (m_libToken) {
        MenuMusicLibrary::get().removeListener(m_libToken);
        m_libToken = 0;
    }
    Popup::onExit();
}

bool EditorMusicPickerPopup::scrollWheel(float x, float y) {
    return kit::queueWheelScroll(m_activeScroll, x, y, m_scrollTargetY, m_scrollTargetSet);
}

void EditorMusicPickerPopup::buildHeaderDeck() {
    m_headerDeck = CCNode::create();
    m_headerDeck->setContentSize({kInnerW, kDeckH});
    m_headerDeck->setPosition({kPad, kDeckY});
    m_mainLayer->addChild(m_headerDeck, 3);

    if (auto* bg = ui::makeInset({kInnerW, kDeckH}, 115, ui::palette::ink)) {
        m_headerDeck->addChild(bg, -1);
    }

    // Cover slot (left): GJ_square05 sits BEHIND the clipped artwork so it frames it without hiding it.
    constexpr float kArtSz = 48.f;
    auto* artSlot = CCNode::create();
    artSlot->setContentSize({kArtSz, kArtSz});
    artSlot->setPosition({7.f, (kDeckH - kArtSz) * 0.5f});
    m_headerDeck->addChild(artSlot, 2);

    if (auto* frame = SpriteHelper::safeCreateNineSliceFromFile("GJ_square05.png")) {
        frame->setScaleMultiplier(0.46f);
        frame->setContentSize({kArtSz, kArtSz});
        frame->setPosition({kArtSz * 0.5f, kArtSz * 0.5f});
        artSlot->addChild(frame, 0);
    }

    if (auto* well = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png")) {
        well->setScaleMultiplier(0.32f);
        well->setContentSize({kArtSz - 8.f, kArtSz - 8.f});
        well->setPosition({kArtSz * 0.5f, kArtSz * 0.5f});
        well->setColor({14, 18, 30});
        well->setOpacity(225);
        artSlot->addChild(well, 1);
    }

    float const innerSz = kArtSz - 10.f;
    m_discRotor = CCNode::create();
    m_discRotor->setContentSize({innerSz, innerSz});
    m_discRotor->setAnchorPoint({0.5f, 0.5f});
    m_discRotor->setPosition({kArtSz * 0.5f, kArtSz * 0.5f});
    artSlot->addChild(m_discRotor, 2);

    if (auto* stencil = SpriteHelper::createRoundedRectStencil(innerSz, innerSz, innerSz * 0.5f)) {
        m_coverClip = CCClippingNode::create();
        m_coverClip->setStencil(stencil);
        m_coverClip->setAlphaThreshold(0.05f);
        m_coverClip->setContentSize({innerSz, innerSz});
        m_coverClip->setAnchorPoint({0.5f, 0.5f});
        m_coverClip->setPosition({innerSz * 0.5f, innerSz * 0.5f});
        m_discRotor->addChild(m_coverClip, 0);
    }

    m_coverFallback = firstFrame({"GJ_musicOnBtn_001.png", "GJ_playMusicBtn_001.png"});
    if (m_coverFallback) {
        limitNodeSize(m_coverFallback, {22.f, 22.f}, 2.f, 0.05f);
        m_coverFallback->setPosition({innerSz * 0.5f, innerSz * 0.5f});
        m_discRotor->addChild(m_coverFallback, 2);
    }

    // Row 1 (top): Title, subtitle, and 5-bar equalizer
    m_nowTitle = CCLabelBMFont::create("Sin cancion", "bigFont.fnt");
    m_nowTitle->setAnchorPoint({0.f, 0.5f});
    m_nowTitle->setPosition({62.f, kDeckH - 11.f});
    m_nowTitle->setScale(0.42f);
    m_headerDeck->addChild(m_nowTitle, 2);

    m_nowSub = CCLabelBMFont::create("Elige una cancion o presiona Play", "chatFont.fnt");
    m_nowSub->setAnchorPoint({0.f, 0.5f});
    m_nowSub->setPosition({62.f, kDeckH - 22.f});
    m_nowSub->setScale(0.40f);
    m_nowSub->setColor(ui::palette::muted);
    m_headerDeck->addChild(m_nowSub, 2);

    for (int i = 0; i < 5; ++i) {
        auto* bar = SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png");
        if (!bar) continue;
        bar->setScaleMultiplier(0.18f);
        bar->setAnchorPoint({0.5f, 0.f});
        bar->setContentSize({3.2f, 3.f});
        bar->setPosition({kInnerW - 34.f + static_cast<float>(i) * 5.2f, kDeckH - 24.f});
        bar->setColor({110, 245, 85});
        m_headerDeck->addChild(bar, 3);
        m_eqBars[static_cast<std::size_t>(i)] = bar;
    }

    // Row 2 (middle, y = 28): Seek timeline (left) + Volume slider (right)
    constexpr float kMidY = 28.f;
    m_seekCurLabel = CCLabelBMFont::create("0:00", "chatFont.fnt");
    m_seekCurLabel->setAnchorPoint({1.f, 0.5f});
    m_seekCurLabel->setPosition({82.f, kMidY});
    m_seekCurLabel->setScale(0.40f);
    m_headerDeck->addChild(m_seekCurLabel, 2);

    m_seekSlider = Slider::create(this, menu_selector(EditorMusicPickerPopup::onSeekSliderChanged), 0.56f);
    if (m_seekSlider) {
        m_seekSlider->setPosition({150.f, kMidY});
        m_seekSlider->setTouchEnabled(true);
        if (m_seekSlider->m_touchLogic) m_seekSlider->m_touchLogic->setTouchPriority(popupChildPrio(3));
        m_headerDeck->addChild(m_seekSlider, 2);
    }

    m_seekTotLabel = CCLabelBMFont::create("0:00", "chatFont.fnt");
    m_seekTotLabel->setAnchorPoint({0.f, 0.5f});
    m_seekTotLabel->setPosition({218.f, kMidY});
    m_seekTotLabel->setScale(0.40f);
    m_seekTotLabel->setColor(ui::palette::dim);
    m_headerDeck->addChild(m_seekTotLabel, 2);

    m_volSlider = Slider::create(this, menu_selector(EditorMusicPickerPopup::onVolumeSliderChanged), 0.44f);
    if (m_volSlider) {
        m_volSlider->setPosition({kInnerW - 58.f, kMidY});
        m_volSlider->setTouchEnabled(true);
        if (m_volSlider->m_touchLogic) m_volSlider->m_touchLogic->setTouchPriority(popupChildPrio(3));
        m_headerDeck->addChild(m_volSlider, 2);
    }

    // Row 3 (bottom, y = 11): Transport & quick actions cleanly separated below sliders
    auto* deckMenu = CCMenu::create();
    deckMenu->setPosition({0.f, 0.f});
    deckMenu->setContentSize({kInnerW, kDeckH});
    deckMenu->setTouchPriority(popupChildPrio(3));
    m_headerDeck->addChild(deckMenu, 4);

    auto makeBtn = [&](std::initializer_list<char const*> frames, float sz, CCPoint pos, std::function<void()> cb)
        -> CCMenuItemSpriteExtra* {
        auto* spr = firstFrame(frames);
        if (!spr) return nullptr;
        limitNodeSize(spr, {sz, sz}, 2.f, 0.05f);
        auto* item = CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(cb)](auto*) { cb(); });
        item->setPosition(pos);
        deckMenu->addChild(item);
        return item;
    };

    // Mute button sits on Row 2 left of the volume slider
    m_muteBtn = makeBtn({"GJ_audioOnBtn_001.png"}, 13.f, {kInnerW - 122.f, kMidY}, [] {
        EditorMusicPlayer::get().toggleMute();
    });

    constexpr float kTrY = 11.f;
    makeBtn({"GJ_arrow_02_001.png"}, 11.f, {74.f, kTrY}, [] {
        EditorMusicPlayer::get().playPrevious();
    });
    m_playBtn = makeBtn({"GJ_playBtn2_001.png", "GJ_playBtn_001.png"}, 15.f, {98.f, kTrY}, [] {
        EditorMusicPlayer::get().togglePause();
    });
    if (auto* next = makeBtn({"GJ_arrow_02_001.png"}, 11.f, {122.f, kTrY}, [] {
            EditorMusicPlayer::get().playNext();
        })) {
        if (auto* spr = typeinfo_cast<CCSprite*>(next->getNormalImage())) spr->setFlipX(true);
    }

    if (auto* sep = ui::makeDivider(10.f, ui::palette::muted, 90)) {
        sep->setRotation(90.f);
        sep->setPosition({139.f, kTrY});
        m_headerDeck->addChild(sep, 2);
    }

    if (auto* shuf = makeBtn({"GJ_sortIcon_001.png", "GJ_reloadBtn_001.png"}, 13.f, {156.f, kTrY}, [] {
            EditorMusicPlayer::get().toggleShuffle();
        })) {
        m_shuffleIcon = typeinfo_cast<CCSprite*>(shuf->getNormalImage());
    }
    if (auto* rep = makeBtn({"GJ_updateBtn_001.png", "GJ_reloadBtn_001.png"}, 13.f, {180.f, kTrY}, [] {
            EditorMusicPlayer::get().cycleRepeat();
        })) {
        m_repeatIcon = typeinfo_cast<CCSprite*>(rep->getNormalImage());
        m_repeatBadge = CCLabelBMFont::create("1", "bigFont.fnt");
        m_repeatBadge->setScale(0.24f);
        m_repeatBadge->setColor(ui::palette::gold);
        m_repeatBadge->setPosition({rep->getContentSize().width * 0.82f, rep->getContentSize().height * 0.22f});
        m_repeatBadge->setVisible(false);
        rep->addChild(m_repeatBadge, 2);
    }
    m_favBtn = makeBtn({"GJ_starBtn_001.png", "GJ_starsIcon_001.png"}, 13.f, {204.f, kTrY}, [] {
        auto& player = EditorMusicPlayer::get();
        if (player.trackId().empty()) return;
        if (auto const* t = MenuMusicLibrary::get().findTrack(player.trackId())) {
            MenuMusicLibrary::get().setFavorite(t->id, !t->favorite);
        }
    });
    if (m_favBtn) {
        m_favIcon = typeinfo_cast<CCSprite*>(m_favBtn->getNormalImage());
    }
    makeBtn({"GJ_copyBtn_001.png"}, 13.f, {228.f, kTrY}, [] {
        auto& player = EditorMusicPlayer::get();
        if (player.trackId().empty()) {
            PaimonNotify::show("No hay cancion activa.", NotificationIcon::Info);
            return;
        }
        if (auto const* t = MenuMusicLibrary::get().findTrack(player.trackId())) {
            auto val = paimon::menumusic::resolveTrackCopyValue(*t);
            geode::utils::clipboard::write(val);
            PaimonNotify::show(fmt::format("Copiado: {}", val), NotificationIcon::Success);
        }
    });

    m_deckMetaLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_deckMetaLabel->setAnchorPoint({1.f, 0.5f});
    m_deckMetaLabel->setPosition({kInnerW - 10.f, kTrY});
    m_deckMetaLabel->setScale(0.38f);
    m_deckMetaLabel->setColor(ui::palette::dim);
    m_headerDeck->addChild(m_deckMetaLabel, 2);
}

void EditorMusicPickerPopup::buildTabBar() {
    if (m_tabBarContainer) {
        m_tabBarContainer->removeFromParent();
        m_tabBarContainer = nullptr;
    }
    m_tabBarContainer = kit::makeTabBar(
        kInnerW,
        {"Canciones", "Playlists", "Efectos FX", "Ajustes"},
        m_tab,
        [this](int idx) {
            m_tab = idx;
            rebuildTabContent();
        }
    );
    if (m_tabBarContainer) {
        m_tabBarContainer->setPosition({kPad, kTabBarY});
        m_mainLayer->addChild(m_tabBarContainer, 4);
    }
}

void EditorMusicPickerPopup::scheduleRebuild() {
    if (m_rebuildPending || m_closing) return;
    m_rebuildPending = true;
    Ref<EditorMusicPickerPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (!self) return;
        self->m_rebuildPending = false;
        if (self->getParent() && !self->m_closing) self->rebuildTabContent();
    });
}

void EditorMusicPickerPopup::scheduleSongsListRebuild(bool preserveScroll) {
    if (m_closing) return;
    m_songsListPreserveScroll = m_songsListPreserveScroll || preserveScroll;
    if (m_songsListRebuildPending) return;
    m_songsListRebuildPending = true;
    Ref<EditorMusicPickerPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (!self) return;
        bool const keep = self->m_songsListPreserveScroll;
        self->m_songsListRebuildPending = false;
        self->m_songsListPreserveScroll = false;
        if (self->getParent() && !self->m_closing && self->m_tab == 0) {
            self->rebuildSongsList(keep);
        }
    });
}

void EditorMusicPickerPopup::rebuildTabContent() {
    m_activeScroll = nullptr;
    m_scrollTargetSet = false;
    m_songsListHolder = nullptr;
    m_filterBtn = nullptr;
    m_sortBtn = nullptr;

    if (m_tabContent) {
        m_tabContent->removeFromParent();
        m_tabContent = nullptr;
    }

    m_tabContent = CCNode::create();
    m_tabContent->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(m_tabContent, 2);

    switch (m_tab) {
        case 0: buildSongsTab(kContentTop, kContentBottom); break;
        case 1: buildPlaylistsTab(kContentTop, kContentBottom); break;
        case 2: buildEffectsTab(kContentTop, kContentBottom); break;
        case 3: buildSettingsTab(kContentTop, kContentBottom); break;
        default: buildSongsTab(kContentTop, kContentBottom); break;
    }
}

bool EditorMusicPickerPopup::isTrackPlayable(std::string const& audioPath) const {
    if (audioPath.empty()) return false;
    auto it = m_playableCache.find(audioPath);
    if (it != m_playableCache.end()) return it->second;
    std::error_code ec;
    bool const ok = std::filesystem::is_regular_file(paimon::assets::pathFromUtf8(audioPath), ec) && !ec;
    m_playableCache.emplace(audioPath, ok);
    return ok;
}

std::vector<std::string> EditorMusicPickerPopup::filteredLibraryTracks() const {
    auto& lib = MenuMusicLibrary::get();
    auto needle = lowered(m_search);
    int const filterIdx = static_cast<int>(m_filter);
    std::vector<MusicTrack const*> matched;
    matched.reserve(lib.tracks().size());

    for (auto const& t : lib.tracks()) {
        if (t.blacklisted || !isTrackPlayable(t.audioPath)) continue;
        bool passFilter = true;
        switch (filterIdx) {
            case 1: passFilter = t.favorite; break;
            case 2: passFilter = t.source == TrackSource::GeometryDash; break;
            case 3: passFilter = t.source == TrackSource::Local || t.source == TrackSource::Downloaded; break;
            default: break;
        }
        if (!passFilter) continue;
        if (!needle.empty()) {
            bool hit = lowered(t.displayName).find(needle) != std::string::npos
                || lowered(t.artist).find(needle) != std::string::npos;
            if (!hit) continue;
        }
        matched.push_back(&t);
    }

    switch (m_sort) {
        case SongSort::Alphabetical:
            std::sort(matched.begin(), matched.end(), [](MusicTrack const* a, MusicTrack const* b) {
                return lowered(a->displayName) < lowered(b->displayName);
            });
            break;
        case SongSort::Recent:
            std::sort(matched.begin(), matched.end(), [](MusicTrack const* a, MusicTrack const* b) {
                return a->addedUnixMs > b->addedUnixMs;
            });
            break;
        case SongSort::Duration:
            std::sort(matched.begin(), matched.end(), [](MusicTrack const* a, MusicTrack const* b) {
                return a->durationMs > b->durationMs;
            });
            break;
        case SongSort::Count:
            break;
    }

    std::vector<std::string> ids;
    ids.reserve(matched.size());
    for (auto const* t : matched) ids.push_back(t->id);
    return ids;
}

CCNode* EditorMusicPickerPopup::trackRow(float width, std::string const& trackId) {
    auto const* track = MenuMusicLibrary::get().findTrack(trackId);
    if (!track) return nullptr;

    auto* row = CCNode::create();
    row->setContentSize({width, kRowH});
    row->setAnchorPoint({0.f, 0.f});

    bool const current = EditorMusicPlayer::get().trackId() == trackId;
    bool const playing = current && EditorMusicPlayer::get().isPlaying();
    if (auto* bg = ui::makeInset({width, kRowH}, current ? 145 : 85,
            current ? ui::palette::info : ui::palette::ink)) {
        row->addChild(bg, -1);
    }

    if (auto* note = firstFrame({"GJ_musicOnBtn_001.png"})) {
        limitNodeSize(note, {15.f, 15.f}, 2.f, 0.05f);
        note->setPosition({14.f, kRowH * 0.5f});
        note->setColor(current ? ui::palette::info : ui::palette::muted);
        row->addChild(note, 1);
    }

    auto* name = CCLabelBMFont::create(
        track->displayName.empty() ? trackId.c_str() : track->displayName.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->setPosition({28.f, kRowH * 0.67f});
    name->setScale(0.34f);
    name->limitLabelWidth(width - 118.f, 0.34f, 0.18f);
    if (current) name->setColor({140, 225, 255});
    row->addChild(name, 1);

    std::string subText = track->artist;
    if (track->durationMs > 0) {
        auto len = formatTime(track->durationMs);
        subText = subText.empty() ? len : fmt::format("{}  -  {}", subText, len);
    }
    char const* srcTag = track->source == TrackSource::GeometryDash ? "GD"
        : (track->source == TrackSource::Downloaded ? "YT" : "Local");
    subText = subText.empty() ? srcTag : fmt::format("{}  [{}]", subText, srcTag);

    auto* sub = CCLabelBMFont::create(subText.c_str(), "chatFont.fnt");
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({28.f, kRowH * 0.27f});
    sub->setScale(0.38f);
    sub->setColor({165, 178, 200});
    sub->limitLabelWidth(width - 118.f, 0.38f, 0.2f);
    row->addChild(sub, 1);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowH});
    menu->setTouchPriority(popupChildPrio(2));
    row->addChild(menu, 2);

    auto addRowIcon = [&](std::initializer_list<char const*> frames, float size, ccColor3B color, GLubyte opacity,
                          float x, std::function<void()> cb) {
        auto* spr = firstFrame(frames);
        if (!spr) return;
        limitNodeSize(spr, {size, size}, 2.f, 0.05f);
        spr->setColor(color);
        spr->setOpacity(opacity);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr, [cb = std::move(cb)](auto*) { cb(); });
        btn->setPosition({x, kRowH * 0.5f});
        menu->addChild(btn);
    };

    // Favorite toggle (verified GD star sprite)
    addRowIcon({"GJ_starBtn_001.png", "GJ_starsIcon_001.png"},
        13.f,
        track->favorite ? ui::palette::gold : ui::palette::dim,
        track->favorite ? 255 : 145,
        width - 72.f, [this, trackId, fav = !track->favorite] {
            MenuMusicLibrary::get().setFavorite(trackId, fav);
            scheduleSongsListRebuild(true);
        });

    // Queue next (+)
    addRowIcon({"GJ_plusBtn_001.png"}, 12.f, {255, 220, 110}, 240, width - 48.f, [trackId] {
        EditorMusicPlayer::get().enqueueNext(trackId);
        PaimonNotify::show("Agregada como siguiente en la cola", NotificationIcon::Success);
    });

    // Play / Stop
    addRowIcon({playing ? "GJ_stopMusicBtn_001.png" : "GJ_playMusicBtn_001.png"},
        16.f, {255, 255, 255}, 255, width - 20.f, [this, trackId, playing] {
            auto& player = EditorMusicPlayer::get();
            if (playing) player.stop();
            else player.play(trackId);
            if (auto* panel = EditorMusicPanel::get()) panel->refreshTrackInfo(true);
            scheduleSongsListRebuild(true);
        });

    return row;
}

void EditorMusicPickerPopup::updateSongFilterButtons() {
    setButtonCaption(m_filterBtn, fmt::format("Filtro: {}", filterLabel(static_cast<int>(m_filter))));
    setButtonCaption(m_sortBtn, fmt::format("Orden: {}", sortLabel(static_cast<int>(m_sort))));
}

void EditorMusicPickerPopup::rebuildSongsList(bool preserveScroll) {
    if (!m_songsListHolder) return;

    float savedY = 0.f;
    bool hasSavedY = false;
    if (preserveScroll && m_activeScroll && m_activeScroll->m_contentLayer) {
        savedY = m_activeScroll->m_contentLayer->getPositionY();
        hasSavedY = true;
    }

    m_activeScroll = nullptr;
    m_scrollTargetSet = false;
    m_songsListHolder->removeAllChildren();

    auto& player = EditorMusicPlayer::get();
    m_renderedTrackId = player.trackId();
    m_renderedPlaying = player.isPlaying();

    auto tracks = filteredLibraryTracks();
    std::vector<CCNode*> rows;
    rows.reserve(tracks.size());
    float const rowW = m_songsListSize.width - 8.f;
    for (auto const& tid : tracks) {
        if (auto* row = trackRow(rowW, tid)) {
            rows.push_back(row);
        }
    }

    if (rows.empty()) {
        auto* empty = CCLabelBMFont::create(
            m_search.empty()
                ? "No hay canciones en este filtro. Usa Sync, Carpeta o Agregar."
                : "Ninguna cancion coincide con tu busqueda.",
            "chatFont.fnt");
        empty->setScale(0.48f);
        empty->setColor(ui::palette::muted);
        empty->limitLabelWidth(m_songsListSize.width - 20.f, 0.48f, 0.25f);
        empty->setPosition(m_songsListSize / 2.f);
        m_songsListHolder->addChild(empty, 2);
    } else if (auto* scroll = kit::makeScrollStack(m_songsListSize, rows, 3.f)) {
        scroll->setPosition({0.f, 0.f});
        m_songsListHolder->addChild(scroll, 2);
        m_activeScroll = scroll;
        if (hasSavedY && scroll->m_contentLayer) {
            float const minY = std::min(0.f, m_songsListSize.height - scroll->m_contentLayer->getContentSize().height);
            scroll->m_contentLayer->setPositionY(std::clamp(savedY, minY, 0.f));
        }
    }

    if (m_statusLabel) {
        if (!m_search.empty() || m_filter != SongFilter::All) {
            m_statusLabel->setString(fmt::format("{} canciones encontradas", tracks.size()).c_str());
        } else {
            m_statusLabel->setString(fmt::format("{} canciones disponibles", tracks.size()).c_str());
        }
    }
}

void EditorMusicPickerPopup::buildSongsTab(float contentTop, float contentBottom) {
    float const controlsY = contentTop - 12.f; // 152

    // Compact chatFont TextInput (persists across search keystrokes so focus is never lost)
    constexpr float kSearchScale = 0.72f;
    constexpr float kSearchVisualW = 152.f;
    auto* search = TextInput::create(kSearchVisualW / kSearchScale, "Buscar cancion...", "chatFont.fnt");
    if (search) {
        search->setScale(kSearchScale);
        search->setCommonFilter(CommonFilter::Text);
        search->setMaxCharCount(64);
        search->setString(m_search);
        search->setPosition({kPad + kSearchVisualW * 0.5f, controlsY});
        search->setCallback([this](std::string const& text) {
            m_search = text;
            scheduleSongsListRebuild(false);
        });
        m_tabContent->addChild(search, 3);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(popupChildPrio(2));
    m_tabContent->addChild(menu, 3);

    // Fixed-width top pills so cycling Filter/Sort never shifts or overlaps
    auto addTopPill = [&](std::string const& text, float width, float centerX, ui::Btn skin, std::function<void()> cb) {
        auto* btn = ui::makeButton(text.c_str(), std::move(cb), skin, width, 0.50f, "goldFont.fnt");
        btn->setPosition({centerX, controlsY});
        menu->addChild(btn);
        return btn;
    };

    float const pillsStart = kPad + kSearchVisualW + 6.f; // 170
    constexpr float kFilterW = 94.f;
    constexpr float kSortW = 86.f;
    constexpr float kSyncW = 58.f;
    constexpr float kPillGap = 6.f;

    m_filterBtn = addTopPill(
        fmt::format("Filtro: {}", filterLabel(static_cast<int>(m_filter))),
        kFilterW, pillsStart + kFilterW * 0.5f, ui::Btn::Blue, [this] {
            m_filter = static_cast<SongFilter>((static_cast<int>(m_filter) + 1) % static_cast<int>(SongFilter::Count));
            updateSongFilterButtons();
            scheduleSongsListRebuild(false);
        });
    m_sortBtn = addTopPill(
        fmt::format("Orden: {}", sortLabel(static_cast<int>(m_sort))),
        kSortW, pillsStart + kFilterW + kPillGap + kSortW * 0.5f, ui::Btn::Gray, [this] {
            m_sort = static_cast<SongSort>((static_cast<int>(m_sort) + 1) % static_cast<int>(SongSort::Count));
            updateSongFilterButtons();
            scheduleSongsListRebuild(false);
        });
    addTopPill(
        "Sync",
        kSyncW, pillsStart + kFilterW + kPillGap + kSortW + kPillGap + kSyncW * 0.5f, ui::Btn::Green, [this] {
            syncDownloads();
        });

    // Footer bar (well-proportioned buttons across kInnerW)
    float const footerY = contentBottom + 9.f; // 31
    constexpr float kFooterGap = 8.f;
    constexpr float kFooterBtnW = (kInnerW - kFooterGap * 2.f) / 3.f; // ~130.6
    auto addFooterBtn = [&](char const* label, int slot, ui::Btn skin, std::function<void()> cb) {
        float const cx = kPad + kFooterBtnW * (static_cast<float>(slot) + 0.5f) + kFooterGap * static_cast<float>(slot);
        auto* btn = ui::makeButton(label, std::move(cb), skin, kFooterBtnW, 0.54f, "goldFont.fnt");
        btn->setPosition({cx, footerY});
        menu->addChild(btn);
    };
    addFooterBtn("Importar Carpeta", 0, ui::Btn::Blue, [this] { importFolder(); });
    addFooterBtn("Agregar / YouTube", 1, ui::Btn::Green, [this] { openAddMusicPopup(); });
    addFooterBtn("Newgrounds", 2, ui::Btn::Pink, [this] { openNewgroundsBrowser(); });

    // Framed song list area (y = 43..138 -> 95px tall, cleanly fits 3+ compact rows)
    float const listBot = footerY + 12.f; // 43
    float const listTop = controlsY - 14.f; // 138
    m_songsListSize = CCSize{kInnerW, std::max(40.f, listTop - listBot)};

    if (auto* listBg = ui::makeInset(m_songsListSize, 65, ui::palette::ink)) {
        listBg->setPosition({kPad, listBot});
        m_tabContent->addChild(listBg, 1);
    }

    m_songsListHolder = CCNode::create();
    m_songsListHolder->setContentSize(m_songsListSize);
    m_songsListHolder->setPosition({kPad, listBot});
    m_tabContent->addChild(m_songsListHolder, 2);

    rebuildSongsList(false);
}

void EditorMusicPickerPopup::buildPlaylistsTab(float contentTop, float contentBottom) {
    float const innerW = kit::cardInnerWidth(kInnerW);
    auto& player = EditorMusicPlayer::get();
    auto& lib = MenuMusicLibrary::get();

    m_renderedTrackId = player.trackId();
    m_renderedSource = player.source();
    m_renderedPlaylistId = player.playlistId();
    m_renderedQueueSize = player.queue().size();

    std::vector<CCNode*> items;

    // Source quick selector card
    auto* srcMenuRow = CCNode::create();
    srcMenuRow->setContentSize({innerW, 24.f});
    auto* srcMenu = CCMenu::create();
    srcMenu->setPosition({0.f, 0.f});
    srcMenu->setContentSize({innerW, 24.f});
    srcMenu->setTouchPriority(popupChildPrio(2));
    srcMenuRow->addChild(srcMenu);

    struct SrcBtn { char const* name; EditorQueueSource src; };
    SrcBtn const srcBtns[] = {
        {"Todas", EditorQueueSource::All},
        {"Favoritas", EditorQueueSource::Favorites},
        {"GD Songs", EditorQueueSource::GeometryDash},
        {"Locales", EditorQueueSource::Local},
    };
    float const step = innerW / 4.f;
    for (int i = 0; i < 4; ++i) {
        bool const active = (player.source() == srcBtns[i].src);
        auto* btn = ui::makeButton(
            srcBtns[i].name,
            [this, src = srcBtns[i].src] {
                EditorMusicPlayer::get().setSource(src);
                scheduleRebuild();
            },
            active ? ui::Btn::Green : ui::Btn::Gray,
            step - 6.f, 0.50f, "goldFont.fnt"
        );
        btn->setPosition({step * (static_cast<float>(i) + 0.5f), 12.f});
        srcMenu->addChild(btn);
    }
    items.push_back(kit::makeCard(kInnerW, "Fuente de reproduccion", {120, 210, 255}, {srcMenuRow}));

    // Playlists card
    std::vector<CCNode*> plRows;
    auto* createRow = CCNode::create();
    createRow->setContentSize({innerW, 24.f});
    constexpr float kPlInputScale = 0.70f;
    float const plInputVisW = innerW - 76.f;
    auto* plInput = TextInput::create(plInputVisW / kPlInputScale, "Nombre de nueva playlist...", "chatFont.fnt");
    if (plInput) {
        plInput->setScale(kPlInputScale);
        plInput->setCommonFilter(CommonFilter::Text);
        plInput->setMaxCharCount(48);
        plInput->setString(m_newPlaylistName);
        plInput->setPosition({plInputVisW * 0.5f, 12.f});
        plInput->setCallback([this](std::string const& s) { m_newPlaylistName = s; });
        createRow->addChild(plInput);
    }
    auto* createMenu = CCMenu::create();
    createMenu->setPosition({0.f, 0.f});
    createMenu->setTouchPriority(popupChildPrio(2));
    createRow->addChild(createMenu);
    auto* createBtn = ui::makeButton("Crear", [this] { createPlaylistFromInput(); }, ui::Btn::Green, 68.f, 0.50f, "goldFont.fnt");
    createBtn->setPosition({innerW - 34.f, 12.f});
    createMenu->addChild(createBtn);
    plRows.push_back(createRow);

    for (auto const& pl : lib.playlists()) {
        auto* row = CCNode::create();
        row->setContentSize({innerW, 26.f});
        if (auto* bg = ui::makeInset({innerW, 26.f}, 75, ui::palette::ink)) {
            row->addChild(bg, -1);
        }
        bool const active = (player.source() == EditorQueueSource::Playlist && player.playlistId() == pl.id);
        auto* lbl = CCLabelBMFont::create(
            fmt::format("{} ({})", pl.name, pl.trackIds.size()).c_str(), "bigFont.fnt");
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({8.f, 13.f});
        lbl->setScale(0.34f);
        lbl->limitLabelWidth(innerW - 134.f, 0.34f, 0.18f);
        if (active) lbl->setColor(ui::palette::gold);
        row->addChild(lbl);

        auto* m = CCMenu::create();
        m->setPosition({0.f, 0.f});
        m->setTouchPriority(popupChildPrio(2));
        row->addChild(m);

        bool const hasCur = !player.trackId().empty();
        auto* useBtn = ui::makeButton(
            active ? "Activa" : "Usar",
            [this, pid = pl.id] {
                auto& p = EditorMusicPlayer::get();
                p.setSource(EditorQueueSource::Playlist, pid);
                p.playNext();
                scheduleRebuild();
            },
            active ? ui::Btn::Green : ui::Btn::Cyan,
            54.f, 0.48f, "goldFont.fnt"
        );
        useBtn->setPosition({hasCur ? innerW - 90.f : innerW - 30.f, 13.f});
        m->addChild(useBtn);

        if (hasCur) {
            auto* addCurBtn = ui::makeButton("+Actual", [this, pid = pl.id, pname = pl.name] {
                auto tid = EditorMusicPlayer::get().trackId();
                if (!tid.empty()) {
                    MenuMusicLibrary::get().addTrackToPlaylist(pid, tid);
                    PaimonNotify::show(fmt::format("Agregada a {}", pname), NotificationIcon::Success);
                    scheduleRebuild();
                }
            }, ui::Btn::Blue, 56.f, 0.46f, "goldFont.fnt");
            addCurBtn->setPosition({innerW - 30.f, 13.f});
            m->addChild(addCurBtn);
        }
        plRows.push_back(row);
    }
    items.push_back(kit::makeCard(kInnerW, "Playlists", {255, 200, 100}, plRows));

    // Up Next queue preview
    auto upcoming = player.upcomingTrackIds(8);
    std::vector<CCNode*> qRows;
    for (auto const& id : upcoming) {
        auto const* t = lib.findTrack(id);
        if (!t) continue;
        auto* row = CCNode::create();
        row->setContentSize({innerW, 22.f});
        auto* lbl = CCLabelBMFont::create(t->displayName.c_str(), "chatFont.fnt");
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({6.f, 11.f});
        lbl->setScale(0.44f);
        lbl->limitLabelWidth(innerW - 60.f, 0.44f, 0.24f);
        row->addChild(lbl);

        auto* qMenu = CCMenu::create();
        qMenu->setPosition({0.f, 0.f});
        qMenu->setTouchPriority(popupChildPrio(2));
        row->addChild(qMenu);
        auto* playNow = ui::makeButton("Play", [this, id] {
            EditorMusicPlayer::get().play(id);
            scheduleRebuild();
        }, ui::Btn::Green, 44.f, 0.46f, "goldFont.fnt");
        playNow->setPosition({innerW - 24.f, 11.f});
        qMenu->addChild(playNow);
        qRows.push_back(row);
    }
    if (!qRows.empty()) {
        items.push_back(kit::makeCard(kInnerW, "Siguientes en cola", {140, 235, 165}, qRows));
    }

    CCSize scrollSz{kInnerW, contentTop - contentBottom};
    if (auto* scroll = kit::makeScrollStack(scrollSz, items, 6.f)) {
        scroll->setPosition({kPad, contentBottom});
        m_tabContent->addChild(scroll, 2);
        m_activeScroll = scroll;
    }
    if (m_statusLabel) {
        m_statusLabel->setString(fmt::format("Cola activa: {} canciones ({})",
            player.queue().size(), queueSourceName(player.source())).c_str());
    }
}

void EditorMusicPickerPopup::buildEffectsTab(float contentTop, float contentBottom) {
    float const innerW = kit::cardInnerWidth(kInnerW);
    auto& player = EditorMusicPlayer::get();
    auto cfg = player.fxConfig();

    std::vector<CCNode*> items;
    items.push_back(kit::makeHeroToggle(
        kInnerW,
        "Efectos de audio del editor",
        "Solo afecta al reproductor del editor (nunca a la musica ni triggers del nivel).",
        cfg.enabled,
        [this](bool enabled) {
            auto next = EditorMusicPlayer::get().fxConfig();
            next.enabled = enabled;
            EditorMusicPlayer::get().setFxConfig(next);
            scheduleRebuild();
        }
    ));

    // Preset grid (4 top + 3 bottom)
    auto* presetGrid = CCNode::create();
    presetGrid->setContentSize({innerW, 48.f});
    auto* pMenu = CCMenu::create();
    pMenu->setPosition({0.f, 0.f});
    pMenu->setTouchPriority(popupChildPrio(2));
    presetGrid->addChild(pMenu);

    float const btnW = (innerW - 18.f) / 4.f;
    for (int i = 0; i < 7; ++i) {
        auto preset = static_cast<EditorFxPreset>(i);
        bool const active = (cfg.preset == preset);
        int const col = i % 4;
        int const row = i / 4;
        float const x = btnW * (static_cast<float>(col) + 0.5f) + static_cast<float>(col) * 6.f;
        float const y = row == 0 ? 36.f : 12.f;

        auto* btn = ui::makeButton(
            fxPresetName(preset),
            [this, preset] {
                EditorMusicPlayer::get().applyFxPreset(preset);
                scheduleRebuild();
            },
            active ? ui::Btn::Cyan : ui::Btn::Gray,
            btnW, 0.48f, "goldFont.fnt"
        );
        btn->setPosition({x, y});
        pMenu->addChild(btn);
    }
    items.push_back(kit::makeCard(kInnerW, "Presets rapidos", {255, 195, 95}, {presetGrid}));

    auto updateCustom = [](auto mutator) {
        auto c = EditorMusicPlayer::get().fxConfig();
        c.enabled = true;
        c.preset = EditorFxPreset::Custom;
        mutator(c);
        EditorMusicPlayer::get().setFxConfig(c);
    };

    items.push_back(kit::makeCard(kInnerW, "Ajuste fino en vivo", {130, 215, 255}, {
        kit::makeSliderRow(innerW, "Velocidad / Tono", nullptr, cfg.speed, 0.5, 1.5,
            [](double v) { return fmt::format("{:.2f}x", v); },
            [updateCustom](double v) { updateCustom([v](EditorFxConfig& c) { c.speed = static_cast<float>(v); }); }),
        kit::makeSliderRow(innerW, "Graves (Bass)", nullptr, cfg.bassDb, -12.0, 10.0,
            [](double v) { return fmt::format("{:+.1f} dB", v); },
            [updateCustom](double v) { updateCustom([v](EditorFxConfig& c) { c.bassDb = static_cast<float>(v); }); }),
        kit::makeSliderRow(innerW, "Agudos (Treble)", nullptr, cfg.trebleDb, -12.0, 10.0,
            [](double v) { return fmt::format("{:+.1f} dB", v); },
            [updateCustom](double v) { updateCustom([v](EditorFxConfig& c) { c.trebleDb = static_cast<float>(v); }); }),
        kit::makeSliderRow(innerW, "Filtro Lowpass", nullptr, cfg.lowpassHz, 500.0, 22000.0,
            [](double v) { return v >= 1000.0 ? fmt::format("{:.1f} kHz", v / 1000.0) : fmt::format("{:.0f} Hz", v); },
            [updateCustom](double v) { updateCustom([v](EditorFxConfig& c) { c.lowpassHz = static_cast<float>(v); }); }),
        kit::makeSliderRow(innerW, "Reverb", nullptr, cfg.reverbMix, 0.0, 100.0,
            [](double v) { return fmt::format("{:.0f}%", v); },
            [updateCustom](double v) { updateCustom([v](EditorFxConfig& c) { c.reverbMix = static_cast<float>(v); }); }),
        kit::makeSliderRow(innerW, "Eco", nullptr, cfg.echoMix, 0.0, 100.0,
            [](double v) { return fmt::format("{:.0f}%", v); },
            [updateCustom](double v) { updateCustom([v](EditorFxConfig& c) { c.echoMix = static_cast<float>(v); }); }),
        kit::makeToggleRow(innerW, "Comparar A/B (sonido original)", "Desactiva FX temporalmente sin perder tus valores.",
            player.isFxBypassed(), [](bool b) { EditorMusicPlayer::get().setFxBypassed(b); }),
    }));

    CCSize scrollSz{kInnerW, contentTop - contentBottom};
    if (auto* scroll = kit::makeScrollStack(scrollSz, items, 6.f)) {
        scroll->setPosition({kPad, contentBottom});
        m_tabContent->addChild(scroll, 2);
        m_activeScroll = scroll;
    }
    if (m_statusLabel) {
        m_statusLabel->setString(cfg.enabled
            ? fmt::format("FX activo: {} ({:.2f}x)", fxPresetName(cfg.preset), cfg.speed).c_str()
            : "FX desactivado - sonido original");
    }
}

void EditorMusicPickerPopup::buildSettingsTab(float contentTop, float contentBottom) {
    float const innerW = kit::cardInnerWidth(kInnerW);
    auto& player = EditorMusicPlayer::get();

    std::vector<std::string> const playtestOpts = {
        "Pausar y reanudar", "Bajar volumen (Duck)", "Seguir sonando"
    };
    std::vector<std::string> const skinOpts = {
        "Clasico (Cafe)", "Azul GD", "Morado GD", "Oscuro GD"
    };

    std::vector<CCNode*> items = {
        kit::makeCard(kInnerW, "Comportamiento en el editor", {120, 210, 255}, {
            kit::makeSelectRow(innerW, "Durante Playtest",
                "Controla que pasa con tu musica cuando pruebas el nivel.",
                playtestOpts, static_cast<int>(player.playtestBehavior()),
                [](int idx) {
                    EditorMusicPlayer::get().setPlaytestBehavior(static_cast<PlaytestBehavior>(idx));
                }),
            kit::makeToggleRow(innerW, "Auto-reproducir al entrar",
                "Reanuda la ultima cancion al abrir el editor de niveles.",
                player.autoPlayOnEnter(),
                [](bool on) { EditorMusicPlayer::get().setAutoPlayOnEnter(on); }),
            kit::makeButtonRow(innerW, "Cancion del nivel actual",
                "Reproduce la cancion descargada de este nivel en el reproductor.",
                "Reproducir",
                [this] {
                    std::string err;
                    if (EditorMusicPlayer::get().playLevelSong(
                            LevelEditorLayer::get() ? LevelEditorLayer::get()->m_level : nullptr, &err)) {
                        PaimonNotify::show("Reproduciendo cancion del nivel", NotificationIcon::Success);
                        syncHeaderFromState();
                    } else {
                        PaimonNotify::show(err.empty() ? "No disponible" : err, NotificationIcon::Info);
                    }
                }, ui::Btn::Cyan),
        }),
        kit::makeCard(kInnerW, "Apariencia nativa y animaciones", {255, 200, 100}, {
            kit::makeSelectRow(innerW, "Marco nativo GD del panel",
                "Cambia el estilo GJ_square del reproductor flotante.",
                skinOpts, player.panelSkin(),
                [](int idx) {
                    EditorMusicPlayer::get().setPanelSkin(idx);
                    if (auto* panel = EditorMusicPanel::get()) panel->refreshSkin();
                }),
            kit::makeToggleRow(innerW, "Toast al cambiar cancion",
                "Muestra una pildora compacta sobre la barra inferior al cambiar de pista.",
                player.showToastOnChange(),
                [](bool on) { EditorMusicPlayer::get().setShowToastOnChange(on); }),
            kit::makeToggleRow(innerW, "Animaciones del reproductor",
                "Activa transiciones con rebote, ecualizador, disco giratorio y morphing.",
                player.animationsEnabled(),
                [](bool on) { EditorMusicPlayer::get().setAnimationsEnabled(on); }),
            kit::makeButtonRow(innerW, "Reiniciar posicion del panel",
                "Devuelve el reproductor flotante a la esquina superior izquierda.",
                "Centrar",
                [] {
                    if (auto* panel = EditorMusicPanel::get()) panel->resetPosition();
                    PaimonNotify::show("Posicion restaurada", NotificationIcon::Success);
                }, ui::Btn::Gray),
        }),
    };

    CCSize scrollSz{kInnerW, contentTop - contentBottom};
    if (auto* scroll = kit::makeScrollStack(scrollSz, items, 6.f)) {
        scroll->setPosition({kPad, contentBottom});
        m_tabContent->addChild(scroll, 2);
        m_activeScroll = scroll;
    }
    if (m_statusLabel) {
        m_statusLabel->setString("Atajo: Ctrl+M abre o cierra el panel flotante");
    }
}

void EditorMusicPickerPopup::syncHeaderFromState() {
    auto& player = EditorMusicPlayer::get();
    std::string const tid = player.trackId();

    if (m_nowTitle) {
        auto name = player.trackName();
        if (name.empty()) name = "Sin cancion";
        m_nowTitle->setString(name.c_str());
        m_nowTitle->limitLabelWidth(296.f, 0.42f, 0.22f);
    }
    if (m_nowSub) {
        std::string sub = tid.empty() ? "Elige una cancion abajo o presiona Play" : player.trackArtist();
        if (!tid.empty()) {
            std::string mode = queueSourceName(player.source());
            if (player.source() == EditorQueueSource::Playlist) {
                if (auto* pl = MenuMusicLibrary::get().findPlaylist(player.playlistId())) mode = pl->name;
            }
            sub = sub.empty() ? mode : fmt::format("{}  -  {}", sub, mode);
        }
        m_nowSub->setString(sub.c_str());
        m_nowSub->limitLabelWidth(296.f, 0.40f, 0.24f);
    }
    if (m_deckMetaLabel) {
        auto const& fx = player.fxConfig();
        if (fx.enabled && !player.isFxBypassed()) {
            m_deckMetaLabel->setString(fmt::format("FX: {}", fxPresetName(fx.preset)).c_str());
            m_deckMetaLabel->setColor(ui::palette::gold);
        } else {
            m_deckMetaLabel->setString(fmt::format("Vol {}%", static_cast<int>(std::round((player.isMuted() ? 0.f : player.volume()) * 100.f))).c_str());
            m_deckMetaLabel->setColor(ui::palette::dim);
        }
    }

    if (tid != m_shownTrackId) {
        m_shownTrackId = tid;
        loadHeaderCover(player.trackCoverPath(), player.trackSongId());
    }

    bool const playing = player.isPlaying();
    if (m_playBtn && playing != m_shownPlaying) {
        m_shownPlaying = playing;
        if (auto* spr = firstFrame({playing ? "GJ_pauseBtn_001.png" : "GJ_playBtn2_001.png"})) {
            replaceButtonIcon(m_playBtn, spr, 15.f);
        }
    }

    bool isFav = false;
    if (!tid.empty()) {
        if (auto const* t = MenuMusicLibrary::get().findTrack(tid)) isFav = t->favorite;
    }
    m_shownFav = isFav;
    if (m_favIcon) {
        m_favIcon->setColor(isFav ? ui::palette::gold : ui::palette::dim);
        m_favIcon->setOpacity(isFav ? 255 : 145);
    }

    bool const muted = player.isMuted() || player.volume() <= 0.005f;
    if (m_muteBtn && muted != m_shownMuted) {
        m_shownMuted = muted;
        if (auto* spr = firstFrame({muted ? "GJ_audioOffBtn_001.png" : "GJ_audioOnBtn_001.png"})) {
            spr->setColor(muted ? ccColor3B{255, 115, 115} : ccColor3B{255, 255, 255});
            replaceButtonIcon(m_muteBtn, spr, 13.f);
        }
    }

    if (m_shuffleIcon) {
        m_shuffleIcon->setColor(player.shuffle() ? ui::palette::gold : ui::palette::dim);
        m_shuffleIcon->setOpacity(player.shuffle() ? 255 : 145);
    }
    if (m_repeatIcon) {
        bool const on = player.repeat() != RepeatMode::Off;
        m_repeatIcon->setColor(on ? ui::palette::info : ui::palette::dim);
        m_repeatIcon->setOpacity(on ? 255 : 145);
    }
    if (m_repeatBadge) m_repeatBadge->setVisible(player.repeat() == RepeatMode::One);

    if (m_volSlider && !m_volDragging) {
        m_volSlider->setValue(player.isMuted() ? 0.f : player.volume());
    }
}

void EditorMusicPickerPopup::loadHeaderCover(std::string const& path, int songId) {
    if (path == m_loadedCoverPath && m_coverSprite) return;
    m_loadedCoverPath = path;
    auto const gen = ++m_coverGen;

    if (path.empty()) {
        applyHeaderCover(nullptr);
        if (songId > 0 && Mod::get()->getSavedValue<bool>("menuMusicThumbnailCovers", true)) {
            WeakRef<EditorMusicPickerPopup> weak = this;
            SongCoverCache::get().requestCovers(songId, [weak, gen](std::vector<std::string> const& paths, bool ok) {
                auto self = weak.lock();
                if (!self || self->m_closing || self->m_coverGen != gen || !ok || paths.empty()) return;
                self->loadHeaderCover(paths.front(), 0);
            });
        }
        return;
    }

    if (auto* cached = CCTextureCache::sharedTextureCache()->textureForKey(path.c_str())) {
        applyHeaderCover(cached);
        return;
    }

    WeakRef<EditorMusicPickerPopup> weak = this;
    paimon::image::loadBudgetedAsync(path, [weak, gen](CCTexture2D* tex) {
        auto self = weak.lock();
        if (!self || self->m_closing || self->m_coverGen != gen) return;
        self->applyHeaderCover(tex);
    });
}

void EditorMusicPickerPopup::applyHeaderCover(CCTexture2D* tex) {
    if (m_coverSprite) {
        m_coverSprite->removeFromParent();
        m_coverSprite = nullptr;
    }
    if (!tex || !m_coverClip) {
        if (m_coverFallback) m_coverFallback->setVisible(true);
        return;
    }
    auto* spr = CCSprite::createWithTexture(tex);
    if (!spr) {
        if (m_coverFallback) m_coverFallback->setVisible(true);
        return;
    }
    auto const clipSz = m_coverClip->getContentSize();
    auto const texSz = spr->getContentSize();
    float const scale = std::max(
        clipSz.width / std::max(1.f, texSz.width),
        clipSz.height / std::max(1.f, texSz.height));
    spr->setScale(scale);
    spr->setPosition(clipSz / 2.f);
    m_coverClip->addChild(spr, 1);
    m_coverSprite = spr;
    if (m_coverFallback) m_coverFallback->setVisible(false);
}

void EditorMusicPickerPopup::tick(float dt) {
    if (m_closing) return;
    float const safeDt = std::clamp(dt, 0.f, 0.05f);
    m_clock += safeDt;

    kit::stepWheelScroll(m_activeScroll, m_scrollTargetY, m_scrollTargetSet, safeDt);

    auto& player = EditorMusicPlayer::get();
    bool const playing = player.isPlaying();
    float const peak = player.outputPeak();

    // Disc spin
    float const targetVel = playing ? 48.f : 0.f;
    m_spinVel = m_spinVel + (targetVel - m_spinVel) * std::min(1.f, safeDt * 5.f);
    m_spinAngle = std::fmod(m_spinAngle + m_spinVel * safeDt, 360.f);
    if (m_discRotor) m_discRotor->setRotation(m_spinAngle);

    // Equalizer
    constexpr std::array<float, 5> kFreq = {6.2f, 8.4f, 7.1f, 9.3f, 5.8f};
    constexpr std::array<float, 5> kPhase = {0.4f, 1.7f, 2.9f, 4.2f, 5.5f};
    for (std::size_t i = 0; i < m_eqBars.size(); ++i) {
        float target = 0.14f;
        if (playing) {
            float const w1 = 0.5f + 0.5f * std::sin(m_clock * kFreq[i] + kPhase[i]);
            float const w2 = 0.5f + 0.5f * std::sin(m_clock * (kFreq[i] * 0.62f) + kPhase[i] * 1.7f);
            float const synth = 0.25f + 0.45f * w1 + 0.30f * w2;
            target = peak > 0.01f ? std::clamp(synth * 0.4f + peak * 0.9f, 0.14f, 1.f) : synth;
        }
        m_eqLevels[i] += (target - m_eqLevels[i]) * std::min(1.f, safeDt * 16.f);
        if (auto* bar = m_eqBars[i]) {
            bar->setContentSize({3.2f, 3.f + 13.f * m_eqLevels[i]});
            GLubyte g = static_cast<GLubyte>(245);
            GLubyte r = static_cast<GLubyte>(110 + m_eqLevels[i] * 135.f);
            bar->setColor({r, g, 85});
        }
    }

    // Seek bar & timestamps (commit seek/volume once when slider thumb is released to avoid FMOD stream stalls)
    bool const seekHeld = m_seekSlider && m_seekSlider->m_touchLogic && m_seekSlider->m_touchLogic->m_activateThumb;
    if (m_seekCommitPending && !seekHeld) {
        m_seekCommitPending = false;
        m_seekDragging = false;
        int const lenNow = player.lengthMs();
        if (lenNow > 0) {
            player.seekMs(static_cast<int>(m_seekPendingRatio * static_cast<float>(lenNow)));
        }
    }

    bool const volHeld = m_volSlider && m_volSlider->m_touchLogic && m_volSlider->m_touchLogic->m_activateThumb;
    if (m_volCommitPending && !volHeld) {
        m_volCommitPending = false;
        m_volDragging = false;
        player.setVolume(player.volume(), true);
    }

    int const len = player.lengthMs();
    int const pos = (m_seekDragging && len > 0)
        ? static_cast<int>(m_seekPendingRatio * static_cast<float>(len))
        : player.positionMs();
    float const ratio = len > 0 ? std::clamp(static_cast<float>(pos) / static_cast<float>(len), 0.f, 1.f) : 0.f;
    if (m_seekSlider && !m_seekDragging) {
        m_seekSlider->setValue(ratio);
    }
    int const posSec = std::max(0, pos) / 1000;
    int const lenSec = std::max(0, len) / 1000;
    if (posSec != m_lastSeekPosSec || lenSec != m_lastSeekLenSec) {
        m_lastSeekPosSec = posSec;
        m_lastSeekLenSec = lenSec;
        if (m_seekCurLabel) m_seekCurLabel->setString(formatTime(pos).c_str());
        if (m_seekTotLabel) m_seekTotLabel->setString(formatTime(len).c_str());
    }
    if (m_volSlider && !m_volDragging) {
        m_volSlider->setValue(player.isMuted() ? 0.f : player.volume());
    }

    if (player.trackId() != m_shownTrackId || playing != m_shownPlaying) {
        syncHeaderFromState();
    }
}

void EditorMusicPickerPopup::onSeekSliderChanged(CCObject*) {
    if (!m_seekSlider) return;
    auto* thumb = m_seekSlider->getThumb();
    float const v = std::clamp(thumb ? thumb->getValue() : m_seekSlider->getValue(), 0.f, 1.f);
    m_seekPendingRatio = v;
    m_seekDragging = true;
    m_seekCommitPending = true;
    int const len = EditorMusicPlayer::get().lengthMs();
    if (m_seekCurLabel && len > 0) {
        int const previewMs = static_cast<int>(v * static_cast<float>(len));
        int const sec = previewMs / 1000;
        if (sec != m_lastSeekPosSec) {
            m_lastSeekPosSec = sec;
            m_seekCurLabel->setString(formatTime(previewMs).c_str());
        }
    }
}

void EditorMusicPickerPopup::onVolumeSliderChanged(CCObject*) {
    if (!m_volSlider) return;
    m_volDragging = true;
    m_volCommitPending = true;
    auto* thumb = m_volSlider->getThumb();
    float const v = std::clamp(thumb ? thumb->getValue() : m_volSlider->getValue(), 0.f, 1.f);
    EditorMusicPlayer::get().setVolume(v, false);
    if (m_deckMetaLabel) {
        auto& player = EditorMusicPlayer::get();
        auto const& fx = player.fxConfig();
        if (!fx.enabled || player.isFxBypassed()) {
            m_deckMetaLabel->setString(fmt::format("Vol {}%", static_cast<int>(std::round(v * 100.f))).c_str());
        }
    }
}

void EditorMusicPickerPopup::keyDown(cocos2d::enumKeyCodes key, double timestamp) {
    if (key == cocos2d::KEY_Escape) {
        this->onClose(nullptr);
        return;
    }
    auto* kd = CCDirector::sharedDirector()->getKeyboardDispatcher();
    bool const ctrl = kd && (kd->getControlKeyPressed() || kd->getCommandKeyPressed());
    if (ctrl && (key == cocos2d::KEY_Right || key == cocos2d::KEY_ArrowRight)) {
        EditorMusicPlayer::get().playNext();
        return;
    }
    if (ctrl && (key == cocos2d::KEY_Left || key == cocos2d::KEY_ArrowLeft)) {
        EditorMusicPlayer::get().playPrevious();
        return;
    }
    Popup::keyDown(key, timestamp);
}

void EditorMusicPickerPopup::syncDownloads() {
    auto added = MenuMusicLibrary::get().syncDownloadedSongs(true);
    m_playableCache.clear();
    EditorMusicPlayer::get().refreshQueue();
    PaimonNotify::show(
        fmt::format("{} canciones descargadas anadidas", added),
        added > 0 ? NotificationIcon::Success : NotificationIcon::Info);
    scheduleSongsListRebuild(false);
}

void EditorMusicPickerPopup::importFolder() {
    Ref<EditorMusicPickerPopup> self = this;
    pt::pickFolder([self](Result<std::optional<std::filesystem::path>> result) {
        auto selected = std::move(result).unwrapOr(std::nullopt);
        if (!selected) return;
        auto added = MenuMusicLibrary::get().importFolder(*selected, true);
        EditorMusicPlayer::get().refreshQueue();
        PaimonNotify::show(
            fmt::format("{} canciones importadas", added),
            added > 0 ? NotificationIcon::Success : NotificationIcon::Warning);
        if (self && self->getParent() && !self->m_closing) {
            self->m_playableCache.clear();
            if (self->m_tab == 0) self->rebuildSongsList(false);
            else self->rebuildTabContent();
        }
    });
}

void EditorMusicPickerPopup::openAddMusicPopup() {
    if (auto* p = MenuMusicAddPopup::create()) {
        p->show();
    }
}

void EditorMusicPickerPopup::openNewgroundsBrowser() {
    if (auto* p = NewgroundsBrowserPopup::create()) {
        p->show();
    }
}

void EditorMusicPickerPopup::createPlaylistFromInput() {
    if (m_newPlaylistName.empty()) {
        PaimonNotify::show("Escribe un nombre para la playlist.", NotificationIcon::Warning);
        return;
    }
    paimon::menumusic::MusicPlaylist pl;
    pl.id = MenuMusicLibrary::get().generateId("pl");
    pl.name = m_newPlaylistName;
    pl.createdUnixMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    MenuMusicLibrary::get().addPlaylist(pl);
    m_newPlaylistName.clear();
    PaimonNotify::show(fmt::format("Playlist '{}' creada", pl.name), NotificationIcon::Success);
    scheduleRebuild();
}

} // namespace paimon::editormusic
