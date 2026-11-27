#include "ReferenceManagerPopup.hpp"

#include "../ReferenceText.hpp"
#include "../services/ReferenceSession.hpp"
#include "../services/ReferenceStore.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editorref {

namespace kit = paimon::editor::kit;

namespace {

constexpr float kPopupW = 456.f;
constexpr float kPopupH = 296.f;

constexpr float kListX = 14.f;
constexpr float kListY = 28.f;
constexpr float kListW = 156.f;
constexpr float kListH = 198.f;

constexpr float kInspX = 178.f;
constexpr float kInspY = 28.f;
constexpr float kInspW = 264.f;
constexpr float kInspH = 198.f;

constexpr float kHistX = 14.f;
constexpr float kHistY = 28.f;
constexpr float kHistW = 428.f;
constexpr float kHistH = 198.f;

std::string truncateName(std::string const& name, std::size_t maxChars) {
    if (name.size() <= maxChars) return name;
    if (maxChars <= 3) return name.substr(0, maxChars);
    return name.substr(0, maxChars - 3) + "...";
}

CCMenuItemSpriteExtra* makeIconButton(
    char const* plateTexture,
    char const* paimIcon,
    char const* fallbackIcon,
    CCSize size,
    std::function<void()> onPress
) {
    auto* body = CCNode::create();
    body->setContentSize(size);
    body->setAnchorPoint({0.5f, 0.5f});

    if (auto* bg = NineSlice::create(plateTexture)) {
        bg->setContentSize(size);
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        body->addChild(bg, -1);
    }

    auto* icon = kit::glyph(paimIcon, fallbackIcon, std::min(size.width, size.height) * 0.58f);
    icon->setPosition({size.width * 0.5f, size.height * 0.5f + 1.f});
    body->addChild(icon, 1);

    return CCMenuItemExt::createSpriteExtra(body, [cb = std::move(onPress)](auto*) {
        if (cb) cb();
    });
}

} // namespace

ReferenceManagerPopup* ReferenceManagerPopup::create(int initialSelectedId) {
    if (!paimon::modules::isEnabled("paimbnails.reference.editor")) return nullptr;
    auto* popup = new ReferenceManagerPopup();
    if (popup && popup->init(initialSelectedId)) {
        popup->autorelease();
        return popup;
    }
    CC_SAFE_DELETE(popup);
    return nullptr;
}

bool ReferenceManagerPopup::init(int initialSelectedId) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    setID("reference-manager-popup"_spr);
    setTitle(tr("Reference Images", "Imagenes de Referencia"));

    m_selectedId = initialSelectedId;
    m_tab = Tab::Level;

    auto* topMenu = CCMenu::create();
    topMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(topMenu, 5);

    float const topY = kListY + kListH + 18.f;

    auto addPill = kit::pill(
        "paim_anim_frameAdd.png",
        "GJ_plusBtn_001.png",
        tr("Add Image", "Anadir"),
        "GJ_button_01.png",
        {104.f, 24.f},
        [this] { pickNewImage(); }
    );
    addPill.button->setPosition({kListX + 52.f, topY});
    topMenu->addChild(addPill.button);

    auto pastePill = kit::pill(
        "paim_ui_showThumb.png",
        "GJ_pasteBtn_001.png",
        tr("Paste", "Pegar"),
        "GJ_button_05.png",
        {70.f, 24.f},
        [this] { pasteClipboardImage(); }
    );
    pastePill.button->setPosition({kListX + 104.f + 6.f + 35.f, topY});
    topMenu->addChild(pastePill.button);

    m_levelTabPill = kit::pill(
        nullptr,
        nullptr,
        tr("Level", "Nivel"),
        "GJ_button_02.png",
        {90.f, 24.f},
        [this] { setTab(Tab::Level); },
        "bigFont.fnt"
    );
    m_levelTabPill.button->setPosition({kListX + 104.f + 6.f + 70.f + 12.f + 45.f, topY});
    topMenu->addChild(m_levelTabPill.button);

    m_historyTabPill = kit::pill(
        nullptr,
        nullptr,
        tr("History", "Historial"),
        "GJ_button_04.png",
        {104.f, 24.f},
        [this] { setTab(Tab::History); },
        "bigFont.fnt"
    );
    m_historyTabPill.button->setPosition({kListX + 104.f + 6.f + 70.f + 12.f + 90.f + 5.f + 52.f, topY});
    topMenu->addChild(m_historyTabPill.button);

    m_countLabel = kit::label("", "chatFont.fnt", 0.54f, kit::tint::muted);
    m_countLabel->setAnchorPoint({1.f, 0.5f});
    m_countLabel->setPosition({kInspX + kInspW - 2.f, topY});
    m_mainLayer->addChild(m_countLabel, 5);

    // Level view (left list + right inspector).
    m_levelView = CCNode::create();
    m_levelView->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_levelView, 2);

    m_levelView->addChild(kit::inset({kListX, kListY, kListW, kListH}, 125), 1);
    m_levelView->addChild(kit::inset({kInspX, kInspY, kInspW, kInspH}, 115), 1);

    m_listContainer = CCNode::create();
    m_listContainer->setPosition({kListX + 4.f, kListY + 4.f});
    m_listContainer->setContentSize({kListW - 8.f, kListH - 8.f});
    m_levelView->addChild(m_listContainer, 4);

    m_inspectorContainer = CCNode::create();
    m_inspectorContainer->setPosition({kInspX, kInspY});
    m_inspectorContainer->setContentSize({kInspW, kInspH});
    m_levelView->addChild(m_inspectorContainer, 4);

    // History view (full-width inset panel).
    m_historyView = CCNode::create();
    m_historyView->setPosition({0.f, 0.f});
    m_historyView->setVisible(false);
    m_mainLayer->addChild(m_historyView, 2);

    m_historyView->addChild(kit::inset({kHistX, kHistY, kHistW, kHistH}, 125), 1);

    m_historyContainer = CCNode::create();
    m_historyContainer->setPosition({kHistX, kHistY});
    m_historyContainer->setContentSize({kHistW, kHistH});
    m_historyView->addChild(m_historyContainer, 4);

    m_tipLabel = kit::label("", "chatFont.fnt", 0.50f, kit::tint::muted);
    m_tipLabel->setPosition({kPopupW * 0.5f, 14.f});
    m_mainLayer->addChild(m_tipLabel, 5);

    refreshAll();
    return true;
}

