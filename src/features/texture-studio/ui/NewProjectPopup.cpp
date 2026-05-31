#include "NewProjectPopup.hpp"

#include "../data/GdResourcesLocator.hpp"
#include "../engine/PackMetadataBuilder.hpp"
#include "../persist/SlotStore.hpp"
#include "../persist/TextureProject.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace paimon::texture_studio {

NewProjectPopup* NewProjectPopup::create(SlotCreatedCallback cb) {
    auto* ret = new NewProjectPopup();
    if (ret->init(std::move(cb))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool NewProjectPopup::init(SlotCreatedCallback cb) {
    if (!Popup::init(420.f, 280.f)) return false;
    m_onCreated = std::move(cb);
    this->setTitle("New Texture Pack");

    // ── Pack name ──────────────────────────────────────────────────────
    if (auto* nameLbl = CCLabelBMFont::create("Pack Name:", "bigFont.fnt")) {
        nameLbl->setScale(0.5f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(nameLbl, Anchor::TopLeft, {20.f, -50.f});
    }

    if (auto* input = TextInput::create(280.f, "My Pack")) {
        input->setString("My Pack");
        input->setMaxCharCount(40);
        m_mainLayer->addChildAtPosition(input, Anchor::TopLeft, {120.f, -55.f});
        m_nameInput = input;
    }

    // ── Sheets list label ──────────────────────────────────────────────
    if (auto* listLbl = CCLabelBMFont::create(
            "Sheets to include (auto-detected):", "bigFont.fnt")) {
        listLbl->setScale(0.45f);
        listLbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(listLbl, Anchor::TopLeft, {20.f, -90.f});
    }

    // ── Sheets list (scroll) ───────────────────────────────────────────
    auto* container = CCNode::create();
    if (container) {
        container->setContentSize({380.f, 130.f});
        m_mainLayer->addChildAtPosition(container, Anchor::Top, {0.f, -160.f});
        m_sheetsListContainer = container;
        refreshSheetsList();
    }

    // ── Footer buttons ─────────────────────────────────────────────────
    if (m_buttonMenu) {
        if (auto* createBtnSpr = ButtonSprite::create("Create", "goldFont.fnt", "GJ_button_01.png", 0.8f)) {
            if (auto* createBtn = CCMenuItemExt::createSpriteExtra(createBtnSpr,
                    [this](CCMenuItemSpriteExtra*) { this->onCreateClicked(nullptr); })) {
                m_buttonMenu->addChildAtPosition(createBtn, Anchor::BottomRight, {-50.f, 25.f});
            }
        }
    }

    return true;
}

void NewProjectPopup::refreshSheetsList() {
    if (!m_sheetsListContainer) return;
    m_sheetsListContainer->removeAllChildren();
    m_rows.clear();

    auto detected = GdResourcesLocator::detectVanillaSheets();
    if (!detected || detected.unwrap().empty()) {
        if (auto* empty = CCLabelBMFont::create(
                "No sheets detected in GD Resources.", "bigFont.fnt")) {
            empty->setScale(0.4f);
            empty->setColor({200, 80, 80});
            m_sheetsListContainer->addChildAtPosition(empty, Anchor::Center);
        }
        return;
    }

    auto sheets = detected.unwrap();
    // Restrict to the most relevant sheets for menu recoloring (matches
    // PackGen's typical selection). Users can expand later via the
    // ProjectEditorLayer.
    static const std::vector<std::string> kRelevantPrefixes = {
        "GJ_GameSheet", "GJ_LaunchSheet", "GJ_LaunchSheet2",
        "FireSheet", "GauntletSheet", "PixelSheet",
    };
    std::vector<DetectedSheet> filtered;
    for (auto const& s : sheets) {
        for (auto const& p : kRelevantPrefixes) {
            if (s.baseName.rfind(p, 0) == 0) {
                filtered.push_back(s);
                break;
            }
        }
    }
    if (filtered.empty()) filtered = sheets;  // fallback: show everything

    auto* scroll = ScrollLayer::create({380.f, 130.f});
    if (!scroll) return;
    scroll->setAnchorPoint({0.5f, 0.5f});
    auto* content = scroll->m_contentLayer;
    if (!content) return;

    constexpr float kRowH = 22.f;
    int idx = 0;
    for (auto const& s : filtered) {
        SheetRow row;
        row.baseName      = s.baseName;
        row.qualitySuffix = s.qualitySuffix;
        row.plistPath     = s.plistPath.string();
        row.pngPath       = s.pngPath.string();
        row.checked       = true;
        m_rows.push_back(row);

        auto* rowNode = CCNode::create();
        if (!rowNode) { ++idx; continue; }
        rowNode->setContentSize({380.f, kRowH});

        auto* checkMenu = CCMenu::create();
        if (!checkMenu) { ++idx; continue; }
        checkMenu->setContentSize({30.f, kRowH});

        // Use the Geode lambda variant directly. The previous code created a
        // standard-sprites toggler with a null selector, attached it to the
        // menu, then immediately removed it and re-added an ext toggler.
        // That dance leaves an orphaned toggler with target=this and
        // selector=nullptr in the autorelease pool, plus has retain/release
        // sequencing that has been observed to corrupt the menu's child
        // array on cleanup (CCNode::cleanup recursion crash).
        int rowIndex = idx;
        auto* extToggler = CCMenuItemExt::createTogglerWithStandardSprites(
            0.5f, [this, rowIndex](CCMenuItemToggler* t) {
                if (!t) return;
                if (rowIndex >= 0 && rowIndex < static_cast<int>(m_rows.size())) {
                    m_rows[rowIndex].checked = !t->isToggled();
                }
            });
        if (extToggler) {
            extToggler->toggle(true);
            checkMenu->addChildAtPosition(extToggler, Anchor::Left, {15.f, 0.f});
        }
        rowNode->addChild(checkMenu);

        if (auto* lbl = CCLabelBMFont::create(
                (s.baseName + " (" + s.qualitySuffix.substr(0, 3) + ")").c_str(),
                "bigFont.fnt")) {
            lbl->setScale(0.4f);
            lbl->setAnchorPoint({0.f, 0.5f});
            rowNode->addChildAtPosition(lbl, Anchor::Left, {40.f, 0.f});
        }

        // Position in scroll content layer.
        rowNode->setAnchorPoint({0.5f, 0.5f});
        rowNode->setPosition({190.f, 130.f - (idx + 0.5f) * kRowH});
        content->addChild(rowNode);

        ++idx;
    }

    float totalH = std::max(130.f, idx * kRowH);
    content->setContentHeight(totalH);
    // Position children top-down within the content layer.
    if (auto* children = content->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            auto* c = static_cast<CCNode*>(children->objectAtIndex(i));
            if (!c) continue;
            c->setPositionY(totalH - (i + 0.5f) * kRowH);
        }
    }
    scroll->scrollToTop();
    scroll->setAnchorPoint({0.5f, 0.5f});
    scroll->setPosition({m_sheetsListContainer->getContentSize().width / 2.f,
                         m_sheetsListContainer->getContentSize().height / 2.f});
    m_sheetsListContainer->addChild(scroll);
}

void NewProjectPopup::onCreateClicked(CCObject*) {
    std::string name = m_nameInput ? m_nameInput->getString() : std::string("My Pack");
    if (name.empty()) name = "My Pack";

    TextureProject p;
    p.id        = PackMetadataBuilder::buildPackId(name);
    p.name      = name;
    p.author    = "Paimbnails";
    p.createdAt = nowUnixMs();
    p.modifiedAt = p.createdAt;

    int included = 0;
    for (auto const& row : m_rows) {
        if (!row.checked) continue;
        ProjectSheetRef ref;
        ref.baseName        = row.baseName;
        ref.qualitySuffix   = row.qualitySuffix;
        ref.sourcePlistPath = row.plistPath;
        ref.sourcePngPath   = row.pngPath;
        p.sheets.push_back(std::move(ref));
        ++included;
    }
    if (included == 0) {
        Notification::create("Select at least one sheet.", NotificationIcon::Warning, 2.0f)->show();
        return;
    }

    auto created = SlotStore::get().createSlot(p);
    if (!created) {
        Notification::create(
            ("Create failed: " + created.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }
    auto id = created.unwrap();
    log::info("[texture-studio] created slot '{}' ({}) with {} sheet(s)",
        name, id, included);

    if (m_onCreated) m_onCreated(id);
    this->onClose(nullptr);
}

}  // namespace paimon::texture_studio
