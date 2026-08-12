#include "EditorMusicPickerPopup.hpp"

#include "EditorMusicPanel.hpp"
#include "../services/EditorMusicPlayer.hpp"
#include "../../menu-music/services/MenuMusicLibrary.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;
using paimon::menumusic::MenuMusicLibrary;
using paimon::menumusic::MusicTrack;

namespace {

namespace kit = paimon::configkit;

constexpr float kPopupW = 380.f;
constexpr float kPopupH = 280.f;
constexpr float kRowH = 34.f;
constexpr float kScrollTop = 214.f;
constexpr float kScrollBottom = 44.f;

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string durationText(int ms) {
    if (ms <= 0) return "";
    int total = ms / 1000;
    return fmt::format("{}:{:02}", total / 60, total % 60);
}

} // namespace

namespace paimon::editormusic {

EditorMusicPickerPopup* EditorMusicPickerPopup::create() {
    auto* ret = new EditorMusicPickerPopup();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool EditorMusicPickerPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    setTitle("Musica del editor");
    setID("editor-music-picker"_spr);

    MenuMusicLibrary::get().load();
    EditorMusicPlayer::get().refreshQueue();

    auto* search = TextInput::create(200.f, "Buscar cancion...");
    if (search) {
        search->setPosition({kPopupW / 2.f - 62.f, kPopupH - 62.f});
        search->setCallback([this](std::string const& text) {
            m_search = text;
            scheduleRebuild();
        });
        m_mainLayer->addChild(search, 3);
    }

    auto* headerMenu = CCMenu::create();
    headerMenu->setPosition({0.f, 0.f});
    headerMenu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    m_mainLayer->addChild(headerMenu, 3);

    auto addHeaderButton = [&](char const* text, float x, std::function<void()> onPress) {
        auto* sprite = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_05.png", 0.5f);
        if (!sprite) return;
        sprite->setScale(0.7f);
        auto* button = CCMenuItemExt::createSpriteExtra(
            sprite, [onPress = std::move(onPress)](auto*) { onPress(); });
        if (!button) return;
        button->setPosition({x, kPopupH - 62.f});
        headerMenu->addChild(button);
    };

    addHeaderButton("Sync", kPopupW / 2.f + 58.f, [this] { syncDownloads(); });
    addHeaderButton("Carpeta", kPopupW / 2.f + 128.f, [this] { importFolder(); });

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setAnchorPoint({0.5f, 0.5f});
    m_statusLabel->setPosition({kPopupW / 2.f, 26.f});
    m_statusLabel->setScale(0.5f);
    m_statusLabel->setColor({166, 176, 198});
    m_mainLayer->addChild(m_statusLabel, 3);

    rebuild();
    return true;
}

void EditorMusicPickerPopup::scheduleRebuild() {
    Ref<EditorMusicPickerPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (self && self->getParent()) self->rebuild();
    });
}

std::vector<std::string> EditorMusicPickerPopup::filteredTracks() const {
    auto const& queue = EditorMusicPlayer::get().queue();
    if (m_search.empty()) return queue;

    auto needle = lowered(m_search);
    std::vector<std::string> out;
    for (auto const& id : queue) {
        auto const* track = MenuMusicLibrary::get().findTrack(id);
        if (!track) continue;
        if (lowered(track->displayName).find(needle) != std::string::npos ||
            lowered(track->artist).find(needle) != std::string::npos) {
            out.push_back(id);
        }
    }
    return out;
}

