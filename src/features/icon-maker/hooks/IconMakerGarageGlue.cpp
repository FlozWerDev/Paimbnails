#include "IconMakerGarageGlue.hpp"

#include "../data/IconAnatomy.hpp"
#include "../persist/IconProjectStore.hpp"
#include "../services/IconApplier.hpp"
#include "../services/IconBuildService.hpp"
#include "../services/IconThumbs.hpp"
#include "../services/MoreIconsBridge.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace paimon::icon_maker::garage {

namespace {

constexpr int kCols = 12;
constexpr int kRows = 3;
constexpr int kPerPage = kCols * kRows;
constexpr char const* kCustomBtnPrefix = "paimbnails/custom-icon-";

bool shouldExtendNativeBar(IconType type) {
    if (!paimon::settings::icon_maker::enabled()) return false;
    if (MoreIconsBridge::available()) return false;
    return anatomyFor(type) != nullptr;
}

std::vector<IconIndexEntry> customEntriesFor(IconType type) {
    auto& store = IconProjectStore::get();
    store.loadIndex();
    std::vector<IconIndexEntry> out;
    for (auto const& entry : store.list()) {
        if (entry.type == type) out.push_back(entry);
    }
    // Stable creation order so newly created icons always appear at the very end.
    std::sort(out.begin(), out.end(), [](IconIndexEntry const& a, IconIndexEntry const& b) {
        if (a.createdAt != b.createdAt) return a.createdAt < b.createdAt;
        return a.id < b.id;
    });
    return out;
}

CCMenu* iconPageMenu(GJGarageLayer* layer) {
    if (!layer || !layer->m_iconSelection || !layer->m_iconSelection->m_pages) return nullptr;
    auto* page = typeinfo_cast<CCNode*>(layer->m_iconSelection->m_pages->firstObject());
    return page ? page->getChildByType<CCMenu>(0) : nullptr;
}

struct GridLayout {
    float originX = -165.f;
    float originY = 30.f;
    float stepX = 30.f;
    float stepY = 30.f;
    bool valid = false;
};

GridLayout measureGrid(CCMenu* menu) {
    GridLayout g;
    if (!menu || !menu->getChildren() || menu->getChildrenCount() == 0) return g;

    std::vector<CCPoint> pts;
    pts.reserve(menu->getChildrenCount());
    for (auto* child : CCArrayExt<CCNode*>(menu->getChildren())) {
        if (child) pts.push_back(child->getPosition());
    }
    if (pts.empty()) return g;

    g.originX = pts[0].x;
    g.originY = pts[0].y;
    if (pts.size() >= 2) {
        float dx = pts[1].x - pts[0].x;
        if (dx > 10.f && dx < 60.f) g.stepX = dx;
    }
    if (pts.size() > static_cast<std::size_t>(kCols)) {
        float dy = pts[0].y - pts[kCols].y;
        if (dy > 10.f && dy < 60.f) g.stepY = dy;
    } else {
        g.stepY = g.stepX;
    }
    g.valid = true;
    return g;
}

void moveCursorToNode(GJGarageLayer* layer, CCNode* target) {
    if (!layer || !layer->m_cursor1) return;
    if (!target || !target->getParent()) {
        layer->m_cursor1->setVisible(false);
        return;
    }
    auto world = target->getParent()->convertToWorldSpace(target->getPosition());
    layer->m_cursor1->setPosition(layer->convertToNodeSpace(world));
    layer->m_cursor1->setVisible(true);
}

void syncCursor(GJGarageLayer* layer, IconType type) {
    if (!layer || !layer->m_cursor1) return;
    auto activeId = IconApplier::get().activeFor(type);
    if (activeId.empty()) return;

    auto* menu = iconPageMenu(layer);
    if (!menu) {
        layer->m_cursor1->setVisible(false);
        return;
    }
    std::string targetNodeId = std::string(kCustomBtnPrefix) + activeId;
    if (auto* btn = menu->getChildByID(targetNodeId)) {
        moveCursorToNode(layer, btn);
    } else {
        layer->m_cursor1->setVisible(false);
    }
}

void applyEquippedToGarageDoll(GJGarageLayer* layer, IconType type) {
    if (!layer || !layer->m_playerObject) return;
    auto* gm = GameManager::get();
    if (!gm) return;

    layer->m_selectedIconType = type;
    layer->m_iconID = gm->activeIconForType(type);
    layer->m_playerObject->updatePlayerFrame(layer->m_iconID, type);
    layer->updatePlayerColors();
    IconApplier::get().applyExactColors(layer->m_playerObject, type);
}

void onCustomIconPicked(GJGarageLayer* layer, IconType type, std::string const& slotId,
                        CCNode* buttonNode) {
    if (!layer || slotId.empty()) return;

    auto loaded = IconProjectStore::get().loadProject(slotId);
    if (!loaded) {
        Notification::create("No se pudo cargar este icono.", NotificationIcon::Error, 2.f)->show();
        return;
    }
    auto project = loaded.unwrap();

    auto equipNow = [layerRef = Ref<GJGarageLayer>(layer),
                     btnRef = Ref<CCNode>(buttonNode), type, slotId]() {
        if (paimon::isRuntimeShuttingDown() || !layerRef) return;
        IconApplier::get().invalidate(slotId);
        IconApplier::get().setActive(type, slotId);
        applyEquippedToGarageDoll(layerRef, type);
        if (btnRef) {
            moveCursorToNode(layerRef, btnRef);
        } else {
            syncCursor(layerRef, type);
        }
    };

    if (project.hasBuiltOnce) {
        equipNow();
        return;
    }

    // First-time click on a not-yet-compiled custom icon: compile seamlessly and equip!
    Notification::create("Preparando icono...", NotificationIcon::Info, 1.2f)->show();
    IconBuildService::buildAndApply(std::move(project),
        [layerRef = Ref<GJGarageLayer>(layer), btnRef = Ref<CCNode>(buttonNode),
         type, slotId](geode::Result<std::string> res) {
            if (paimon::isRuntimeShuttingDown() || !layerRef) return;
            if (!res) {
                Notification::create(("Error: " + res.unwrapErr()).c_str(),
                    NotificationIcon::Error, 2.5f)->show();
                return;
            }
            applyEquippedToGarageDoll(layerRef, type);
            if (btnRef && btnRef->getParent()) {
                moveCursorToNode(layerRef, btnRef);
            } else {
                syncCursor(layerRef, type);
            }
        });
}

CCMenuItemSpriteExtra* makeCustomIconButton(GJGarageLayer* layer,
                                            IconIndexEntry const& entry) {
    constexpr float kBox = 26.f;
    auto* wrap = CCNode::create();
    wrap->setAnchorPoint({0.5f, 0.5f});
    wrap->setContentSize({kBox, kBox});

    // Subtle dark backing plate so bright/transparent custom icons stay readable.
    if (auto* bg = paimon::SpriteHelper::createColorPanel(
            kBox, kBox, {12, 22, 46}, 125, 4.f)) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setPosition({0.f, 0.f});
        wrap->addChild(bg, -1);
    }