void ReferenceManagerPopup::onClose(CCObject* sender) {
    if (auto* ed = ReferenceSession::get().editor()) {
        ReferenceSession::get().persist(ed);
    }
    PaimonPopup::onClose(sender);
}

void ReferenceManagerPopup::setTab(Tab tab) {
    if (m_tab == tab) return;
    m_tab = tab;
    refreshAll();
}

void ReferenceManagerPopup::refreshTabBar() {
    auto const levelCount = ReferenceSession::get().activeEntries().size();
    auto const histCount = store::loadHistory().size();

    kit::setPillText(
        m_levelTabPill,
        fmt::format("{} ({})", tr("Level", "Nivel"), levelCount)
    );
    kit::setPillSkin(
        m_levelTabPill,
        m_tab == Tab::Level ? "GJ_button_02.png" : "GJ_button_04.png"
    );

    kit::setPillText(
        m_historyTabPill,
        fmt::format("{} ({})", tr("History", "Historial"), histCount)
    );
    kit::setPillSkin(
        m_historyTabPill,
        m_tab == Tab::History ? "GJ_button_02.png" : "GJ_button_04.png"
    );

    if (m_countLabel) {
        if (m_tab == Tab::Level) {
            m_countLabel->setString(fmt::format("{}/{}", levelCount, kMaxReferencesPerLevel).c_str());
        } else {
            m_countLabel->setString(fmt::format("{}/{}", histCount, kMaxHistoryEntries).c_str());
        }
    }

    if (m_tipLabel) {
        if (m_tab == Tab::Level) {
            m_tipLabel->setString(tr(
                "Select in editor to Move, Rotate, Scale, Warp or Edit Object.",
                "Selecciona en el editor para mover, rotar, escalar, warp o Edit Object."
            ));
        } else {
            m_tipLabel->setString(tr(
                "Global history shared across all levels. Deleting here does not affect existing levels.",
                "Historial global entre todos los niveles. Borrar aqui no afecta niveles existentes."
            ));
        }
        m_tipLabel->limitLabelWidth(kPopupW - 28.f, 0.50f, 0.28f);
    }
}

