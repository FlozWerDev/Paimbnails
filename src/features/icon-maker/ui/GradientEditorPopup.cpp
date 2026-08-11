#include "GradientEditorPopup.hpp"

#include "IconActionSheet.hpp"
#include "IconMakerKit.hpp"
#include "IconMakerUI.hpp"
#include "../data/IconPalettes.hpp"
#include "../engine/GradientRasterizer.hpp"
#include "../../texture-studio/engine/SpritePreviewRenderer.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/ColorPickPopup.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <algorithm>

using namespace geode::prelude;
namespace ts = paimon::texture_studio;
namespace kit = paimon::icon_maker::gdkit;
namespace mkui = paimon::icon_maker::ui;

namespace paimon::icon_maker {

namespace {

constexpr float kPopupW = 400.f;
constexpr float kPopupH = 285.f;
constexpr float kPreviewSide = 74.f;
constexpr std::size_t kMaxStops = 8;

}  // anonymous namespace

GradientEditorPopup* GradientEditorPopup::create(GradientSpec initial,
                                                 ChangedCallback onChanged) {
    auto* p = new GradientEditorPopup();
    if (p->init(std::move(initial), std::move(onChanged))) {
        p->autorelease();
        return p;
    }
    delete p;
    return nullptr;
}

bool GradientEditorPopup::init(GradientSpec initial, ChangedCallback onChanged) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    paimon::markDynamicPopup(this);

    m_spec = std::move(initial);
    if (m_spec.stops.empty()) m_spec = GradientSpec{};
    m_onChanged = std::move(onChanged);

    setTitle("Degradado");
    setID("icon-maker-gradient-popup"_spr);

    auto size = m_mainLayer->getContentSize();

    // Square preview: a strip cannot show what "radial" means.
    if (auto* frame = paimon::SpriteHelper::createColorPanel(
            kPreviewSide + 6.f, kPreviewSide + 6.f, {0, 0, 0}, 140, 6.f)) {
        frame->setAnchorPoint({0.f, 0.f});
        frame->setPosition({16.f, size.height - 52.f - kPreviewSide});
        m_mainLayer->addChild(frame);
    }
    m_preview = CCSprite::create();
    if (m_preview) {
        m_preview->setAnchorPoint({0.f, 0.f});
        m_preview->setPosition({19.f, size.height - 49.f - kPreviewSide});
        m_mainLayer->addChild(m_preview, 1);
    }

    m_bodyHost = CCNode::create();
    m_bodyHost->setPosition({0.f, 0.f});
    m_mainLayer->addChild(m_bodyHost);

    rebuildBody();
    refreshPreview();
    return true;
}

void GradientEditorPopup::scheduleBodyRebuild() {
    if (m_rebuildQueued) return;
    m_rebuildQueued = true;
    Ref<GradientEditorPopup> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        self->m_rebuildQueued = false;
        if (self->getParent()) self->rebuildBody();
    });
}