    // Tiny cyan marker in top-right corner so custom icons stand out cleanly.
    if (auto* dot = paimon::SpriteHelper::createColorPanel(
            4.5f, 4.5f, {80, 230, 255}, 235, 2.f)) {
        dot->setAnchorPoint({1.f, 1.f});
        dot->setPosition({kBox - 1.5f, kBox - 1.5f});
        wrap->addChild(dot, 3);
    }

    Ref<CCNode> wrapRef = wrap;
    IconThumbs::get().request(entry.id, [wrapRef, kBox](CCTexture2D* texture) {
        if (paimon::isRuntimeShuttingDown() || !wrapRef || !texture) return;
        auto* spr = CCSprite::createWithTexture(texture);
        if (!spr) return;
        float const longest = std::max(spr->getContentSize().width,
                                       spr->getContentSize().height);
        if (longest > 0.f) {
            // Render icon at native 1:1 visual scale inside the 26pt cell
            // (canvas is 2x the guide box, so fill ~1.65x cell to match vanilla size).
            spr->setScale((kBox * 1.65f) / longest);
        }
        spr->setPosition({kBox * 0.5f, kBox * 0.5f});
        wrapRef->addChild(spr, 1);
    });

    Ref<GJGarageLayer> layerRef = layer;
    std::string slotId = entry.id;
    IconType type = entry.type;
    auto* btn = CCMenuItemExt::createSpriteExtra(wrap,
        [layerRef, type, slotId](CCMenuItemSpriteExtra* sender) {
            if (paimon::isRuntimeShuttingDown() || !layerRef) return;
            onCustomIconPicked(layerRef, type, slotId, sender);
        });
    if (btn) {
        btn->setID(std::string(kCustomBtnPrefix) + entry.id);
        btn->setTag(-1000);  // distinct from positive vanilla icon IDs
    }
    return btn;
}

