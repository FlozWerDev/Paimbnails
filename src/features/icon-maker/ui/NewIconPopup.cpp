#include "NewIconPopup.hpp"

#include "IconMakerKit.hpp"
#include "TemplatePickerPopup.hpp"
#include "../data/IconAnatomy.hpp"
#include "../engine/TemplateExtractor.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 420.f;
constexpr float kPopupH = 275.f;
constexpr char const* kNameFilter =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_";

// Prefills every zone of a fresh project with the pieces of a vanilla icon, so
// a beginner lands on something that already looks like an icon.
void seedFromTemplate(std::string const& slotId, int iconId) {
    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded) return;
    auto project = loaded.unwrap();

    auto const* def = anatomyFor(project.type);
    if (!def) return;

    auto extracted = TemplateExtractor::extract(project.type, iconId, def->canvasUhd);
    if (!extracted) {
        Notification::create(("Plantilla: " + extracted.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.f)->show();
        return;
    }

    for (auto& frame : extracted.unwrap()) {
        int part = 0;
        std::string slotKey;
        if (!slotForSuffix(project.type, frame.suffix, part, slotKey)) continue;

        IconPiece piece;
        piece.id = project.makePieceId();
        piece.name = "Base";
        piece.shape.kind = PieceShape::Kind::Template;
        piece.shape.templateIconId = iconId;
        piece.shape.templateFrameSuffix = frame.suffix;
        piece.shape.file = IconPaths::sanitizeFilename(
            fmt::format("tpl_{}_{}.png", piece.id, frame.suffix));

        if (auto r = frame.pixels.saveToPng(
                IconPaths::imageFile(slotId, piece.shape.file)); !r) {
            continue;
        }
        project.slots[slotStorageKey(part, slotKey)].pieces.push_back(std::move(piece));
    }

    project.modifiedAt = nowUnixMs();
    if (auto r = IconProjectStore::get().saveProject(project); !r) {
        log::warn("[icon-maker] seedFromTemplate save: {}", r.unwrapErr());
    }
}

}  // anonymous namespace

