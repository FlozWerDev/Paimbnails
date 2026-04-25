#include "CaptureLayerEditorPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "CapturePreviewPopup.hpp"
#include "CaptureUIConstants.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <Geode/cocos/kazmath/include/kazmath/mat4.h>
#include <set>
#include <algorithm>
#include <cstring>

using namespace geode::prelude;
using namespace cocos2d;

// Shared static for restoreAllLayers() — populated by instances, cleared on restore
static std::vector<std::pair<WeakRef<CCNode>, bool>> s_originalVisibilities;

// Clear WeakRefs during shutdown to avoid interaction with dead CCPoolManager
$on_game(Exiting) {
    s_originalVisibilities.clear();
}

// ─── auxiliares ───────────────────────────────────────────────────

namespace {
    static std::string simplifyClassName(std::string const& cls) {
        std::string name = cls;
        for (char const* prefix : {"class ", "struct "}) {
            if (name.find(prefix) == 0) {
                name = name.substr(std::strlen(prefix));
            }
        }
        auto pos = name.find('<');
        if (pos != std::string::npos) name = name.substr(0, pos);
        if (name.length() > 24) name = name.substr(0, 21) + "...";
        return name;
    }

    static std::string tr(std::string const& es, std::string const& en) {
        return Localization::get().getLanguage() == Localization::Language::SPANISH ? es : en;
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    static std::string describeNode(CCNode* node) {
        if (!node) return {};
        std::string id = node->getID();
        if (!id.empty()) return id;
        auto cls = simplifyClassName(typeid(*node).name());
        if (typeinfo_cast<CCParticleSystem*>(node)) {
            return tr("Partículas", "Particles") + " (" + cls + ")";
        }
        if (typeinfo_cast<CCMenu*>(node)) {
            return tr("Menú", "Menu") + " (" + cls + ")";
        }
        return cls;
    }

    static bool looksLikeEffectNode(CCNode* node) {
        if (!node) return false;
        if (typeinfo_cast<ShaderLayer*>(node)) return true;
        if (typeinfo_cast<CCParticleSystem*>(node)) return true;
        auto cls = toLower(typeid(*node).name());
        auto id = toLower(node->getID());
        static std::vector<std::string> patterns = {
            "shader", "effect", "particle", "trail", "glow", "bloom", "swing", "dash", "fire"
        };
        for (auto const& pattern : patterns) {
            if (cls.find(pattern) != std::string::npos || id.find(pattern) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    // CCMenu subclass that clips touch testing to a parent ScrollLayer's
    // visible rect. Prevents togglers scrolled out of view from stealing clicks.
    class ClippedMenu : public CCMenu {
    public:
        static ClippedMenu* create(CCNode* clipParent) {
            auto ret = new ClippedMenu();
            if (ret && ret->init()) {
                ret->m_clipParent = clipParent;
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
            if (m_clipParent) {
                auto worldPt = touch->getLocation();
                auto parentPos = m_clipParent->convertToWorldSpace({0.f, 0.f});
                auto parentSize = m_clipParent->getContentSize();
                // Scale-aware: use the parent's actual world-space bounds
                auto parentScale = m_clipParent->getScale();
                float w = parentSize.width * parentScale;
                float h = parentSize.height * parentScale;
                if (worldPt.x < parentPos.x || worldPt.x > parentPos.x + w ||
                    worldPt.y < parentPos.y || worldPt.y > parentPos.y + h) {
                    return false;
                }
            }
            return CCMenu::ccTouchBegan(touch, event);
        }

    private:
        CCNode* m_clipParent = nullptr;
    };
}

// ─── api estatica ────────────────────────────────────────────────

CaptureLayerEditorPopup* CaptureLayerEditorPopup::create(CapturePreviewPopup* previewPopup) {
    auto ret = new CaptureLayerEditorPopup();
    ret->m_previewPopup = previewPopup;
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void CaptureLayerEditorPopup::restoreAllLayers() {
    if (PlayLayer::get()) {
        for (auto& [node, vis] : s_originalVisibilities) {
            if (auto locked = node.lock()) {
                locked->setVisible(vis);
            }
        }
    }
    s_originalVisibilities.clear();
    log::info("[LayerEditor] All layers restored to original visibility");
}

// ─── init ──────────────────────────────────────────────────────

bool CaptureLayerEditorPopup::init() {
    namespace C = paimon::capture::layers;

    if (!Popup::init(C::POPUP_WIDTH, C::POPUP_HEIGHT)) return false;
    this->setTitle(Localization::get().getString("layers.title").c_str());

    auto content = m_mainLayer->getContentSize();

    populateLayers();

    if (m_layers.empty()) {
        auto noLabel = CCLabelBMFont::create(
            Localization::get().getString("layers.no_playlayer").c_str(),
            "bigFont.fnt"
        );
        noLabel->setScale(0.4f);
        noLabel->setPosition({content.width * 0.5f, content.height * 0.5f});
        m_mainLayer->addChild(noLabel);
        return true;
    }

    // ── mini preview ─────────────────────────────────────────────
    const float previewW = C::MINI_PREVIEW_W;
    const float previewH = C::MINI_PREVIEW_H;
    const float previewY = content.height - C::MINI_PREVIEW_TOP_PAD - previewH * 0.5f;

    auto previewBg = CCLayerColor::create({0, 0, 0, 200});
    previewBg->setContentSize({previewW + 4.f, previewH + 4.f});
    previewBg->ignoreAnchorPointForPosition(false);
    previewBg->setAnchorPoint({0.5f, 0.5f});
    previewBg->setPosition({content.width * 0.5f, previewY});
    m_mainLayer->addChild(previewBg, 0);

    m_miniPreview = CCSprite::create();
    m_miniPreview->setContentSize({previewW, previewH});
    m_miniPreview->setPosition({content.width * 0.5f, previewY});
    m_mainLayer->addChild(m_miniPreview, 1);

    updateMiniPreview();

    // ── filter button ────────────────────────────────────────────
    const float filterY = previewY - previewH * 0.5f - C::FILTER_GAP_BELOW_PREVIEW;
    auto filterMenu = CCMenu::create();
    filterMenu->setPosition({0.f, 0.f});

    auto filterSpr = ButtonSprite::create(
        (tr("Filtro: Todo", "Filter: All") + "  \xe2\x96\xbc").c_str(),
        static_cast<int>(C::FILTER_BTN_WIDTH), true, "bigFont.fnt",
        "GJ_button_04.png", C::FILTER_BTN_HEIGHT, 0.3f
    );
    m_filterLabel = filterSpr ? filterSpr->getChildByType<CCLabelBMFont>(0) : nullptr;

    auto filterBtn = CCMenuItemSpriteExtra::create(
        filterSpr, this,
        menu_selector(CaptureLayerEditorPopup::onFilterBtn)
    );
    filterBtn->setPosition({content.width * 0.5f, filterY});
    PaimonButtonHighlighter::registerButton(filterBtn);
    filterMenu->addChild(filterBtn);
    m_mainLayer->addChild(filterMenu, 3);

    // ── lista de capas ───────────────────────────────────────────
    buildList();

    // ── botones inferiores ───────────────────────────────────────
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({content.width * 0.5f, 20.f});
    btnMenu->setID("bottom-buttons"_spr);

    auto restoreSpr = ButtonSprite::create(
        Localization::get().getString("layers.restore_all").c_str(),
        70, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.35f
    );
    if (restoreSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            restoreSpr, this,
            menu_selector(CaptureLayerEditorPopup::onRestoreAllBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    auto doneSpr = ButtonSprite::create(
        Localization::get().getString("layers.done").c_str(),
        70, true, "bigFont.fnt", "GJ_button_02.png", 22.f, 0.35f
    );
    if (doneSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            doneSpr, this,
            menu_selector(CaptureLayerEditorPopup::onDoneBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    btnMenu->alignItemsHorizontallyWithPadding(10.f);
    m_mainLayer->addChild(btnMenu);

    paimon::markDynamicPopup(this);
    return true;
}

void CaptureLayerEditorPopup::onClose(CCObject* sender) {
    Popup::onClose(sender);
}

void CaptureLayerEditorPopup::keyBackClicked() {
    Popup::keyBackClicked();
}

void CaptureLayerEditorPopup::onExit() {
    Popup::onExit();
}

// ─── enumeracion de capas ─────────────────────────────────────────

void CaptureLayerEditorPopup::populateLayers() {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* scene = CCDirector::sharedDirector()->getRunningScene();

    m_layers.clear();

    bool needRecordOriginals = s_originalVisibilities.empty();
    std::set<CCNode*> addedNodes;
    std::set<CCNode*> recordedNodes;

    auto addEntry = [&](CCNode* node, std::string const& name, bool isGroup, int depth, int parent) {
        LayerEntry entry;
        entry.node = node;
        entry.name = name;
        entry.currentVisibility = node ? node->isVisible() : true;
        entry.originalVisibility = entry.currentVisibility;
        entry.isGroup = isGroup;
        entry.depth = depth;
        entry.parentIndex = parent;

        int idx = static_cast<int>(m_layers.size());
        m_layers.push_back(std::move(entry));
        if (parent >= 0 && parent < static_cast<int>(m_layers.size())) {
            m_layers[parent].childIndices.push_back(idx);
        }

        if (!isGroup && node) {
            addedNodes.insert(node);
            if (needRecordOriginals && recordedNodes.insert(node).second) {
                s_originalVisibilities.push_back({WeakRef<CCNode>(node), node->isVisible()});
                m_originalVisibilities.push_back({WeakRef<CCNode>(node), node->isVisible()});
            }
        }
        return idx;
    };

    auto addGroup = [&](std::string const& name, int parent, int depth) {
        return addEntry(nullptr, name, true, depth, parent);
    };

    auto addLeaf = [&](CCNode* node, std::string const& name, int parent, int depth) {
        if (!node || addedNodes.count(node)) return -1;
        return addEntry(node, name, false, depth, parent);
    };

    // ── Jugadores ────────────────────────────────────────────────
    auto addPlayerGroup = [&](PlayerObject* player, std::string const& playerName) {
        if (!player) return;

        int playerGroup = addGroup(playerName, -1, 0);
        addLeaf(player, tr("Cuerpo", "Body"), playerGroup, 1);

        int trailsGroup = -1;
        auto addTrail = [&](CCNode* node, std::string const& name) {
            if (!node) return;
            if (trailsGroup == -1) trailsGroup = addGroup(tr("Trazos", "Trails"), playerGroup, 1);
            addLeaf(node, name, trailsGroup, 2);
        };

        int particlesGroup = -1;
        auto addParticle = [&](CCNode* node, std::string const& name) {
            if (!node) return;
            if (particlesGroup == -1) particlesGroup = addGroup(tr("Partículas", "Particles"), playerGroup, 1);
            addLeaf(node, name, particlesGroup, 2);
        };

        addTrail(player->m_regularTrail, tr("Trazo normal", "Regular trail"));
        addTrail(player->m_waveTrail, tr("Trazo wave", "Wave trail"));
        addTrail(player->m_ghostTrail, tr("Trazo ghost", "Ghost trail"));

        addParticle(player->m_vehicleGroundParticles, tr("Polvo vehículo", "Vehicle dust"));
        addParticle(player->m_robotFire, tr("Fuego robot", "Robot fire"));
        addParticle(player->m_playerGroundParticles, tr("Polvo suelo", "Ground particles"));
        addParticle(player->m_trailingParticles, tr("Partículas trail", "Trailing particles"));
        addParticle(player->m_shipClickParticles, tr("Click ship", "Ship click particles"));
        addParticle(player->m_ufoClickParticles, tr("Click UFO", "UFO click particles"));
        addParticle(player->m_robotBurstParticles, tr("Explosión robot", "Robot burst"));
        addParticle(player->m_dashParticles, tr("Partículas dash", "Dash particles"));
        addParticle(player->m_swingBurstParticles1, tr("Swing burst 1", "Swing burst 1"));
        addParticle(player->m_swingBurstParticles2, tr("Swing burst 2", "Swing burst 2"));
        addParticle(player->m_landParticles0, tr("Aterrizaje 0", "Landing particles 0"));
        addParticle(player->m_landParticles1, tr("Aterrizaje 1", "Landing particles 1"));
        addParticle(player->m_dashFireSprite, tr("Fuego dash", "Dash fire"));

        int extrasGroup = -1;
        std::set<CCNode*> skipped = {
            player,
            player->m_regularTrail, player->m_waveTrail, player->m_ghostTrail,
            player->m_vehicleGroundParticles, player->m_robotFire,
            player->m_playerGroundParticles, player->m_trailingParticles,
            player->m_shipClickParticles, player->m_ufoClickParticles,
            player->m_robotBurstParticles, player->m_dashParticles,
            player->m_swingBurstParticles1, player->m_swingBurstParticles2,
            player->m_landParticles0, player->m_landParticles1,
            player->m_dashFireSprite
        };

        auto collectExtras = [&](auto&& self, CCNode* root) -> void {
            if (!root) return;
            auto* children = root->getChildren();
            if (!children) return;
            for (auto* obj : CCArrayExt<CCObject*>(children)) {
                auto* nd = typeinfo_cast<CCNode*>(obj);
                if (!nd) continue;
                if (!skipped.insert(nd).second) { self(self, nd); continue; }
                if (!addedNodes.count(nd)) {
                    if (extrasGroup == -1) extrasGroup = addGroup(tr("Extras", "Extras"), playerGroup, 1);
                    addLeaf(nd, describeNode(nd), extrasGroup, 2);
                }
                self(self, nd);
            }
        };
        collectExtras(collectExtras, player);
    };

    addPlayerGroup(pl->m_player1, Localization::get().getString("layers.player1"));
    addPlayerGroup(pl->m_player2, Localization::get().getString("layers.player2"));

    // ── Escenario (Background / Ground / Middleground) ───────────
    int sceneGroup = -1;
    auto ensureSceneGroup = [&]() {
        if (sceneGroup == -1) sceneGroup = addGroup(tr("Escenario", "Scenery"), -1, 0);
        return sceneGroup;
    };

    addLeaf(pl->m_background,   tr("Fondo (BG)", "Background (BG)"),     ensureSceneGroup(), 1);
    addLeaf(pl->m_middleground, tr("Medio fondo (MG)", "Middleground (MG)"), ensureSceneGroup(), 1);
    addLeaf(pl->m_groundLayer,  tr("Suelo 1", "Ground 1"),               ensureSceneGroup(), 1);
    addLeaf(pl->m_groundLayer2, tr("Suelo 2", "Ground 2"),               ensureSceneGroup(), 1);

    // ── Capas de objetos (Object Layers) ─────────────────────────
    int objectGroup = -1;
    auto ensureObjectGroup = [&]() {
        if (objectGroup == -1) objectGroup = addGroup(tr("Capas de objetos", "Object layers"), -1, 0);
        return objectGroup;
    };

    addLeaf(pl->m_objectLayer,           tr("Objetos principal", "Main objects"),        ensureObjectGroup(), 1);
    addLeaf(pl->m_inShaderObjectLayer,    tr("Objetos in-shader", "In-shader objects"),   ensureObjectGroup(), 1);
    addLeaf(pl->m_aboveShaderObjectLayer, tr("Objetos sobre-shader", "Above-shader objects"), ensureObjectGroup(), 1);

    // Batch nodes (gameplay sprite sheets)
    if (pl->m_batchNodes) {
        int batchGroup = -1;
        for (unsigned int bi = 0; bi < pl->m_batchNodes->count(); ++bi) {
            auto* bn = static_cast<CCNode*>(pl->m_batchNodes->objectAtIndex(bi));
            if (!bn || addedNodes.count(bn)) continue;
            if (batchGroup == -1) batchGroup = addGroup(tr("Batch nodes", "Batch nodes"), ensureObjectGroup(), 1);
            std::string batchName = describeNode(bn);
            if (batchName.empty()) batchName = "Batch #" + std::to_string(bi);
            addLeaf(bn, batchName, batchGroup, 2);
        }
    }

    // ── UI / HUD ─────────────────────────────────────────────────
    int uiGroup = -1;
    auto ensureUiGroup = [&]() {
        if (uiGroup == -1) uiGroup = addGroup(tr("UI / HUD", "UI / HUD"), -1, 0);
        return uiGroup;
    };

    if (pl->m_uiLayer) {
        addLeaf(pl->m_uiLayer, tr("Todo el UI", "All UI"), ensureUiGroup(), 1);
        if (auto* uiChildren = pl->m_uiLayer->getChildren()) {
            int uiChildrenGroup = -1;
            for (auto* obj : CCArrayExt<CCNode*>(uiChildren)) {
                if (!obj || addedNodes.count(obj)) continue;
                if (uiChildrenGroup == -1) uiChildrenGroup = addGroup(tr("Componentes UI", "UI components"), ensureUiGroup(), 1);
                addLeaf(obj, describeNode(obj), uiChildrenGroup, 2);
            }
        }
    }

    addLeaf(pl->m_attemptLabel, tr("Texto de intento", "Attempt label"), ensureUiGroup(), 1);
    addLeaf(pl->m_percentageLabel, tr("Porcentaje", "Percentage label"), ensureUiGroup(), 1);

    // ── Efectos (Shaders / Particles) ────────────────────────────
    int effectsGroup = -1;
    auto ensureEffectsGroup = [&]() {
        if (effectsGroup == -1) effectsGroup = addGroup(Localization::get().getString("layers.effects"), -1, 0);
        return effectsGroup;
    };

    addLeaf(pl->m_shaderLayer, tr("Shader layer", "Shader layer"), ensureEffectsGroup(), 1);

    // ── Mods / Overlays (nodes with '/' in ID = Geode mods) ─────
    int modGroup = -1;
    auto ensureModGroup = [&]() {
        if (modGroup == -1) modGroup = addGroup(tr("Mods / Overlays", "Mods / Overlays"), -1, 0);
        return modGroup;
    };

    // ── Gameplay / Escena (remaining PlayLayer children) ─────────
    int gameplayGroup = -1;
    auto ensureGameplayGroup = [&]() {
        if (gameplayGroup == -1) gameplayGroup = addGroup(tr("Gameplay / Escena", "Gameplay / Scene"), -1, 0);
        return gameplayGroup;
    };

    if (auto* plChildren = pl->getChildren()) {
        for (auto* obj : CCArrayExt<CCNode*>(plChildren)) {
            if (!obj || addedNodes.count(obj)) continue;
            auto size = obj->getContentSize();
            bool hasVisualSize = size.width >= 0.1f || size.height >= 0.1f;
            bool hasChildren = obj->getChildren() && obj->getChildren()->count() > 0;
            if (!hasVisualSize && !hasChildren) continue;

            std::string nid = obj->getID();

            // Mod overlays: IDs containing '/' are from Geode mods
            if (nid.find('/') != std::string::npos) {
                addLeaf(obj, nid.empty() ? describeNode(obj) : nid, ensureModGroup(), 1);
                continue;
            }

            // Known UI-like classes
            if (typeinfo_cast<CCMenu*>(obj) || typeinfo_cast<CCLabelBMFont*>(obj) ||
                typeinfo_cast<CCLabelTTF*>(obj)) {
                addLeaf(obj, describeNode(obj), ensureUiGroup(), 1);
                continue;
            }

            if (looksLikeEffectNode(obj)) {
                addLeaf(obj, describeNode(obj), ensureEffectsGroup(), 1);
            } else {
                addLeaf(obj, describeNode(obj), ensureGameplayGroup(), 1);
            }
        }
    }

    // ── Overlays de escena (scene children outside PlayLayer) ────
    int overlayGroup = -1;
    auto ensureOverlayGroup = [&]() {
        if (overlayGroup == -1) overlayGroup = addGroup(tr("Overlays de escena", "Scene overlays"), -1, 0);
        return overlayGroup;
    };

    if (scene) {
        for (auto* obj : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!obj || obj == pl || addedNodes.count(obj)) continue;
            // Include invisible nodes too — the user might want to toggle them on
            if (typeinfo_cast<FLAlertLayer*>(obj)) continue;
            std::string cls = typeid(*obj).name();
            if (cls.find("PauseLayer") != std::string::npos) continue;

            std::string nid = obj->getID();
            auto size = obj->getContentSize();
            bool hasVisualSize = size.width >= 0.1f || size.height >= 0.1f;
            bool hasChildren = obj->getChildren() && obj->getChildren()->count() > 0;
            if (!hasVisualSize && !hasChildren) continue;

            // Mod overlays at scene level
            if (nid.find('/') != std::string::npos) {
                addLeaf(obj, nid, ensureModGroup(), 1);
            } else {
                addLeaf(obj, describeNode(obj), ensureOverlayGroup(), 1);
            }
        }
    }

    log::info("[LayerEditor] Enumerated {} layer entries", m_layers.size());
}

// ─── visibility helpers ───────────────────────────────────────────

bool CaptureLayerEditorPopup::isEntryVisible(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return false;
    auto const& entry = m_layers[idx];

    // For groups: visible only if ALL children are visible
    // This makes the group checkbox a "select all / deselect all" control
    if (!entry.childIndices.empty()) {
        for (int child : entry.childIndices) {
            if (!isEntryVisible(child)) return false;
        }
        return true;
    }

    return entry.node ? entry.node->isVisible() : entry.currentVisibility;
}

void CaptureLayerEditorPopup::setEntryVisible(int idx, bool visible, bool cascadeChildren) {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    auto& entry = m_layers[idx];

    if (entry.isGroup) {
        // Groups have no node; just propagate to children
        entry.currentVisibility = visible;
        if (cascadeChildren) {
            for (int child : entry.childIndices) {
                setEntryVisible(child, visible, true);
            }
        }
    } else {
        // Always apply the explicit visibility value.
        // The "Restore All" button handles restoring to originals.
        entry.currentVisibility = visible;
        if (entry.node) entry.node->setVisible(visible);
        if (cascadeChildren) {
            for (int child : entry.childIndices) {
                setEntryVisible(child, visible, true);
            }
        }
    }
}

// ─── in-place visual refresh (no rebuild) ─────────────────────────

void CaptureLayerEditorPopup::refreshRowVisuals(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    auto& entry = m_layers[idx];

    bool vis = isEntryVisible(idx);

    // Update toggler visual without triggering callback
    if (entry.toggler) {
        bool isCurrentlyToggled = entry.toggler->isToggled();
        bool shouldBeToggled = vis; // toggled (checkOn shown) = layer is visible
        if (isCurrentlyToggled != shouldBeToggled) {
            entry.toggler->toggle(shouldBeToggled);
        }
    }

    // Update label color
    if (entry.label) {
        if (entry.isGroup) {
            entry.label->setColor(vis ? ccColor3B{255, 226, 120} : ccColor3B{120, 110, 80});
        } else {
            entry.label->setColor(vis ? ccColor3B{255, 255, 255} : ccColor3B{130, 130, 130});
        }
    }
}

// ─── build list UI ─────────────────────────────────────────────────

bool CaptureLayerEditorPopup::entryMatchesFilter(int idx) const {
    if (m_filterGroupIndex < 0) return true; // show all
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return false;
    if (idx == m_filterGroupIndex) return true;
    // Check if this entry is a descendant of the selected group
    int parent = m_layers[idx].parentIndex;
    while (parent >= 0) {
        if (parent == m_filterGroupIndex) return true;
        parent = m_layers[parent].parentIndex;
    }
    return false;
}

void CaptureLayerEditorPopup::buildList() {
    namespace C = paimon::capture::layers;

    if (m_listRoot) {
        m_listRoot->removeFromParentAndCleanup(true);
        m_listRoot = nullptr;
        m_scrollView = nullptr;
    }

    // Clear stale UI refs (nodes were just destroyed)
    for (auto& entry : m_layers) {
        entry.toggler = nullptr;
        entry.label = nullptr;
    }

    auto content = m_mainLayer->getContentSize();

    const float listW    = content.width - C::LIST_PAD_X * 2;
    const float rowH     = C::ROW_HEIGHT;
    const float previewH = C::MINI_PREVIEW_H;
    const float previewY = content.height - C::MINI_PREVIEW_TOP_PAD - previewH * 0.5f;
    const float filterY  = previewY - previewH * 0.5f - C::FILTER_GAP_BELOW_PREVIEW;
    const float listTop  = filterY - C::LIST_GAP_BELOW_FILTER;
    const float listBot  = C::LIST_BOT;
    const float viewH    = listTop - listBot;
    const float viewX    = (content.width - listW) * 0.5f;

    // Collect visible (filtered) entry indices
    std::vector<int> visibleIndices;
    for (int i = 0; i < static_cast<int>(m_layers.size()); ++i) {
        if (entryMatchesFilter(i)) visibleIndices.push_back(i);
    }
    int numVisible = static_cast<int>(visibleIndices.size());

    m_listRoot = CCNode::create();
    m_listRoot->setID("list-root"_spr);
    m_mainLayer->addChild(m_listRoot, 2);

    // Dark panel background
    auto panel = paimon::SpriteHelper::createDarkPanel(listW, viewH, 80);
    panel->setPosition({viewX, listBot});
    m_listRoot->addChild(panel, 0);

    float totalH = std::max(viewH, numVisible * rowH);

    // ScrollLayer
    m_scrollView = ScrollLayer::create({listW, viewH});
    m_scrollView->setPosition({viewX, listBot});
    m_scrollView->m_contentLayer->setContentSize({listW, totalH});

    for (int row = 0; row < numVisible; ++row) {
        int i = visibleIndices[row];
        auto& entry = m_layers[i];
        float y = totalH - rowH - row * rowH;

        // Row container
        auto rowNode = CCNode::create();
        rowNode->setContentSize({listW, rowH});
        rowNode->setPosition({0.f, y});
        rowNode->setAnchorPoint({0.f, 0.f});

        // Row background
        if (entry.isGroup) {
            auto bg = CCLayerColor::create({255, 215, 90, C::GROUP_BG_ALPHA});
            bg->setContentSize({listW, rowH});
            bg->setAnchorPoint({0.f, 0.f});
            bg->ignoreAnchorPointForPosition(false);
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -2);

            // Accent bar
            auto accent = CCLayerColor::create({255, 215, 90, C::GROUP_ACCENT_ALPHA});
            accent->setContentSize({C::GROUP_ACCENT_WIDTH, rowH - 4.f});
            accent->ignoreAnchorPointForPosition(false);
            accent->setAnchorPoint({0.f, 0.5f});
            accent->setPosition({4.f + entry.depth * C::DEPTH_INDENT, rowH * 0.5f});
            rowNode->addChild(accent, -1);
        } else if (row % 2 == 0) {
            auto bg = CCLayerColor::create({255, 255, 255, C::ALT_ROW_ALPHA});
            bg->setContentSize({listW, rowH});
            bg->setAnchorPoint({0.f, 0.f});
            bg->ignoreAnchorPointForPosition(false);
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -1);
        }

        // Toggle menu (one per row — ClippedMenu prevents clicks outside scroll area)
        auto rowMenu = ClippedMenu::create(m_scrollView);
        rowMenu->setContentSize({listW, rowH});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setAnchorPoint({0.f, 0.f});

        // Checkbox toggler
        float checkScale = entry.isGroup ? C::CHECK_SCALE_GROUP : C::CHECK_SCALE_LEAF;
        auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        if (onSpr && offSpr) {
            onSpr->setScale(checkScale);
            offSpr->setScale(checkScale);

            auto toggler = CCMenuItemToggler::create(
                offSpr, onSpr,
                this, menu_selector(CaptureLayerEditorPopup::onToggleLayer)
            );
            toggler->setTag(i);

            bool vis = isEntryVisible(i);
            toggler->toggle(vis);

            float checkX = C::CHECK_X_BASE + entry.depth * C::DEPTH_INDENT;
            toggler->setPosition({checkX, rowH * 0.5f});
            rowMenu->addChild(toggler);

            entry.toggler = toggler;
        }

        rowNode->addChild(rowMenu, 1);

        // Label
        std::string labelText = entry.name;
        if (!entry.isGroup) {
            labelText = std::string(entry.depth >= 2 ? "• " : "› ") + labelText;
        }

        auto label = CCLabelBMFont::create(labelText.c_str(), "bigFont.fnt");
        float labelScale = entry.isGroup ? C::LABEL_SCALE_GROUP
                         : (entry.depth >= 2 ? C::LABEL_SCALE_LEAF_D2 : C::LABEL_SCALE_LEAF_D0);
        label->setScale(labelScale);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({C::LABEL_X_BASE + entry.depth * C::DEPTH_INDENT, rowH * 0.5f});

        bool vis = isEntryVisible(i);
        if (entry.isGroup) {
            label->setColor(vis ? ccColor3B{255, 226, 120} : ccColor3B{120, 110, 80});
        } else if (!vis) {
            label->setColor({130, 130, 130});
        }
        rowNode->addChild(label, 2);

        entry.label = label;

        m_scrollView->m_contentLayer->addChild(rowNode);
    }

    m_scrollView->scrollToTop();
    m_listRoot->addChild(m_scrollView, 2);
}

// ─── render mini-preview using CCRenderTexture ──────────────────

void CaptureLayerEditorPopup::updateMiniPreview() {
    namespace C = paimon::capture::layers;
    if (!m_miniPreview) return;

    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* director = CCDirector::sharedDirector();
    auto winSize = director->getWinSize();

    const int rtW = C::RT_WIDTH;
    const int rtH = C::RT_HEIGHT;

    auto* rt = CCRenderTexture::create(rtW, rtH, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) return;

    // Temporarily hide this popup and other system overlays
    bool selfWasVisible = this->isVisible();
    this->setVisible(false);

    auto* scene = director->getRunningScene();
    std::vector<std::pair<CCNode*, bool>> hiddenOverlays;
    if (scene) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child || !child->isVisible() || child == pl) continue;
            if (typeinfo_cast<FLAlertLayer*>(child)) {
                hiddenOverlays.push_back({child, true});
                child->setVisible(false);
            } else {
                std::string cls = typeid(*child).name();
                if (cls.find("PauseLayer") != std::string::npos) {
                    hiddenOverlays.push_back({child, true});
                    child->setVisible(false);
                }
            }
        }
    }

    rt->begin();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    kmGLPushMatrix();
    float sx = static_cast<float>(rtW) / winSize.width;
    float sy = static_cast<float>(rtH) / winSize.height;
    kmMat4 scaleMat;
    std::memset(&scaleMat, 0, sizeof(scaleMat));
    scaleMat.mat[0]  = sx;
    scaleMat.mat[5]  = sy;
    scaleMat.mat[10] = 1.0f;
    scaleMat.mat[15] = 1.0f;
    kmGLMultMatrix(&scaleMat);
    pl->visit();
    kmGLPopMatrix();
    rt->end();

    // Restore overlays
    for (auto& [node, _] : hiddenOverlays) node->setVisible(true);
    this->setVisible(selfWasVisible);

    // Apply texture to mini preview
    auto* rtSprite = rt->getSprite();
    if (rtSprite) {
        auto* rtTex = rtSprite->getTexture();
        if (rtTex) {
            m_miniPreview->setTexture(rtTex);
            m_miniPreview->setTextureRect(CCRect(0, 0, rtW, rtH));
            m_miniPreview->setFlipY(true);

            float scaleToFit = std::min(C::MINI_PREVIEW_W / rtW, C::MINI_PREVIEW_H / rtH);
            m_miniPreview->setScale(scaleToFit);
        }
    }
}

// ─── callbacks ─────────────────────────────────────────────────

void CaptureLayerEditorPopup::onToggleLayer(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int idx = toggler->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;

    // With create(offSpr, onSpr, ...):
    //   - Normal (untoggled): shows offSpr (checkOff) = hidden
    //   - Toggled: shows onSpr (checkOn) = visible
    // After CCMenuItemToggler fires callback, it has already toggled.
    // isToggled() == true means it's now showing onSpr (checkOn) = visible
    bool newVisible = toggler->isToggled();
    // Only cascade to children when toggling a group.
    // For individual leaves, apply the explicit value directly.
    bool cascade = m_layers[idx].isGroup;
    setEntryVisible(idx, newVisible, cascade);

    log::info("[LayerEditor] '{}' -> {}", m_layers[idx].name,
        newVisible ? "visible" : "hidden");

    // Refresh visuals of this entry and related entries in-place
    // (parent groups and children) — NO rebuild!
    refreshRowVisuals(idx);

    // Refresh parent chain
    int parentIdx = m_layers[idx].parentIndex;
    while (parentIdx >= 0) {
        refreshRowVisuals(parentIdx);
        parentIdx = m_layers[parentIdx].parentIndex;
    }

    // Refresh children (for group toggles)
    for (int child : m_layers[idx].childIndices) {
        refreshRowVisuals(child);
        // Also refresh grandchildren
        for (int grandchild : m_layers[child].childIndices) {
            refreshRowVisuals(grandchild);
        }
    }

    updateMiniPreview();
}

// ─── filter dropdown ────────────────────────────────────────────

void CaptureLayerEditorPopup::onFilterBtn(CCObject*) {
    namespace C = paimon::capture::layers;
    if (m_filterDropdown) {
        closeFilterDropdown();
        return;
    }

    if (m_listRoot) m_listRoot->setVisible(false);

    auto content = m_mainLayer->getContentSize();
    const float previewH = C::MINI_PREVIEW_H;
    const float previewY = content.height - C::MINI_PREVIEW_TOP_PAD - previewH * 0.5f;
    const float filterY  = previewY - previewH * 0.5f - C::FILTER_GAP_BELOW_PREVIEW;
    const float areaTop  = filterY - C::LIST_GAP_BELOW_FILTER;
    const float areaBot  = C::LIST_BOT;
    const float areaH    = areaTop - areaBot;
    const float areaW    = content.width - C::LIST_PAD_X * 2;
    const float areaX    = C::LIST_PAD_X;

    // Gather top-level branches
    struct FilterOption { int idx; std::string name; };
    std::vector<FilterOption> opts;
    opts.push_back({-1, tr("Todo", "All")});
    for (int i = 0; i < static_cast<int>(m_layers.size()); ++i) {
        if (m_layers[i].depth == 0) opts.push_back({i, m_layers[i].name});
    }

    m_filterDropdown = CCNode::create();
    m_filterDropdown->setID("filter-dropdown"_spr);

    auto panel = paimon::SpriteHelper::createDarkPanel(areaW, areaH, 100);
    panel->setPosition({areaX, areaBot});
    m_filterDropdown->addChild(panel, 0);

    const float optH = C::OPTION_HEIGHT;
    int numOpts = static_cast<int>(opts.size());
    float totalH = std::max(areaH, numOpts * optH);

    auto scroll = ScrollLayer::create({areaW, areaH});
    scroll->setPosition({areaX, areaBot});
    scroll->m_contentLayer->setContentSize({areaW, totalH});

    for (int o = 0; o < numOpts; ++o) {
        bool active = (opts[o].idx == m_filterGroupIndex);
        float y = totalH - optH - o * optH;

        auto rowNode = CCNode::create();
        rowNode->setContentSize({areaW, optH});
        rowNode->setPosition({0.f, y});
        rowNode->setAnchorPoint({0.f, 0.f});

        if (active) {
            auto bg = CCLayerColor::create({50, 150, 60, 90});
            bg->setContentSize({areaW, optH});
            bg->ignoreAnchorPointForPosition(false);
            bg->setAnchorPoint({0.f, 0.f});
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -1);

            auto accent = CCLayerColor::create({80, 220, 90, 200});
            accent->setContentSize({3.f, optH - 4.f});
            accent->ignoreAnchorPointForPosition(false);
            accent->setAnchorPoint({0.f, 0.5f});
            accent->setPosition({2.f, optH * 0.5f});
            rowNode->addChild(accent, 0);
        } else if (o % 2 == 0) {
            auto bg = CCLayerColor::create({255, 255, 255, 8});
            bg->setContentSize({areaW, optH});
            bg->ignoreAnchorPointForPosition(false);
            bg->setAnchorPoint({0.f, 0.f});
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -1);
        }

        auto lbl = CCLabelBMFont::create(opts[o].name.c_str(), "bigFont.fnt");
        lbl->setScale(0.3f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({active ? 10.f : 8.f, optH * 0.5f});
        lbl->setColor(active ? ccColor3B{140, 255, 140} : ccColor3B{220, 220, 220});
        rowNode->addChild(lbl, 2);

        auto rowMenu = CCMenu::create();
        rowMenu->setContentSize({areaW, optH});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setAnchorPoint({0.f, 0.f});

        auto hitSpr = paimon::SpriteHelper::createColorPanel(areaW - 4.f, optH - 2.f, {0,0,0}, 0);
        auto btn = CCMenuItemSpriteExtra::create(
            hitSpr, this,
            menu_selector(CaptureLayerEditorPopup::onFilterSelect)
        );
        btn->setTag(opts[o].idx);
        btn->setPosition({areaW * 0.5f, optH * 0.5f});
        rowMenu->addChild(btn);
        rowNode->addChild(rowMenu, 3);

        scroll->m_contentLayer->addChild(rowNode);
    }

    scroll->scrollToTop();
    m_filterDropdown->addChild(scroll, 1);
    m_mainLayer->addChild(m_filterDropdown, 10);
}

void CaptureLayerEditorPopup::onFilterSelect(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;

    m_filterGroupIndex = node->getTag();

    if (m_filterLabel) {
        std::string name;
        if (m_filterGroupIndex < 0) {
            name = tr("Filtro: Todo", "Filter: All");
        } else if (m_filterGroupIndex < static_cast<int>(m_layers.size())) {
            name = m_layers[m_filterGroupIndex].name;
        }
        m_filterLabel->setString((name + "  \xe2\x96\xbc").c_str());
    }

    closeFilterDropdown();
    buildList();
}

void CaptureLayerEditorPopup::closeFilterDropdown() {
    if (m_filterDropdown) {
        m_filterDropdown->removeFromParentAndCleanup(true);
        m_filterDropdown = nullptr;
    }
    if (m_listRoot) m_listRoot->setVisible(true);
}

void CaptureLayerEditorPopup::onDoneBtn(CCObject* sender) {
    if (!sender) return;

    auto previewRef = m_previewPopup.lock();
    this->onClose(nullptr);

    // Recapture with updated layer visibility
    if (previewRef) {
        previewRef->liveRecapture(true);
    }
}

void CaptureLayerEditorPopup::onRestoreAllBtn(CCObject* sender) {
    if (!sender) return;

    // Restore from instance copy (more reliable than static)
    for (auto& [node, vis] : m_originalVisibilities) {
        if (auto locked = node.lock()) {
            locked->setVisible(vis);
        }
    }

    for (auto& entry : m_layers) {
        entry.currentVisibility = entry.node ? entry.node->isVisible() : true;
    }

    // Reset filter to show all
    m_filterGroupIndex = -1;
    if (m_filterLabel) m_filterLabel->setString((tr("Filtro: Todo", "Filter: All") + "  \xe2\x96\xbc").c_str());
    closeFilterDropdown();

    buildList();
    updateMiniPreview();

    PaimonNotify::create(
        Localization::get().getString("layers.restored").c_str(),
        NotificationIcon::Success
    )->show();
}