void rebuildNavDots(GJGarageLayer* layer, int activePage, int vanillaPages, int totalPages) {
    if (!layer || !layer->m_navDotMenu) return;

    auto* navMenu = layer->m_navDotMenu;
    navMenu->removeAllChildren();
    if (layer->m_pageButtons) {
        layer->m_pageButtons->removeAllObjects();
    }

    if (totalPages <= 1) {
        if (layer->m_leftArrow)  layer->m_leftArrow->setVisible(false);
        if (layer->m_rightArrow) layer->m_rightArrow->setVisible(false);
        return;
    }

    if (layer->m_leftArrow)  layer->m_leftArrow->setVisible(true);
    if (layer->m_rightArrow) layer->m_rightArrow->setVisible(true);

    float const dotStep = totalPages > 16 ? 12.f : 16.f;
    float const startX = -dotStep * static_cast<float>(totalPages - 1) * 0.5f;

    for (int p = 0; p < totalPages; ++p) {
        bool const active = (p == activePage);
        bool const isCustomPage = (p >= vanillaPages);
        auto* spr = CCSprite::createWithSpriteFrameName(
            active ? "gj_navDotBtn_on_001.png" : "gj_navDotBtn_off_001.png");
        if (!spr) continue;
        spr->setScale(0.8f);
        if (isCustomPage) {
            spr->setColor(active ? ccColor3B{110, 245, 255} : ccColor3B{90, 185, 220});
        }
        auto* dot = CCMenuItemSpriteExtra::create(
            spr, layer, menu_selector(GJGarageLayer::onNavigate));
        if (!dot) continue;
        dot->setTag(p);
        dot->setPosition({startX + static_cast<float>(p) * dotStep, 0.f});
        navMenu->addChild(dot);
        if (layer->m_pageButtons) {
            layer->m_pageButtons->addObject(dot);
        }
    }
}

}  // anonymous namespace

void onPlayerColorChanged(GJGarageLayer* layer) {
    if (!layer) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    Ref<GJGarageLayer> ref = layer;
    Loader::get()->queueInMainThread([ref]() {
        if (paimon::isRuntimeShuttingDown() || !ref) return;
        if (auto* player = ref->m_playerObject) {
            IconApplier::get().applyExactColors(player, ref->m_selectedIconType);
        }
    });
}

int vanillaPagesFor(IconType type) {
    auto* gm = GameManager::get();
    int const vanillaCount = gm ? std::max(gm->countForType(type), 0) : 0;
    return std::max(1, (vanillaCount + kPerPage - 1) / kPerPage);
}

int totalPagesFor(IconType type) {
    if (!shouldExtendNativeBar(type)) return 0;
    auto customs = customEntriesFor(type);
    if (customs.empty()) return 0;

    auto* gm = GameManager::get();
    int const vanillaCount = gm ? std::max(gm->countForType(type), 0) : 0;
    int const total = vanillaCount + static_cast<int>(customs.size());
    return std::max(1, (total + kPerPage - 1) / kPerPage);
}