NewIconPopup* NewIconPopup::create(CreatedCallback onCreated) {
    auto* p = new NewIconPopup();
    if (p->init(std::move(onCreated))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool NewIconPopup::init(CreatedCallback onCreated) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    m_onCreated = std::move(onCreated);
    setTitle("Nuevo icono");
    setID("icon-maker-new-popup"_spr);

    auto size = m_mainLayer->getContentSize();

    float const leftW = 180.f;
    float const leftCX = 18.f + leftW / 2.f;
    float const rightX = 18.f + leftW + 16.f;
    float const rightW = size.width - rightX - 18.f;

    if (auto* heading = CCLabelBMFont::create("1. Para que modo", "goldFont.fnt")) {
        heading->setAnchorPoint({0.5f, 1.f});
        heading->setScale(0.48f);
        heading->setPosition({leftCX, size.height - 46.f});
        m_mainLayer->addChild(heading);
    }

    auto* typeMenu = CCMenu::create();
    typeMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(typeMenu);

    auto const& types = supportedTypes();
    constexpr int kCols = 3;
    constexpr float kCell = 46.f;
    float const gridTop = size.height - 66.f;
    float const gridLeft = leftCX - kCell * (kCols - 1) / 2.f;

    m_typeSelector = paimon::SpriteHelper::createDarkPanel(
        kCell - 6.f, kCell - 6.f, 160, 6.f);
    if (m_typeSelector) {
        m_typeSelector->setAnchorPoint({0.5f, 0.5f});
        m_typeSelector->setVisible(false);
        typeMenu->addChild(m_typeSelector, -1);
    }

    for (std::size_t i = 0; i < types.size(); ++i) {
        IconType type = types[i];

        // SimplePlayer has no content size of its own; it always rides inside a
        // sized wrapper so the button gets a real touch box.
        auto* wrap = CCNode::create();
        wrap->setContentSize({kCell - 6.f, kCell - 6.f});
        wrap->setAnchorPoint({0.5f, 0.5f});

        auto* player = SimplePlayer::create(1);
        if (player) {
            player->updatePlayerFrame(1, type);
            player->setPosition({(kCell - 6.f) / 2.f, (kCell - 6.f) / 2.f});
            player->setScale(0.72f);
            wrap->addChild(player, 1);
        }

        auto* btn = CCMenuItemExt::createSpriteExtra(wrap,
            [this, type](CCMenuItemSpriteExtra*) { this->selectType(type); });
        if (!btn) continue;
        btn->setPosition({
            gridLeft + kCell * static_cast<float>(i % kCols),
            gridTop - kCell * static_cast<float>(i / kCols) - kCell / 2.f,
        });
        typeMenu->addChild(btn);

        m_typeButtons.push_back({type, btn->getPosition()});
    }

    m_typeLabel = CCLabelBMFont::create("Cubo", "goldFont.fnt");
    if (m_typeLabel) {
        m_typeLabel->setScale(0.5f);
        m_typeLabel->setPosition({leftCX, gridTop - kCell * 3.f - 8.f});
        m_mainLayer->addChild(m_typeLabel);
    }

    if (auto* heading = CCLabelBMFont::create("2. Como se llama", "goldFont.fnt")) {
        heading->setAnchorPoint({0.f, 1.f});
        heading->setScale(0.48f);
        heading->setPosition({rightX, size.height - 46.f});
        m_mainLayer->addChild(heading);
    }

    m_nameInput = TextInput::create(rightW, "Mi icono");
    if (m_nameInput) {
        m_nameInput->setPosition({rightX + rightW / 2.f, size.height - 78.f});
        m_nameInput->setMaxCharCount(24);
        m_nameInput->setFilter(kNameFilter);
        m_mainLayer->addChild(m_nameInput);
    }

    if (auto* heading = CCLabelBMFont::create("3. De donde partimos", "goldFont.fnt")) {
        heading->setAnchorPoint({0.f, 1.f});
        heading->setScale(0.48f);
        heading->setPosition({rightX, size.height - 100.f});
        m_mainLayer->addChild(heading);
    }

    auto* tabs = kit::makeTabBar(rightW, {"Icono oficial", "En blanco"}, 0,
        [this](int index) { this->setStartFromTemplate(index == 0); });
    if (tabs) {
        tabs->setPosition({rightX, size.height - 122.f - kit::kTabBarHeight});
        m_mainLayer->addChild(tabs);
    }

    m_startLabel = CCLabelBMFont::create("", "chatFont.fnt", rightW / 0.44f,
                                         kCCTextAlignmentLeft);
    if (m_startLabel) {
        m_startLabel->setScale(0.44f);
        m_startLabel->setAnchorPoint({0.f, 1.f});
        m_startLabel->setColor(kit::kDescColor);
        m_startLabel->setPosition({rightX, size.height - 154.f});
        m_mainLayer->addChild(m_startLabel);
    }

    // Row that shows which official icon is being copied.
    m_templateRow = CCNode::create();
    m_templateRow->setPosition({rightX, 46.f});
    m_mainLayer->addChild(m_templateRow);

    auto* rowMenu = CCMenu::create();
    rowMenu->setPosition({0.f, 0.f});
    m_templateRow->addChild(rowMenu, 5);

    if (auto* panel = kit::makePlate(rightW, 40.f)) {
        panel->setPosition({0.f, 0.f});
        m_templateRow->addChild(panel, -1);
    }

    auto* previewWrap = CCNode::create();
    previewWrap->setContentSize({34.f, 34.f});
    previewWrap->setPosition({6.f, 3.f});
    m_templateRow->addChild(previewWrap);

    m_templatePreview = SimplePlayer::create(1);
    if (m_templatePreview) {
        m_templatePreview->setPosition({17.f, 17.f});
        m_templatePreview->setScale(0.72f);
        previewWrap->addChild(m_templatePreview);
    }

    if (auto* spr = ButtonSprite::create("Elegir", "goldFont.fnt", "GJ_button_04.png", 0.7f)) {
        spr->setScale(0.5f);
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this](CCMenuItemSpriteExtra*) { this->pickTemplate(); });
        btn->setPosition({rightW - 12.f - btn->getScaledContentSize().width / 2.f, 20.f});
        rowMenu->addChild(btn);
    }

    auto* actionMenu = CCMenu::create();
    actionMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(actionMenu);

    if (auto* createSpr = ButtonSprite::create("Crear", "bigFont.fnt", "GJ_button_01.png", 0.7f)) {
        auto* btn = CCMenuItemExt::createSpriteExtra(createSpr,
            [this](CCMenuItemSpriteExtra*) { this->onCreate(); });
        btn->setPosition({leftCX, 30.f});
        actionMenu->addChild(btn);
    }

    selectType(IconType::Cube);
    setStartFromTemplate(true);
    return true;
}