CCNode* EditorMusicPickerPopup::trackRow(float width, std::string const& trackId) {
    auto const* track = MenuMusicLibrary::get().findTrack(trackId);
    if (!track) return nullptr;

    auto* row = CCNode::create();
    row->setContentSize({width, kRowH});
    row->setAnchorPoint({0.f, 0.f});

    bool current = EditorMusicPlayer::get().trackId() == trackId;
    bool playing = current && EditorMusicPlayer::get().isPlaying();
    if (auto* bg = SpriteHelper::createColorPanel(
            width, kRowH, current ? ccColor3B{28, 60, 96} : ccColor3B{14, 18, 32}, 190, 6.f)) {
        bg->setAnchorPoint({0.f, 0.f});
        row->addChild(bg, -1);
    }

    auto* name = CCLabelBMFont::create(
        track->displayName.empty() ? trackId.c_str() : track->displayName.c_str(), "bigFont.fnt");
    name->setAnchorPoint({0.f, 0.5f});
    name->setPosition({10.f, kRowH * 0.64f});
    name->setScale(0.4f);
    name->limitLabelWidth(width - 90.f, 0.4f, 0.2f);
    if (current) name->setColor({120, 200, 255});
    row->addChild(name);

    auto subtitle = track->artist;
    if (auto length = durationText(track->durationMs); !length.empty()) {
        subtitle = subtitle.empty() ? length : subtitle + "  -  " + length;
    }
    if (!subtitle.empty()) {
        auto* sub = CCLabelBMFont::create(subtitle.c_str(), "chatFont.fnt");
        sub->setAnchorPoint({0.f, 0.5f});
        sub->setPosition({10.f, kRowH * 0.26f});
        sub->setScale(0.42f);
        sub->setColor({150, 158, 175});
        sub->limitLabelWidth(width - 90.f, 0.42f, 0.2f);
        row->addChild(sub);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setContentSize({width, kRowH});
    menu->setTouchPriority(CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    row->addChild(menu, 2);

    auto* icon = SpriteHelper::safeCreateWithFrameName(
        playing ? "GJ_stopMusicBtn_001.png" : "GJ_playMusicBtn_001.png");
    if (icon) {
        icon->setScale(0.5f);
        auto* button = CCMenuItemExt::createSpriteExtra(icon, [this, trackId, playing](auto*) {
            auto& player = EditorMusicPlayer::get();
            if (playing) player.stop();
            else player.play(trackId);
            if (auto* panel = EditorMusicPanel::get()) panel->refreshTrackInfo();
            scheduleRebuild();
        });
        if (button) {
            button->setPosition({width - 24.f, kRowH / 2.f});
            menu->addChild(button);
        }
    }

    return row;
}

void EditorMusicPickerPopup::rebuild() {
    if (m_content) {
        m_content->removeFromParent();
        m_content = nullptr;
    }

    m_content = CCNode::create();
    m_content->setContentSize(m_mainLayer->getContentSize());
    m_mainLayer->addChild(m_content, 2);

    auto tracks = filteredTracks();
    float const width = kPopupW - 40.f;

    std::vector<CCNode*> rows;
    rows.reserve(tracks.size());
    for (auto const& id : tracks) {
        if (auto* row = trackRow(width, id)) rows.push_back(row);
    }

    if (rows.empty()) {
        auto* empty = CCLabelBMFont::create(
            m_search.empty()
                ? "No hay canciones. Usa Sync o Carpeta para anadirlas."
                : "Ninguna cancion coincide con la busqueda.",
            "chatFont.fnt");
        empty->setScale(0.6f);
        empty->setPosition({kPopupW / 2.f, (kScrollTop + kScrollBottom) / 2.f});
        empty->setColor({166, 176, 198});
        m_content->addChild(empty);
    } else {
        CCSize scrollSize{width + 8.f, kScrollTop - kScrollBottom};
        if (auto* scroll = kit::makeScrollStack(scrollSize, rows, 6.f)) {
            scroll->setPosition({(kPopupW - scrollSize.width) / 2.f, kScrollBottom});
            m_content->addChild(scroll);
        }
    }

    if (m_statusLabel) {
        m_statusLabel->setString(
            fmt::format("{} canciones  -  Ctrl+M abre el panel en el editor", tracks.size()).c_str());
    }
}

void EditorMusicPickerPopup::syncDownloads() {
    auto added = MenuMusicLibrary::get().syncDownloadedSongs(true);
    EditorMusicPlayer::get().refreshQueue();
    PaimonNotify::show(
        fmt::format("{} canciones descargadas anadidas", added),
        added > 0 ? NotificationIcon::Success : NotificationIcon::Info);
    scheduleRebuild();
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
        if (self && self->getParent()) self->rebuild();
    });
}

} // namespace paimon::editormusic