void injectCustomIconsOnPage(GJGarageLayer* layer, int requestedPage, IconType type,
                             bool isExtraCustomPage) {
    if (!layer || !shouldExtendNativeBar(type)) return;

    auto customs = customEntriesFor(type);
    if (customs.empty()) return;

    auto* gm = GameManager::get();
    int const vanillaCount = gm ? std::max(gm->countForType(type), 0) : 0;
    int const vPages = std::max(1, (vanillaCount + kPerPage - 1) / kPerPage);
    int const total = vanillaCount + static_cast<int>(customs.size());
    int const tPages = std::max(1, (total + kPerPage - 1) / kPerPage);

    int const page = ((requestedPage % tPages) + tPages) % tPages;

    auto* menu = iconPageMenu(layer);
    if (!menu) return;

    auto const grid = measureGrid(menu);

    if (isExtraCustomPage) {
        menu->removeAllChildren();
        layer->m_iconPages[type] = page;

        int const startCustom = page * kPerPage - vanillaCount;
        for (int slot = 0; slot < kPerPage; ++slot) {
            int const cIdx = startCustom + slot;
            if (cIdx < 0 || cIdx >= static_cast<int>(customs.size())) continue;
            if (auto* btn = makeCustomIconButton(layer, customs[static_cast<std::size_t>(cIdx)])) {
                int const col = slot % kCols;
                int const row = slot / kCols;
                btn->setPosition({
                    grid.originX + static_cast<float>(col) * grid.stepX,
                    grid.originY - static_cast<float>(row) * grid.stepY,
                });
                menu->addChild(btn);
            }
        }
    } else if (page == vPages - 1) {
        int const usedSlots = vanillaCount % kPerPage;
        if (usedSlots > 0) {
            for (int slot = usedSlots; slot < kPerPage; ++slot) {
                int const cIdx = slot - usedSlots;
                if (cIdx >= static_cast<int>(customs.size())) break;
                if (auto* btn = makeCustomIconButton(layer, customs[static_cast<std::size_t>(cIdx)])) {
                    int const col = slot % kCols;
                    int const row = slot / kCols;
                    btn->setPosition({
                        grid.originX + static_cast<float>(col) * grid.stepX,
                        grid.originY - static_cast<float>(row) * grid.stepY,
                    });
                    menu->addChild(btn);
                }
            }
        }
    }

    if (tPages > vPages) {
        rebuildNavDots(layer, page, vPages, tPages);
    }

    syncCursor(layer, type);
}

void onVanillaIconSelected(GJGarageLayer* layer, CCObject* sender) {
    if (!layer || !sender) return;
    if (!shouldExtendNativeBar(layer->m_iconType)) return;
    if (sender->getTag() < 0) return;  // one of our custom icon buttons

    if (!IconApplier::get().activeFor(layer->m_iconType).empty()) {
        IconApplier::get().clearActive(layer->m_iconType);
        Ref<GJGarageLayer> ref = layer;
        Loader::get()->queueInMainThread([ref]() {
            if (paimon::isRuntimeShuttingDown() || !ref) return;
            ref->updatePlayerColors();
        });
    }
}

void refreshGarage(GJGarageLayer* layer) {
    if (!layer) return;
    if (!paimon::settings::icon_maker::enabled()) return;

    IconProjectStore::get().loadIndex();
    IconType const pageType = layer->m_iconType;
    int page = 0;
    if (auto it = layer->m_iconPages.find(pageType); it != layer->m_iconPages.end()) {
        page = it->second;
    }
    int const totalPages = totalPagesFor(pageType);
    if (totalPages > 0) {
        page = std::clamp(page, 0, totalPages - 1);
    }
    if (anatomyFor(pageType)) {
        layer->setupPage(page, pageType);
    }
    if (auto* gm = GameManager::get(); gm && layer->m_playerObject) {
        IconType const dollType = layer->m_selectedIconType;
        layer->m_playerObject->updatePlayerFrame(gm->activeIconForType(dollType), dollType);
        layer->updatePlayerColors();
        IconApplier::get().applyExactColors(layer->m_playerObject, dollType);
    }
}

}  // namespace paimon::icon_maker::garage