void GradientEditorPopup::rebuildBody() {
    if (!m_bodyHost) return;
    m_bodyHost->removeAllChildren();
    m_body = nullptr;

    auto size = m_mainLayer->getContentSize();
    float const left = 16.f + kPreviewSide + 16.f;
    float const bodyW = size.width - left - 16.f;

    // Kind picker stays pinned next to the preview: it is the one choice that
    // changes what every control below means.
    auto* tabs = kit::makeTabBar(bodyW, {"Lineal", "Radial"},
        m_spec.kind == GradientKind::Radial ? 1 : 0,
        [this](int index) {
            m_spec.kind = index == 1 ? GradientKind::Radial : GradientKind::Linear;
            refreshPreview();
            notifyChanged();
            scheduleBodyRebuild();
        });
    if (tabs) {
        tabs->setPosition({left, size.height - 52.f - kit::kTabBarHeight});
        m_bodyHost->addChild(tabs);
    }

    auto* presetSpr = ButtonSprite::create("Listos", "goldFont.fnt", "GJ_button_04.png", 0.7f);
    if (presetSpr) {
        presetSpr->setScale(0.46f);
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(
            CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        m_bodyHost->addChild(menu, 5);

        Ref<GradientEditorPopup> self = this;
        auto* btn = CCMenuItemExt::createSpriteExtra(presetSpr,
            [self](CCMenuItemSpriteExtra*) {
                if (!self) return;
                std::vector<IconActionSheet::Action> actions;
                for (auto const& preset : gradientPresets()) {
                    auto spec = preset.spec;
                    actions.push_back({std::string(preset.name), "", [self, spec] {
                        if (paimon::isRuntimeShuttingDown() || !self) return;
                        self->m_spec = spec;
                        self->refreshPreview();
                        self->notifyChanged();
                        self->rebuildBody();
                    }, false});
                }
                if (auto* sheet = IconActionSheet::create(
                        "Degradados listos", std::move(actions))) {
                    sheet->show();
                }
            });
        btn->setPosition({16.f + (kPreviewSide + 6.f) / 2.f,
                          size.height - 62.f - kPreviewSide});
        menu->addChild(btn);
    }

    // Everything else scrolls: parameters first, then one row per color.
    std::vector<CCNode*> rows;

    if (m_spec.kind == GradientKind::Linear) {
        rows.push_back(kit::makeSliderRow(bodyW, "Direccion",
            "Hacia donde va la mezcla.",
            m_spec.angleDeg, 0.0, 360.0,
            [](double v) { return fmt::format("{}deg", static_cast<int>(v)); },
            [this](double v) {
                m_spec.angleDeg = static_cast<float>(v);
                refreshPreview();
                notifyChanged();
            }));
    } else {
        rows.push_back(kit::makeSliderRow(bodyW, "Centro horizontal", nullptr,
            m_spec.centerX, 0.0, 1.0,
            [](double v) { return fmt::format("{:.2f}", v); },
            [this](double v) {
                m_spec.centerX = static_cast<float>(v);
                refreshPreview();
                notifyChanged();
            }));
        rows.push_back(kit::makeSliderRow(bodyW, "Centro vertical", nullptr,
            m_spec.centerY, 0.0, 1.0,
            [](double v) { return fmt::format("{:.2f}", v); },
            [this](double v) {
                m_spec.centerY = static_cast<float>(v);
                refreshPreview();
                notifyChanged();
            }));
        rows.push_back(kit::makeSliderRow(bodyW, "Tamano del circulo", nullptr,
            m_spec.radius, 0.05, 2.0,
            [](double v) { return fmt::format("{:.2f}", v); },
            [this](double v) {
                m_spec.radius = static_cast<float>(v);
                refreshPreview();
                notifyChanged();
            }));
    }

    for (std::size_t i = 0; i < m_spec.stops.size(); ++i) {
        auto const& stop = m_spec.stops[i];
        Ref<GradientEditorPopup> self = this;

        auto* posRow = kit::makeSliderRow(bodyW - 74.f, "Posicion", nullptr,
            stop.pos, 0.0, 1.0,
            [](double v) { return fmt::format("{}%", static_cast<int>(v * 100.0)); },
            [self, i](double v) {
                if (!self || i >= self->m_spec.stops.size()) return;
                self->m_spec.stops[i].pos = static_cast<float>(v);
                self->refreshPreview();
                self->notifyChanged();
            });

        // La fila crece con el control que lleva dentro.
        float const rowH = std::max(
            42.f, (posRow ? posRow->getContentSize().height : 0.f) + 6.f);
        auto* row = CCNode::create();
        row->setAnchorPoint({0.f, 0.f});
        row->setContentSize({bodyW, rowH});

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(
            CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        row->addChild(menu, 5);

        auto* swatch = mkui::makeSwatch(24.f, {stop.color.r, stop.color.g, stop.color.b}, false);
        auto* swatchBtn = CCMenuItemExt::createSpriteExtra(swatch,
            [self, i](CCMenuItemSpriteExtra*) {
                if (!self || i >= self->m_spec.stops.size()) return;
                auto* picker = ColorPickPopup::create(self->m_spec.stops[i].color);
                if (!picker) return;
                picker->setCallback([self, i](ccColor4B const& picked) {
                    if (paimon::isRuntimeShuttingDown() || !self) return;
                    if (i >= self->m_spec.stops.size()) return;
                    self->m_spec.stops[i].color = picked;
                    self->refreshPreview();
                    self->notifyChanged();
                    self->scheduleBodyRebuild();
                });
                picker->show();
            });
        swatchBtn->setPosition({20.f, rowH / 2.f});
        menu->addChild(swatchBtn);

        if (posRow) {
            posRow->setPosition({36.f, (rowH - posRow->getContentSize().height) / 2.f});
            row->addChild(posRow);
        }

        if (m_spec.stops.size() > 2) {
            if (auto* del = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png")) {
                del->setScale(0.52f);
                auto* btn = CCMenuItemExt::createSpriteExtra(del,
                    [self, i](CCMenuItemSpriteExtra*) {
                        if (!self || i >= self->m_spec.stops.size()) return;
                        self->m_spec.stops.erase(self->m_spec.stops.begin() + i);
                        self->refreshPreview();
                        self->notifyChanged();
                        self->scheduleBodyRebuild();
                    });
                btn->setPosition({bodyW - 18.f, rowH / 2.f});
                menu->addChild(btn);
            }
        }

        rows.push_back(row);
    }

    if (m_spec.stops.size() < kMaxStops) {
        auto* addRow = CCNode::create();
        addRow->setAnchorPoint({0.f, 0.f});
        addRow->setContentSize({bodyW, 32.f});

        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        menu->setTouchPriority(
            CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2);
        addRow->addChild(menu, 5);

        if (auto* spr = ButtonSprite::create("+ Color", "goldFont.fnt",
                                             "GJ_button_01.png", 0.7f)) {
            spr->setScale(0.5f);
            Ref<GradientEditorPopup> self = this;
            auto* btn = CCMenuItemExt::createSpriteExtra(spr,
                [self](CCMenuItemSpriteExtra*) {
                    if (!self) return;
                    GradientStop stop;
                    stop.pos = 0.5f;
                    stop.color = GradientRasterizer::sample(self->m_spec, 0.5f);
                    self->m_spec.stops.push_back(stop);
                    std::sort(self->m_spec.stops.begin(), self->m_spec.stops.end(),
                        [](GradientStop const& a, GradientStop const& b) {
                            return a.pos < b.pos;
                        });
                    self->refreshPreview();
                    self->notifyChanged();
                    self->scheduleBodyRebuild();
                });
            btn->setPosition({bodyW / 2.f, 16.f});
            menu->addChild(btn);
        }
        rows.push_back(addRow);
    }

    float const scrollTop = size.height - 52.f - kit::kTabBarHeight - 6.f;
    m_body = kit::makeScrollStack({bodyW, scrollTop - 12.f}, rows, 4.f);
    if (m_body) {
        m_body->setPosition({left, 12.f});
        m_bodyHost->addChild(m_body);
    }
}

void GradientEditorPopup::refreshPreview() {
    if (!m_preview) return;

    // sample() walks the stops in order, so keep them sorted as they move.
    std::sort(m_spec.stops.begin(), m_spec.stops.end(),
        [](GradientStop const& a, GradientStop const& b) { return a.pos < b.pos; });

    auto buffer = GradientRasterizer::rasterize(96, 96, m_spec);
    if (auto* texture = ts::SpritePreviewRenderer::createTexture(buffer)) {
        m_preview->setTexture(texture);
        m_preview->setTextureRect({0.f, 0.f, 96.f, 96.f});
        m_preview->setScale(kPreviewSide / 96.f);
    }
}

void GradientEditorPopup::notifyChanged() {
    if (m_onChanged) m_onChanged(m_spec);
}

}  // namespace paimon::icon_maker
