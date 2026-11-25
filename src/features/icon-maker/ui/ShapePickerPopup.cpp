#include "ShapePickerPopup.hpp"

#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "../engine/ShapeGenerator.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>
#include <vector>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace kit = paimon::icon_maker::gdkit;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 410.f;
constexpr float kPopupH = 286.f;
constexpr int kCols = 5;
constexpr float kCellW = 70.f;
constexpr float kCellH = 62.f;
constexpr float kCellGap = 5.f;

}  // anonymous namespace

ShapePickerPopup* ShapePickerPopup::create(PickedCallback onPicked, bool startOnDecals) {
    auto* p = new ShapePickerPopup();
    if (p->init(std::move(onPicked), startOnDecals)) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool ShapePickerPopup::init(PickedCallback onPicked, bool startOnDecals) {
    if (!PaimonPopup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);
    m_onPicked = std::move(onPicked);
    m_showDecals = startOnDecals;

    setTitle("Agregar forma o accesorio");
    setID("icon-maker-shape-picker"_spr);
    addInfoButton("Formas y accesorios",
        "Elige cualquier <cy>Forma geometrica</c> (solida o en marco hueco) o "
        "<cg>Accesorios y caras</c> listos para usar: ojos, bocas, coronas, "
        "cuernos, alas, rayos, calaveras y mas. Se renderizan en alta definicion "
        "y puedes pintarlos con colores, degradados o patrones.");

    m_tabsHost = CCNode::create();
    m_mainLayer->addChild(m_tabsHost, 4);

    m_modeHost = CCNode::create();
    m_mainLayer->addChild(m_modeHost, 4);

    m_gridHost = CCNode::create();
    m_mainLayer->addChild(m_gridHost, 3);

    rebuildGrid();
    return true;
}

void ShapePickerPopup::rebuildGrid() {
    if (!m_tabsHost || !m_gridHost || !m_modeHost) return;
    m_tabsHost->removeAllChildren();
    m_modeHost->removeAllChildren();
    m_gridHost->removeAllChildren();
    m_scroll = nullptr;

    auto const size = m_mainLayer->getContentSize();
    float const innerW = size.width - 28.f;
    float const topY = size.height - 40.f;

    auto* tabs = kit::makeTabBar(innerW, {"Formas (12)", "Caras y Accesorios (18)"},
        m_showDecals ? 1 : 0,
        [this](int index) {
            Ref<ShapePickerPopup> self = this;
            Loader::get()->queueInMainThread([self, index] {
                if (paimon::isRuntimeShuttingDown() || !self) return;
                self->m_showDecals = (index == 1);
                self->rebuildGrid();
            });
        });
    if (tabs) {
        tabs->setPosition({14.f, topY - kit::kTabBarHeight});
        m_tabsHost->addChild(tabs);
    }

    float gridTop = topY - kit::kTabBarHeight - 6.f;

    if (!m_showDecals) {
        // Solid / Hollow frame quick toggle for primitives!
        auto* modeTabs = kit::makeTabBar(180.f, {"Solida", "Marco hueco"},
            m_hollowPrimitives ? 1 : 0,
            [this](int index) {
                Ref<ShapePickerPopup> self = this;
                Loader::get()->queueInMainThread([self, index] {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    self->m_hollowPrimitives = (index == 1);
                    self->rebuildGrid();
                });
            });
        if (modeTabs) {
            modeTabs->setPosition({size.width / 2.f - 90.f, gridTop - kit::kTabBarHeight});
            m_modeHost->addChild(modeTabs);
        }
        gridTop -= kit::kTabBarHeight + 6.f;
    }

    float const scrollH = std::max(60.f, gridTop - 12.f);
    m_scroll = ScrollLayer::create({innerW, scrollH});
    if (!m_scroll) return;
    m_scroll->setPosition({14.f, 12.f});
    m_gridHost->addChild(m_scroll);

    struct Entry {
        std::string name;
        PieceShape shape;
        ts::ImageBuffer preview;
    };

    std::vector<Entry> items;
    if (!m_showDecals) {
        for (auto const& info : ShapeGenerator::primitives()) {
            PieceShape s;
            s.kind = PieceShape::Kind::Primitive;
            s.primitive = info.kind;
            s.hollow = m_hollowPrimitives;
            items.push_back({
                std::string(info.name) + (m_hollowPrimitives ? " (marco)" : ""),
                s,
                ShapeGenerator::rasterize(s, 72)
            });
        }
    } else {
        for (auto const& info : ShapeGenerator::decals()) {
            PieceShape s;
            s.kind = PieceShape::Kind::Decal;
            s.decal = info.kind;
            items.push_back({
                std::string(info.name),
                s,
                ShapeGenerator::rasterize(s, 72)
            });
        }
    }

    int const total = static_cast<int>(items.size());
    int const rows = std::max(1, (total + kCols - 1) / kCols);
    float const gridW = static_cast<float>(kCols) * kCellW + static_cast<float>(kCols - 1) * kCellGap;
    float const originX = (innerW - gridW) * 0.5f;
    float const contentH = std::max(scrollH,
        static_cast<float>(rows) * kCellH + static_cast<float>(rows - 1) * kCellGap + 8.f);

    auto* content = m_scroll->m_contentLayer;
    content->setContentSize({innerW, contentH});

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(
        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
    content->addChild(menu, 5);

    for (int i = 0; i < total; ++i) {
        auto const& item = items[static_cast<std::size_t>(i)];
        int const col = i % kCols;
        int const row = i / kCols;

        auto* holder = CCNode::create();
        holder->setAnchorPoint({0.5f, 0.5f});
        holder->setContentSize({kCellW, kCellH});

        if (auto* plate = paimon::SpriteHelper::createColorPanel(
                kCellW, kCellH, {14, 24, 52}, 205, 5.f)) {
            plate->setAnchorPoint({0.f, 0.f});
            holder->addChild(plate, -1);
        }

        if (auto* tex = ts::SpritePreviewRenderer::createTexture(item.preview)) {
            if (auto* spr = CCSprite::createWithTexture(tex)) {
                spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
                float const longest = std::max(spr->getContentSize().width,
                                               spr->getContentSize().height);
                if (longest > 0.f) spr->setScale(34.f / longest);
                spr->setColor(m_showDecals ? ccColor3B{120, 235, 255} : ccColor3B{235, 242, 255});
                spr->setPosition({kCellW * 0.5f, kCellH - 22.f});
                holder->addChild(spr, 1);
            }
        }

        auto* lbl = CCLabelBMFont::create(item.name.c_str(), "chatFont.fnt");
        lbl->setAnchorPoint({0.5f, 0.5f});
        lbl->setColor({230, 238, 255});
        lbl->limitLabelWidth(kCellW - 6.f, 0.42f, 0.22f);
        lbl->setPosition({kCellW * 0.5f, 9.f});
        holder->addChild(lbl, 2);

        PieceShape chosen = item.shape;
        std::string chosenName = item.name;
        auto* btn = CCMenuItemExt::createSpriteExtra(holder,
            [this, chosen, chosenName](CCMenuItemSpriteExtra*) {
                auto cb = m_onPicked;
                onClose(nullptr);
                if (cb) cb(chosen, chosenName);
            });
        btn->setPosition({
            originX + kCellW * 0.5f + static_cast<float>(col) * (kCellW + kCellGap),
            contentH - 4.f - kCellH * 0.5f - static_cast<float>(row) * (kCellH + kCellGap),
        });
        menu->addChild(btn);
    }

    m_scroll->moveToTop();
}

}  // namespace paimon::icon_maker
