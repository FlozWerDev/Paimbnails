#include "NewIconPopup.hpp"

#include "IconMakerKit.hpp"
#include "TemplatePickerPopup.hpp"
#include "../data/IconAnatomy.hpp"
#include "../data/IconPalettes.hpp"
#include "../engine/TemplateExtractor.hpp"
#include "../persist/IconPaths.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../services/IconApplier.hpp"
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

void seedFromStarterShape(std::string const& slotId) {
    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded) return;
    auto project = loaded.unwrap();

    auto const* def = anatomyFor(project.type);
    if (!def) return;

    int const part = def->partCount > 1 ? 1 : 0;

    // 1. Main body shape tuned to the gamemode
    IconPiece body;
    body.id = project.makePieceId();
    body.name = "Cuerpo base";
    body.shape.kind = PieceShape::Kind::Primitive;
    switch (project.type) {
        case IconType::Ball:
            body.shape.primitive = PrimitiveKind::Circle;
            break;
        case IconType::Wave:
            body.shape.primitive = PrimitiveKind::Triangle;
            body.transform.rotationDeg = 90.f;
            break;
        case IconType::Ufo:
            body.shape.primitive = PrimitiveKind::Capsule;
            body.transform.offsetY = -0.10f;
            break;
        case IconType::Ship:
            body.shape.primitive = PrimitiveKind::Shield;
            body.transform.rotationDeg = -90.f;
            break;
        case IconType::Swing:
            body.shape.primitive = PrimitiveKind::Octagon;
            break;
        default:
            body.shape.primitive = PrimitiveKind::RoundedSquare;
            body.shape.cornerRadius = 0.14f;
            break;
    }
    body.fill.type = FillType::Gradient;
    body.fill.gradient = gradientPresets().size() > 3 ? gradientPresets()[3].spec : GradientSpec{};
    body.fill.gloss = true;
    body.fill.glossStrength = 0.35f;
    body.fill.outline.enabled = true;
    body.fill.outline.width = 6.f;
    body.fill.outline.color = {12, 14, 24, 255};
    project.slots[slotStorageKey(part, "main")].pieces.push_back(std::move(body));

    // 2. Secondary inner frame accent
    IconPiece accent;
    accent.id = project.makePieceId();
    accent.name = "Marco interior";
    accent.shape.kind = PieceShape::Kind::Primitive;
    accent.shape.primitive = project.type == IconType::Ball
        ? PrimitiveKind::Circle
        : (project.type == IconType::Wave ? PrimitiveKind::Triangle : PrimitiveKind::RoundedSquare);
    accent.shape.hollow = true;
    accent.shape.strokeWidth = 0.14f;
    accent.transform.scale = 0.80f;
    if (project.type == IconType::Wave) accent.transform.rotationDeg = 90.f;
    accent.fill.type = FillType::Flat;
    accent.fill.flat = {225, 248, 255, 255};
    accent.fill.keepLuminance = false;
    project.slots[slotStorageKey(part, "secondary")].pieces.push_back(std::move(accent));

    // 3. Classic GD eyes in Blanco/Extra for non-Wave icons
    if (project.type != IconType::Wave) {
        IconPiece eyes;
        eyes.id = project.makePieceId();
        eyes.name = "Ojos GD";
        eyes.shape.kind = PieceShape::Kind::Decal;
        eyes.shape.decal = DecalKind::EyesClassic;
        eyes.transform.scale = 0.78f;
        eyes.fill.type = FillType::Flat;
        eyes.fill.flat = {255, 255, 255, 255};
        eyes.fill.keepLuminance = false;
        eyes.fill.outline.enabled = true;
        eyes.fill.outline.width = 4.f;
        eyes.fill.outline.color = {12, 14, 24, 255};
        project.slots[slotStorageKey(part, "extra")].pieces.push_back(std::move(eyes));
    }

    // 4. Glow outline
    IconPiece glow;
    glow.id = project.makePieceId();
    glow.name = "Brillo";
    glow.shape.kind = PieceShape::Kind::Primitive;
    glow.shape.primitive = project.type == IconType::Ball
        ? PrimitiveKind::Circle
        : (project.type == IconType::Wave ? PrimitiveKind::Triangle : PrimitiveKind::RoundedSquare);
    glow.shape.hollow = true;
    glow.shape.strokeWidth = 0.12f;
    glow.transform.scale = 1.08f;
    if (project.type == IconType::Wave) glow.transform.rotationDeg = 90.f;
    glow.fill.type = FillType::Flat;
    glow.fill.flat = {80, 245, 255, 255};
    glow.fill.keepLuminance = false;
    glow.fill.shadow.enabled = true;
    glow.fill.shadow.blur = 8.f;
    glow.fill.shadow.color = {80, 245, 255, 190};
    project.slots[slotStorageKey(part, "glow")].pieces.push_back(std::move(glow));

    project.modifiedAt = nowUnixMs();
    if (auto r = IconProjectStore::get().saveProject(project); !r) {
        log::warn("[icon-maker] seedFromStarterShape save: {}", r.unwrapErr());
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
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    m_onCreated = std::move(onCreated);
    setTitle("Nuevo icono");
    setID("icon-maker-new-popup"_spr);
    addInfoButton("Nuevo icono",
        "Elige el <cy>modo</c> (cubo, nave, bola...), ponle un <cy>nombre</c> y "
        "decide de donde partir:\n"
        "- <cg>Oficial</c>: copia un icono del juego para editarlo.\n"
        "- <cg>Forma base</c>: empieza con un diseno geometrico + ojos listo.\n"
        "- <cg>En blanco</c>: lienzo vacio para crear desde cero.");

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

    {
        IconApplier::VanillaScope vanillaGuard;
        for (std::size_t i = 0; i < types.size(); ++i) {
            IconType type = types[i];

            auto* wrap = CCNode::create();
            wrap->setContentSize({kCell - 6.f, kCell - 6.f});
            wrap->setAnchorPoint({0.5f, 0.5f});

            if (auto* player = SimplePlayer::create(1)) {
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

    constexpr float kNameScale = 0.8f;
    m_nameInput = TextInput::create(rightW / kNameScale, "Mi icono");
    if (m_nameInput) {
        m_nameInput->setScale(kNameScale);
        m_nameInput->setPosition({rightX + rightW / 2.f, size.height - 76.f});
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

    auto* tabs = kit::makeTabBar(rightW, {"Oficial", "Forma base", "En blanco"}, 0,
        [this](int index) { this->setStartMode(static_cast<StartMode>(index)); });
    if (tabs) {
        tabs->setPosition({rightX, size.height - 120.f - kit::kTabBarHeight});
        m_mainLayer->addChild(tabs);
    }

    m_startLabelPos = CCPoint{rightX, size.height - 150.f};
    m_startLabelW = rightW;

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

    {
        IconApplier::VanillaScope vanillaGuard;
        m_templatePreview = SimplePlayer::create(1);
        if (m_templatePreview) {
            m_templatePreview->setPosition({17.f, 17.f});
            m_templatePreview->setScale(0.72f);
            previewWrap->addChild(m_templatePreview);
        }
    }

    if (auto* btn = paimon::ui::makeButton("Elegir", [this] { this->pickTemplate(); },
            paimon::ui::Btn::Gray, 60.f, 0.5f)) {
        btn->setPosition({rightW - 12.f - 30.f, 20.f});
        rowMenu->addChild(btn);
    }

    auto* actionMenu = CCMenu::create();
    actionMenu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(actionMenu);

    if (auto* btn = paimon::ui::makeButton("Crear", [this] { this->onCreate(); },
            paimon::ui::Btn::Green, 110.f, 0.65f)) {
        btn->setPosition({leftCX, 28.f});
        actionMenu->addChild(btn);
    }

    selectType(IconType::Cube);
    setStartMode(StartMode::Official);
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
    m_templateIcon = 1;
    if (m_templatePreview) {
        IconApplier::VanillaScope vanillaGuard;
        m_templatePreview->updatePlayerFrame(m_templateIcon, type);
    }
}

void NewIconPopup::setStartMode(StartMode mode) {
    m_startMode = mode;
    refreshStartRow();
}

void NewIconPopup::refreshStartRow() {
    if (m_templateRow) m_templateRow->setVisible(m_startMode == StartMode::Official);
    if (m_startLabel) m_startLabel->removeFromParent();

    char const* desc = "Copiamos la forma de un icono oficial para que lo pintes y "
                       "le agregues accesorios a tu gusto.";
    if (m_startMode == StartMode::StarterShape) {
        desc = "Empieza con una forma geometrica brillante, marco interior y ojos GD "
               "listos para personalizar sin archivos externos.";
    } else if (m_startMode == StartMode::Blank) {
        desc = "Lienzo completamente vacio. Agrega formas, caras, accesorios, "
               "iconos oficiales o tus propias imagenes PNG.";
    }

    m_startLabel = paimon::ui::makeText(desc, m_startLabelW, 0.44f, kit::kDescColor);
    m_startLabel->setAnchorPoint({0.f, 1.f});
    m_startLabel->setPosition(m_startLabelPos);
    m_mainLayer->addChild(m_startLabel);
    if (m_templatePreview && m_startMode == StartMode::Official) {
        IconApplier::VanillaScope vanillaGuard;
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
    StartMode mode = m_startMode;
    int templateIcon = m_templateIcon;
    onClose(nullptr);

    Loader::get()->queueInMainThread([callback, slotId, mode, templateIcon] {
        if (paimon::isRuntimeShuttingDown()) return;
        if (mode == StartMode::Official) {
            seedFromTemplate(slotId, templateIcon);
        } else if (mode == StartMode::StarterShape) {
            seedFromStarterShape(slotId);
        }
        if (callback) callback(slotId);
    });
}

}  // namespace paimon::icon_maker
