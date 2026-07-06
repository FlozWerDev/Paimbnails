#include "NewProjectPopup.hpp"

#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../data/GdResourcesLocator.hpp"
#include "../engine/PackMetadataBuilder.hpp"
#include "../persist/SlotStore.hpp"
#include "../persist/TextureProject.hpp"

#include <Geode/Geode.hpp>

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
    paimon::markDynamicPopup(this);
    m_onCreated = std::move(cb);
    this->setTitle("New Texture Pack");

    if (auto* nameLbl = CCLabelBMFont::create("Name:", "bigFont.fnt")) {
        nameLbl->setScale(0.45f);
        nameLbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(nameLbl, Anchor::TopLeft, {20.f, -50.f});
    }

    if (auto* input = TextInput::create(150.f, "My Pack")) {
        input->setString("My Pack");
        input->setMaxCharCount(40);
        input->setScale(0.9f);
        m_mainLayer->addChildAtPosition(input, Anchor::TopLeft, {145.f, -50.f});
        m_nameInput = input;
    }

    if (auto* authorLbl = CCLabelBMFont::create("Author:", "bigFont.fnt")) {
        authorLbl->setScale(0.45f);
        authorLbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(authorLbl, Anchor::TopLeft, {230.f, -50.f});
    }

    if (auto* author = TextInput::create(130.f, "Paimbnails")) {
        author->setString("");
        author->setMaxCharCount(30);
        author->setScale(0.9f);
        m_mainLayer->addChildAtPosition(author, Anchor::TopLeft, {345.f, -50.f});
        m_authorInput = author;
    }

    if (auto* listLbl = CCLabelBMFont::create(
            "Sheets to include (auto-detected):", "bigFont.fnt")) {
        listLbl->setScale(0.45f);
        listLbl->setAnchorPoint({0.f, 0.5f});
        m_mainLayer->addChildAtPosition(listLbl, Anchor::TopLeft, {20.f, -90.f});
    }

    if (m_buttonMenu) {
        auto makeSmallBtn = [this](char const* label, float x, bool checked) {
            auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.34f);
            if (!spr) return;
            if (auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                    [this, checked](CCMenuItemSpriteExtra*) {
                        this->setAllChecked(checked);
                    })) {
                m_buttonMenu->addChildAtPosition(btn, Anchor::TopRight, {x, -90.f});
            }
        };
        makeSmallBtn("All",  -92.f, true);
        makeSmallBtn("None", -42.f, false);
    }

    auto* container = CCNode::create();
    if (container) {
        container->setContentSize({380.f, 130.f});
        m_mainLayer->addChildAtPosition(container, Anchor::Top, {0.f, -160.f});
        m_sheetsListContainer = container;
        refreshSheetsList();
    }

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

void NewProjectPopup::setAllChecked(bool checked) {
    for (auto& r : m_rows) r.checked = checked;
    refreshSheetsList();
}

void NewProjectPopup::refreshSheetsList() {
    if (!m_sheetsListContainer) return;
    m_sheetsListContainer->removeAllChildren();

    // Detect once; later calls just rebuild the UI from m_rows so the
    // All/None buttons can flip states without losing them.
    if (m_rows.empty()) {
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
        if (filtered.empty()) filtered = sheets;

        for (auto const& s : filtered) {
            SheetRow row;
            row.baseName      = s.baseName;
            row.qualitySuffix = s.qualitySuffix;
            row.plistPath     = geode::utils::string::pathToString(s.plistPath);
            row.pngPath       = geode::utils::string::pathToString(s.pngPath);
            row.checked       = true;
            m_rows.push_back(row);
        }
    }

    auto* scroll = ScrollLayer::create({380.f, 130.f});
    if (!scroll) return;
    scroll->setAnchorPoint({0.5f, 0.5f});
    auto* content = scroll->m_contentLayer;
    if (!content) return;

    constexpr float kRowH = 22.f;
    int idx = 0;
    for (auto const& row : m_rows) {
        auto* rowNode = CCNode::create();
        if (!rowNode) { ++idx; continue; }
        rowNode->setContentSize({380.f, kRowH});

        auto* checkMenu = CCMenu::create();
        if (!checkMenu) { ++idx; continue; }
        checkMenu->setContentSize({30.f, kRowH});

        // Lambda toggler variant: the old standard-sprites + null-selector
        // dance left an orphaned toggler that corrupted the menu child array
        // on cleanup (CCNode::cleanup recursion crash).
        int rowIndex = idx;
        auto* extToggler = CCMenuItemExt::createTogglerWithStandardSprites(
            0.5f, [this, rowIndex](CCMenuItemToggler* t) {
                if (!t) return;
                if (rowIndex >= 0 && rowIndex < static_cast<int>(m_rows.size())) {
                    m_rows[rowIndex].checked = !t->isToggled();
                }
            });
        if (extToggler) {
            extToggler->toggle(row.checked);
            checkMenu->addChildAtPosition(extToggler, Anchor::Left, {15.f, 0.f});
        }
        rowNode->addChild(checkMenu);

        if (auto* lbl = CCLabelBMFont::create(
                (row.baseName + " (" + row.qualitySuffix.substr(0, 3) + ")").c_str(),
                "bigFont.fnt")) {
            lbl->setScale(0.4f);
            lbl->setAnchorPoint({0.f, 0.5f});
            rowNode->addChildAtPosition(lbl, Anchor::Left, {40.f, 0.f});
        }

        rowNode->setAnchorPoint({0.5f, 0.5f});
        rowNode->setPosition({190.f, 130.f - (idx + 0.5f) * kRowH});
        content->addChild(rowNode);

        ++idx;
    }

    float totalH = std::max(130.f, idx * kRowH);
    content->setContentHeight(totalH);
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
    std::string name = m_nameInput ? std::string(m_nameInput->getString()) : std::string("My Pack");
    if (name.empty()) name = "My Pack";
    std::string author = m_authorInput ? std::string(m_authorInput->getString()) : std::string();
    if (author.empty()) author = "Paimbnails";

    TextureProject p;
    p.id        = PackMetadataBuilder::buildPackId(name);
    p.name      = name;
    p.author    = author;
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

    (void)ensureRepresentativeFrame(p);

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