void NewIconPopup::selectType(IconType type) {
    bool const changed = m_selectedType != type;
    m_selectedType = type;
    if (m_typeSelector) {
        for (auto const& tb : m_typeButtons) {
            if (tb.type != type) continue;

            if (!m_typeSelector->isVisible()) {
                m_typeSelector->setPosition(tb.position);
                m_typeSelector->setVisible(true);
            } else if (changed) {
                m_typeSelector->stopAllActions();
                m_typeSelector->runAction(CCEaseInOut::create(
                    CCMoveTo::create(0.18f, tb.position), 2.f));
            }
            break;
        }
    }
    if (m_typeLabel) {
        if (auto const* def = anatomyFor(type)) {
            m_typeLabel->setString(std::string(def->displayName).c_str());
        }
    }
    // The template belongs to a gamemode, so the preview follows the choice.
    m_templateIcon = 1;
    if (m_templatePreview) m_templatePreview->updatePlayerFrame(m_templateIcon, type);
}

void NewIconPopup::setStartFromTemplate(bool fromTemplate) {
    m_fromTemplate = fromTemplate;
    refreshStartRow();
}

void NewIconPopup::refreshStartRow() {
    if (m_templateRow) m_templateRow->setVisible(m_fromTemplate);
    if (m_startLabel) {
        m_startLabel->setString(m_fromTemplate
            ? "Copiamos la forma de un icono del juego y tu la pintas. Es la "
              "forma mas facil de empezar."
            : "Empiezas con el lienzo vacio y vas agregando tus propias "
              "imagenes. Para cuando ya sabes lo que quieres.");
    }
    if (m_templatePreview) {
        m_templatePreview->updatePlayerFrame(m_templateIcon, m_selectedType);
    }
}

void NewIconPopup::pickTemplate() {
    Ref<NewIconPopup> self = this;
    auto* picker = TemplatePickerPopup::create(m_selectedType, [self](int iconId) {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        self->m_templateIcon = iconId;
        self->refreshStartRow();
    });
    if (picker) picker->show();
}

void NewIconPopup::onCreate() {
    std::string name = m_nameInput ? m_nameInput->getString() : "";
    while (!name.empty() && name.front() == ' ') name.erase(name.begin());
    while (!name.empty() && name.back() == ' ') name.pop_back();
    if (name.empty()) {
        Notification::create("Ponle un nombre al icono.",
            NotificationIcon::Warning, 2.f)->show();
        return;
    }

    IconProject seed;
    seed.name = name;
    seed.type = m_selectedType;

    auto created = IconProjectStore::get().createProject(std::move(seed));
    if (!created) {
        Notification::create(("No se pudo crear: " + created.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.f)->show();
        return;
    }

    auto slotId = created.unwrap();
    auto callback = m_onCreated;
    bool fromTemplate = m_fromTemplate;
    int templateIcon = m_templateIcon;
    onClose(nullptr);

    Loader::get()->queueInMainThread([callback, slotId, fromTemplate, templateIcon] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (fromTemplate) seedFromTemplate(slotId, templateIcon);
        if (callback) callback(slotId);
    });
}

}  // namespace paimon::icon_maker