void ReferenceManagerPopup::refreshAll() {
    bool const isLevel = (m_tab == Tab::Level);
    if (m_levelView) m_levelView->setVisible(isLevel);
    if (m_historyView) m_historyView->setVisible(!isLevel);

    if (isLevel) {
        if (m_historyContainer) m_historyContainer->removeAllChildrenWithCleanup(true);
        rebuildList();
        rebuildInspector();
    } else {
        if (m_listContainer) m_listContainer->removeAllChildrenWithCleanup(true);
        if (m_inspectorContainer) m_inspectorContainer->removeAllChildrenWithCleanup(true);
        m_opacitySlider = nullptr;
        m_opacityValueLabel = nullptr;
        m_previewSprite = nullptr;
        rebuildHistory();
    }
    refreshTabBar();
}

void ReferenceManagerPopup::pickNewImage() {
    WeakRef<ReferenceManagerPopup> selfRef = this;
    pt::pickImage([selfRef](Result<std::optional<std::filesystem::path>> res) {
        if (!res) {
            PaimonNotify::show(res.unwrapErr(), NotificationIcon::Error, 3.f);
            return;
        }
        auto const optPath = res.unwrap();
        if (!optPath || optPath->empty()) return;

        auto added = ReferenceSession::get().addFromFile(*optPath);
        if (!added) {
            PaimonNotify::show(added.unwrapErr(), NotificationIcon::Error, 3.5f);
            return;
        }
        PaimonNotify::show(
            tr("Reference image added", "Imagen de referencia anadida"),
            NotificationIcon::Success,
            2.f
        );
        if (auto popup = selfRef.lock()) {
            popup->m_selectedId = added.unwrap();
            popup->m_tab = Tab::Level;
            popup->refreshAll();
        }
    });
}

void ReferenceManagerPopup::pasteClipboardImage() {
    auto added = ReferenceSession::get().addFromClipboard();
    if (!added) {
        PaimonNotify::show(
            tr("No image in clipboard", "No hay imagen en el portapapeles"),
            NotificationIcon::Warning,
            2.5f
        );
        return;
    }
    PaimonNotify::show(
        tr("Pasted reference image", "Imagen pegada como referencia"),
        NotificationIcon::Success,
        2.f
    );
    m_selectedId = added.unwrap();
    m_tab = Tab::Level;
    refreshAll();
}

void ReferenceManagerPopup::useHistoryEntry(std::string const& historyId) {
    auto added = ReferenceSession::get().addFromHistory(historyId);
    if (!added) {
        PaimonNotify::show(added.unwrapErr(), NotificationIcon::Error, 3.f);
        refreshAll();
        return;
    }
    PaimonNotify::show(
        tr("Image added from history", "Imagen anadida desde el historial"),
        NotificationIcon::Success,
        2.f
    );
    m_selectedId = added.unwrap();
    m_tab = Tab::Level;
    refreshAll();
}

void ReferenceManagerPopup::deleteHistoryEntry(std::string const& historyId) {
    if (store::removeHistoryEntry(historyId)) {
        PaimonNotify::show(
            tr("Removed from history", "Eliminada del historial"),
            NotificationIcon::Info,
            1.6f
        );
    }
    refreshAll();
}

void ReferenceManagerPopup::confirmClearHistory() {
    WeakRef<ReferenceManagerPopup> selfRef = this;
    geode::createQuickPopup(
        tr("Clear History", "Borrar Historial"),
        tr(
            "Delete <cr>all</c> saved images from global history?\nImages already placed in levels will not be affected.",
            "Borrar <cr>todas</c> las imagenes del historial global?\nLas imagenes ya puestas en niveles no se veran afectadas."
        ),
        tr("Cancel", "Cancelar"),
        tr("Clear All", "Borrar todo"),
        [selfRef](auto*, bool confirmed) {
            if (!confirmed) return;
            store::clearHistory();
            PaimonNotify::show(
                tr("History cleared", "Historial borrado"),
                NotificationIcon::Info,
                2.f
            );
            if (auto popup = selfRef.lock()) {
                popup->refreshAll();
            }
        }
    );
}

void ReferenceManagerPopup::rebuildHistory() {
    if (!m_historyContainer) return;
    m_historyContainer->removeAllChildrenWithCleanup(true);

    auto const history = store::loadHistory();

    // Header bar inside history panel.
    float const headerY = kHistH - 16.f;
    auto* headerLbl = kit::label(
        tr("Global Image History", "Historial Global de Imagenes"),
        "goldFont.fnt",
        0.46f,
        kit::tint::white
    );
    headerLbl->setAnchorPoint({0.f, 0.5f});
    headerLbl->setPosition({10.f, headerY});
    m_historyContainer->addChild(headerLbl, 4);

    if (!history.empty()) {
        auto* headerMenu = CCMenu::create();
        headerMenu->setPosition({0.f, 0.f});
        m_historyContainer->addChild(headerMenu, 5);

        auto clearAllPill = kit::pill(
            "paim_anim_frameDel.png",
            "GJ_trashBtn_001.png",
            tr("Clear All", "Borrar todo"),
            "GJ_button_06.png",
            {106.f, 20.f},
            [this] { confirmClearHistory(); },
            "bigFont.fnt"
        );
        clearAllPill.button->setPosition({kHistW - 60.f, headerY});
        headerMenu->addChild(clearAllPill.button);
    }

    auto* divider = CCLayerColor::create({255, 255, 255, 28}, kHistW - 16.f, 1.f);
    divider->setPosition({8.f, kHistH - 30.f});
    m_historyContainer->addChild(divider, 2);

    CCSize const scrollArea{kHistW - 8.f, kHistH - 36.f};

    if (history.empty()) {
        auto* empty = kit::label(
            tr(
                "History is empty.\nImages you add or paste will be saved here globally.",
                "El historial esta vacio.\nLas imagenes que anadas o pegues se guardaran aqui."
            ),
            "chatFont.fnt",
            0.58f,
            kit::tint::muted
        );
        empty->setAlignment(kCCTextAlignmentCenter);
        empty->setPosition({kHistW * 0.5f, scrollArea.height * 0.5f + 4.f});
        m_historyContainer->addChild(empty, 3);
        return;
    }

    auto* scroll = ScrollLayer::create(scrollArea);
    scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({4.f, 4.f});
    m_historyContainer->addChild(scroll, 4);

    constexpr float kRowH = 44.f;
    constexpr float kGap = 4.f;
    float const totalH = std::max(
        scrollArea.height,
        static_cast<float>(history.size()) * (kRowH + kGap) + 6.f
    );
    scroll->m_contentLayer->setContentSize({scrollArea.width, totalH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    scroll->m_contentLayer->addChild(menu, 5);

    for (std::size_t i = 0; i < history.size(); ++i) {
        auto const& h = history[i];
        float const y = totalH - 3.f - static_cast<float>(i) * (kRowH + kGap) - kRowH * 0.5f;

        auto* rowBg = CCLayerColor::create(
            (i % 2 == 0) ? ccColor4B{25, 30, 44, 150} : ccColor4B{33, 40, 58, 150},
            scrollArea.width - 4.f,
            kRowH
        );
        rowBg->ignoreAnchorPointForPosition(false);
        rowBg->setAnchorPoint({0.5f, 0.5f});
        rowBg->setPosition({scrollArea.width * 0.5f, y});
        scroll->m_contentLayer->addChild(rowBg, 1);

        // Thumbnail preview box.
        constexpr float kThumbW = 38.f;
        constexpr float kThumbH = 34.f;
        float const thumbX = 8.f;
        float const thumbY = y - kThumbH * 0.5f;

        auto* checker = kit::checker({kThumbW, kThumbH}, 5.f, {32, 36, 48}, {46, 52, 68});
        checker->setPosition({thumbX, thumbY});
        scroll->m_contentLayer->addChild(checker, 2);

        auto loaded = ImageLoadHelper::loadStaticImage(
            store::resolveHistoryAssetPath(h.assetFile),
            20,
            false
        );
        if (loaded.success && loaded.texture) {
            auto* spr = CCSprite::createWithTexture(loaded.texture);
            loaded.texture->release();
            if (spr) {
                float const sw = std::max(1.f, spr->getContentSize().width);
                float const sh = std::max(1.f, spr->getContentSize().height);
                float const fit = std::min((kThumbW - 4.f) / sw, (kThumbH - 4.f) / sh);
                spr->setScale(fit);
                spr->setPosition({thumbX + kThumbW * 0.5f, y});
                scroll->m_contentLayer->addChild(spr, 3);
            }
        }

        // Name + resolution info.
        float const textX = thumbX + kThumbW + 10.f;
        float const textMaxW = scrollArea.width - textX - 122.f;

        auto* nameLbl = kit::label(
            truncateName(h.name, 30).c_str(),
            "goldFont.fnt",
            0.42f,
            kit::tint::white
        );
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({textX, y + 8.f});
        nameLbl->limitLabelWidth(textMaxW, 0.42f, 0.16f);
        scroll->m_contentLayer->addChild(nameLbl, 3);

        std::string subtitle = (h.width > 0 && h.height > 0)
            ? fmt::format("{} x {} px", h.width, h.height)
            : std::string(tr("Saved image", "Imagen guardada"));
        auto* dimLbl = kit::label(subtitle.c_str(), "chatFont.fnt", 0.50f, kit::tint::muted);
        dimLbl->setAnchorPoint({0.f, 0.5f});
        dimLbl->setPosition({textX, y - 9.f});
        scroll->m_contentLayer->addChild(dimLbl, 3);

        // Use button.
        std::string const entryId = h.id;
        auto usePill = kit::pill(
            "paim_anim_frameAdd.png",
            "GJ_plusBtn_001.png",
            tr("Use", "Usar"),
            "GJ_button_01.png",
            {72.f, 24.f},
            [this, entryId] { useHistoryEntry(entryId); }
        );
        usePill.button->setPosition({scrollArea.width - 76.f, y});
        menu->addChild(usePill.button);

        // Delete individual entry button.
        auto* delBtn = makeIconButton(
            "GJ_button_06.png",
            "paim_anim_frameDel.png",
            "GJ_trashBtn_001.png",
            {26.f, 24.f},
            [this, entryId] { deleteHistoryEntry(entryId); }
        );
        delBtn->setPosition({scrollArea.width - 21.f, y});
        menu->addChild(delBtn);
    }

    scroll->moveToTop();
}

void ReferenceManagerPopup::rebuildList() {
    if (!m_listContainer) return;
    m_listContainer->removeAllChildrenWithCleanup(true);

    auto const entries = ReferenceSession::get().activeEntries();
    if (m_countLabel) {
        m_countLabel->setString(
            fmt::format("{} / {}", entries.size(), kMaxReferencesPerLevel).c_str()
        );
    }

    if (entries.empty()) {
        m_selectedId = 0;
        auto* empty = kit::label(
            tr("No images yet.\nUse Add Image or Paste.", "Sin imagenes aun.\nUsa Anadir o Pegar."),
            "chatFont.fnt",
            0.55f,
            kit::tint::muted
        );
        empty->setAlignment(kCCTextAlignmentCenter);
        auto const sz = m_listContainer->getContentSize();
        empty->setPosition({sz.width * 0.5f, sz.height * 0.5f});
        m_listContainer->addChild(empty);
        return;
    }

    bool found = false;
    for (auto const& e : entries) {
        if (e.id == m_selectedId) {
            found = true;
            break;
        }
    }
    if (!found) {
        m_selectedId = entries.back().id;
    }

    auto const area = m_listContainer->getContentSize();
    auto* scroll = ScrollLayer::create(area);
    scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({0.f, 0.f});
    m_listContainer->addChild(scroll);

    constexpr float kRowH = 26.f;
    constexpr float kGap = 3.f;
    float const totalH = std::max(
        area.height,
        static_cast<float>(entries.size()) * (kRowH + kGap) + 6.f
    );
    scroll->m_contentLayer->setContentSize({area.width, totalH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    scroll->m_contentLayer->addChild(menu, 2);

    for (std::size_t i = 0; i < entries.size(); ++i) {
        auto const& e = entries[i];
        bool const selected = (e.id == m_selectedId);
        float const y = totalH - 3.f - static_cast<float>(i) * (kRowH + kGap) - kRowH * 0.5f;

        auto* rowBg = CCLayerColor::create(
            selected ? ccColor4B{50, 120, 175, 180} : ccColor4B{25, 30, 44, 130},
            area.width - 4.f,
            kRowH
        );
        rowBg->ignoreAnchorPointForPosition(false);
        rowBg->setAnchorPoint({0.5f, 0.5f});
        rowBg->setPosition({area.width * 0.5f, y});
        scroll->m_contentLayer->addChild(rowBg, 1);

        auto* btnNode = CCNode::create();
        btnNode->setContentSize({area.width - 4.f, kRowH});
        auto* btn = CCMenuItemExt::createSpriteExtra(btnNode, [this, id = e.id](CCMenuItemSpriteExtra*) {
            if (m_selectedId == id) return;
            m_selectedId = id;
            rebuildList();
            rebuildInspector();
        });
        btn->setPosition({area.width * 0.5f, y});
        menu->addChild(btn);

        auto* nameLbl = kit::label(
            truncateName(e.name, 15).c_str(),
            "chatFont.fnt",
            0.54f,
            selected ? kit::tint::white : kit::tint::label
        );
        nameLbl->setAnchorPoint({0.f, 0.5f});
        nameLbl->setPosition({8.f, y});
        nameLbl->limitLabelWidth(area.width - 40.f, 0.54f, 0.22f);
        scroll->m_contentLayer->addChild(nameLbl, 3);

        float iconX = area.width - 12.f;
        if (e.locked) {
            auto* lockIcon = kit::glyph("paim_anim_lock.png", "GJ_lock_001.png", 11.f, kit::tint::gold);
            lockIcon->setPosition({iconX, y});
            scroll->m_contentLayer->addChild(lockIcon, 3);
            iconX -= 14.f;
        }
        if (!e.visible) {
            auto* eyeIcon = kit::glyph("paim_anim_eyeOff.png", "GJ_deleteIcon_001.png", 11.f, kit::tint::off);
            eyeIcon->setPosition({iconX, y});
            scroll->m_contentLayer->addChild(eyeIcon, 3);
        }
    }

    scroll->moveToTop();
}

void ReferenceManagerPopup::rebuildInspector() {
    if (!m_inspectorContainer) return;
    m_inspectorContainer->removeAllChildrenWithCleanup(true);
    m_opacitySlider = nullptr;
    m_opacityValueLabel = nullptr;
    m_previewSprite = nullptr;

    auto const* entry = ReferenceSession::get().findEntry(m_selectedId);
    if (!entry) {
        auto* hint = kit::label(
            tr(
                "Add a reference image to place it\nin the editor like a real GD object.\n\nMove, Rotate, Scale & Warp freely!",
                "Anade una imagen de referencia para\ncolocarla como un objeto real de GD.\n\nMuevela, rotala, escalala y haz Warp!"
            ),
            "chatFont.fnt",
            0.56f,
            kit::tint::muted
        );
        hint->setAlignment(kCCTextAlignmentCenter);
        hint->setPosition({kInspW * 0.5f, kInspH * 0.5f});
        m_inspectorContainer->addChild(hint);
        return;
    }

    int const id = entry->id;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_inspectorContainer->addChild(menu, 5);

    // Top section: preview box on the left, name + select/flip/reset on the right.
    constexpr float kPrevX = 10.f;
    constexpr float kPrevY = kInspH - 68.f;
    constexpr float kPrevW = 78.f;
    constexpr float kPrevH = 58.f;

    auto* checker = kit::checker({kPrevW, kPrevH}, 7.f, {32, 36, 48}, {46, 52, 68});
    checker->setPosition({kPrevX, kPrevY});
    m_inspectorContainer->addChild(checker, 1);

    auto loaded = ImageLoadHelper::loadStaticImage(store::resolveAssetPath(entry->assetFile), 20, false);
    if (loaded.success && loaded.texture) {
        auto* spr = CCSprite::createWithTexture(loaded.texture);
        loaded.texture->release();
        if (spr) {
            float const sw = std::max(1.f, spr->getContentSize().width);
            float const sh = std::max(1.f, spr->getContentSize().height);
            float const fit = std::min((kPrevW - 4.f) / sw, (kPrevH - 4.f) / sh);
            spr->setScale(fit);
            spr->setPosition({kPrevX + kPrevW * 0.5f, kPrevY + kPrevH * 0.5f});
            spr->setOpacity(static_cast<GLubyte>(std::clamp(entry->opacity, 0, 255)));
            spr->setFlipX(entry->flipX);
            spr->setFlipY(entry->flipY);
            m_inspectorContainer->addChild(spr, 2);
            m_previewSprite = spr;
        }
    }

    float const rightX = kPrevX + kPrevW + 10.f;
    float const rightW = kInspW - rightX - 10.f;

    auto* titleLbl = kit::label(
        truncateName(entry->name, 20).c_str(),
        "goldFont.fnt",
        0.48f,
        kit::tint::white
    );
    titleLbl->setAnchorPoint({0.f, 0.5f});
    titleLbl->setPosition({rightX, kInspH - 18.f});
    titleLbl->limitLabelWidth(rightW - 4.f, 0.48f, 0.14f);
    m_inspectorContainer->addChild(titleLbl, 4);

    auto selectPill = kit::pill(
        "paim_anim_select.png",
        "GJ_playBtn2_001.png",
        tr("Select in Editor", "Seleccionar"),
        "GJ_button_01.png",
        {rightW, 21.f},
        [this, id] {
            if (ReferenceSession::get().selectAndFocus(id, true)) {
                this->onClose(nullptr);
            }
        }
    );
    selectPill.button->setPosition({rightX + rightW * 0.5f, kInspH - 38.f});
    menu->addChild(selectPill.button);

    float const smallW = (rightW - 8.f) / 3.f;
    float const smallY = kInspH - 61.f;

    auto flipXPill = kit::pill(
        nullptr, nullptr, "Flip X", "GJ_button_04.png", {smallW, 18.f},
        [this, id] {
            ReferenceSession::get().flipEntry(id, false);
            refreshPreview();
        },
        "bigFont.fnt"
    );
    flipXPill.button->setPosition({rightX + smallW * 0.5f, smallY});
    menu->addChild(flipXPill.button);

    auto flipYPill = kit::pill(
        nullptr, nullptr, "Flip Y", "GJ_button_04.png", {smallW, 18.f},
        [this, id] {
            ReferenceSession::get().flipEntry(id, true);
            refreshPreview();
        },
        "bigFont.fnt"
    );
    flipYPill.button->setPosition({rightX + smallW + 4.f + smallW * 0.5f, smallY});
    menu->addChild(flipYPill.button);

    auto resetPill = kit::pill(
        nullptr, nullptr, tr("Reset", "Reset"), "GJ_button_04.png", {smallW, 18.f},
        [this, id] {
            ReferenceSession::get().resetTransform(id);
            refreshPreview();
            PaimonNotify::show(
                tr("Transform reset", "Transformacion restablecida"),
                NotificationIcon::Info,
                1.5f
            );
        },
        "bigFont.fnt"
    );
    resetPill.button->setPosition({rightX + (smallW + 4.f) * 2.f + smallW * 0.5f, smallY});
    menu->addChild(resetPill.button);

    // Opacity slider row.
    float const opY = kInspH - 92.f;
    auto* opTitle = kit::label(tr("Opacity", "Opacidad"), "bigFont.fnt", 0.34f, kit::tint::label);
    opTitle->setAnchorPoint({0.f, 0.5f});
    opTitle->setPosition({12.f, opY});
    m_inspectorContainer->addChild(opTitle, 4);

    int const pct = static_cast<int>(std::round((entry->opacity / 255.f) * 100.f));
    m_opacityValueLabel = kit::label(fmt::format("{}%", pct).c_str(), "goldFont.fnt", 0.44f, kit::tint::gold);
    m_opacityValueLabel->setAnchorPoint({1.f, 0.5f});
    m_opacityValueLabel->setPosition({kInspW - 12.f, opY});
    m_inspectorContainer->addChild(m_opacityValueLabel, 4);

    m_opacitySlider = Slider::create(
        this,
        menu_selector(ReferenceManagerPopup::onOpacitySliderChanged),
        0.72f
    );
    if (m_opacitySlider) {
        m_opacitySlider->setPosition({kInspW * 0.5f + 6.f, opY});
        m_opacitySlider->setValue(std::clamp(entry->opacity / 255.f, 0.f, 1.f));
        m_inspectorContainer->addChild(m_opacitySlider, 6);
    }

    // Z-Layer toggle pill.
    float const zY = kInspH - 121.f;
    bool const isFront = entry->zPlacement == ZPlacement::Front;
    m_layerPill = kit::pill(
        "paim_anim_layers.png",
        "GJ_infoIcon_001.png",
        isFront
            ? tr("Layer: In Front of Objects", "Capa: Delante de objetos")
            : tr("Layer: Behind Objects", "Capa: Detras de objetos"),
        isFront ? "GJ_button_02.png" : "GJ_button_05.png",
        {kInspW - 20.f, 22.f},
        [this, id] {
            auto const* cur = ReferenceSession::get().findEntry(id);
            if (!cur) return;
            auto const next = (cur->zPlacement == ZPlacement::Front)
                ? ZPlacement::Behind
                : ZPlacement::Front;
            ReferenceSession::get().setZPlacement(id, next);
            rebuildInspector();
        }
    );
    m_layerPill.button->setPosition({kInspW * 0.5f, zY});
    menu->addChild(m_layerPill.button);

    // 3 toggles row: Lock, Visible, Playtest.
    float const togY = kInspH - 149.f;
    float const togW = (kInspW - 28.f) / 3.f;

    m_lockPill = kit::pill(
        entry->locked ? "paim_anim_lock.png" : "paim_anim_unlock.png",
        "GJ_lock_001.png",
        entry->locked ? tr("Locked", "Bloqueado") : tr("Lock", "Bloquear"),
        entry->locked ? "GJ_button_03.png" : "GJ_button_04.png",
        {togW, 21.f},
        [this, id] {
            auto const* cur = ReferenceSession::get().findEntry(id);
            if (!cur) return;
            ReferenceSession::get().setLocked(id, !cur->locked);
            rebuildList();
            rebuildInspector();
        },
        "bigFont.fnt"
    );
    m_lockPill.button->setPosition({10.f + togW * 0.5f, togY});
    menu->addChild(m_lockPill.button);

    m_visiblePill = kit::pill(
        entry->visible ? "paim_anim_eye.png" : "paim_anim_eyeOff.png",
        "GJ_infoIcon_001.png",
        entry->visible ? tr("Visible", "Visible") : tr("Hidden", "Oculto"),
        entry->visible ? "GJ_button_01.png" : "GJ_button_04.png",
        {togW, 21.f},
        [this, id] {
            auto const* cur = ReferenceSession::get().findEntry(id);
            if (!cur) return;
            ReferenceSession::get().setVisible(id, !cur->visible);
            rebuildList();
            rebuildInspector();
        },
        "bigFont.fnt"
    );
    m_visiblePill.button->setPosition({14.f + togW + togW * 0.5f, togY});
    menu->addChild(m_visiblePill.button);

    m_playtestPill = kit::pill(
        nullptr,
        nullptr,
        entry->showInPlaytest ? tr("Test: ON", "Test: SI") : tr("Test: OFF", "Test: NO"),
        entry->showInPlaytest ? "GJ_button_01.png" : "GJ_button_04.png",
        {togW, 21.f},
        [this, id] {
            auto const* cur = ReferenceSession::get().findEntry(id);
            if (!cur) return;
            ReferenceSession::get().setShowInPlaytest(id, !cur->showInPlaytest);
            rebuildInspector();
        },
        "bigFont.fnt"
    );
    m_playtestPill.button->setPosition({18.f + togW * 2.f + togW * 0.5f, togY});
    menu->addChild(m_playtestPill.button);

    // Bottom actions: Duplicate + Delete.
    float const actY = 20.f;
    float const actW = (kInspW - 26.f) * 0.5f;

    auto dupPill = kit::pill(
        "paim_anim_frameDup.png",
        "GJ_duplicateBtn_001.png",
        tr("Duplicate", "Duplicar"),
        "GJ_button_05.png",
        {actW, 22.f},
        [this, id] {
            auto res = ReferenceSession::get().duplicateEntry(id);
            if (!res) {
                PaimonNotify::show(res.unwrapErr(), NotificationIcon::Error, 2.5f);
                return;
            }
            m_selectedId = res.unwrap();
            refreshAll();
        }
    );
    dupPill.button->setPosition({10.f + actW * 0.5f, actY});
    menu->addChild(dupPill.button);

    auto delPill = kit::pill(
        "paim_anim_frameDel.png",
        "GJ_trashBtn_001.png",
        tr("Delete", "Eliminar"),
        "GJ_button_06.png",
        {actW, 22.f},
        [this, id] {
            ReferenceSession::get().removeEntry(id);
            m_selectedId = 0;
            refreshAll();
        }
    );
    delPill.button->setPosition({16.f + actW + actW * 0.5f, actY});
    menu->addChild(delPill.button);
}

void ReferenceManagerPopup::refreshPreview() {
    auto const* entry = ReferenceSession::get().findEntry(m_selectedId);
    if (!entry) return;
    if (m_previewSprite) {
        m_previewSprite->setOpacity(static_cast<GLubyte>(std::clamp(entry->opacity, 0, 255)));
        m_previewSprite->setFlipX(entry->flipX);
        m_previewSprite->setFlipY(entry->flipY);
    }
    if (m_opacityValueLabel) {
        int const pct = static_cast<int>(std::round((entry->opacity / 255.f) * 100.f));
        m_opacityValueLabel->setString(fmt::format("{}%", pct).c_str());
    }
}

void ReferenceManagerPopup::onOpacitySliderChanged(CCObject*) {
    if (!m_opacitySlider || m_selectedId <= 0) return;
    float const val = std::clamp(m_opacitySlider->getValue(), 0.f, 1.f);
    int const alpha = static_cast<int>(std::round(val * 255.f));
    ReferenceSession::get().setOpacity(m_selectedId, alpha);
    refreshPreview();
}

} // namespace paimon::editorref
