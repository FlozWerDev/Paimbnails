#include "CaptureLayerEditorPopup.hpp"
#include "../services/CaptureVisibilityState.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "CapturePreviewPopup.hpp"
#include "CaptureMiniPreview.hpp"
#include "CaptureListWidgets.hpp"
#include "CaptureUIConstants.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/ShaderLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <set>
#include <algorithm>
#include <cstring>
#include "../services/FramebufferCapture.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace cocos2d;

using paimon::capture::ui::ClippedMenu;

// Heap-allocated to avoid destruction-order problems during game shutdown.
static auto& s_originalVisibilities = *new std::vector<paimon::capture::VisibilityRecord>();

namespace {
    constexpr ccColor3B kAccent    {255, 215, 90};
    constexpr ccColor3B kTextOn    {255, 255, 255};
    constexpr ccColor3B kTextOff   {130, 130, 130};
    constexpr ccColor3B kHeaderOn  {255, 226, 120};
    constexpr ccColor3B kHeaderOff {120, 110, 80};
    constexpr ccColor3B kPartial   {255, 190, 90};

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
        return Localization::get().getLanguage() == Localization::Language::ENGLISH ? en : es;
    }

    static std::string loc(char const* key) {
        return Localization::get().getString(key);
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
            return tr("Particulas", "Particles") + " (" + cls + ")";
        }
        if (typeinfo_cast<CCMenu*>(node)) {
            return tr("Menu", "Menu") + " (" + cls + ")";
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
}

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
        paimon::capture::restoreVisibility(s_originalVisibilities);
    }
    s_originalVisibilities.clear();
    paimon::capture::clearUserShown();
    log::info("[LayerEditor] All layers restored to original visibility");
}

void CaptureLayerEditorPopup::discardTrackedLayers() {
    s_originalVisibilities.clear();
    paimon::capture::clearUserShown();
}

bool CaptureLayerEditorPopup::init() {
    namespace C = paimon::capture::layers;
    namespace E = paimon::capture::editor;

    if (!Popup::init(C::POPUP_WIDTH, C::POPUP_HEIGHT)) return false;
    this->setTitle(loc("layers.title").c_str());

    auto content = m_mainLayer->getContentSize();

    populateLayers();

    if (m_layers.empty()) {
        auto noLabel = CCLabelBMFont::create(loc("layers.no_playlayer").c_str(), "bigFont.fnt");
        noLabel->setScale(0.4f);
        noLabel->setPosition({content.width * 0.5f, content.height * 0.5f});
        m_mainLayer->addChild(noLabel);
        return true;
    }

    // Sub-branches start folded: the flat tree used to open with a hundred rows
    // of trails and particles before the first interesting layer.
    for (auto& entry : m_layers) {
        if (entry.isGroup && entry.depth >= 1) entry.collapsed = true;
    }

    const float previewTop = content.height - E::HEADER_TOP_PAD;
    const float previewCY  = previewTop - E::PREVIEW_H * 0.5f;
    const float previewCX  = E::SIDE_PAD + E::PREVIEW_W * 0.5f;

    m_miniPreview = paimon::capture::MiniPreview::create(E::PREVIEW_W, E::PREVIEW_H);
    if (m_miniPreview) {
        m_miniPreview->setPosition({previewCX, previewCY});
        if (auto preview = m_previewPopup.lock()) {
            m_miniPreview->setPlayersHidden(preview->isPlayer1Hidden(), preview->isPlayer2Hidden());
        }
        m_mainLayer->addChild(m_miniPreview, 1);
    }

    const float colX = E::SIDE_PAD + E::PREVIEW_W + E::TOOLS_GAP;

    auto toolMenu = CCMenu::create();
    toolMenu->setPosition({0.f, 0.f});
    toolMenu->setID("tool-menu"_spr);
    m_mainLayer->addChild(toolMenu, 3);

    {
        auto filterSpr = ButtonSprite::create(
            (loc("layers.filter_all")).c_str(),
            static_cast<int>(C::FILTER_BTN_WIDTH), true, "bigFont.fnt",
            "GJ_button_04.png", C::FILTER_BTN_HEIGHT, 0.3f);
        m_filterLabel = filterSpr ? filterSpr->getChildByType<CCLabelBMFont>(0) : nullptr;

        auto filterBtn = CCMenuItemSpriteExtra::create(
            filterSpr, this, menu_selector(CaptureLayerEditorPopup::onFilterBtn));
        filterBtn->setPosition({colX + C::FILTER_BTN_WIDTH * 0.5f, previewTop - 14.f});
        filterBtn->setID("filter-button"_spr);
        PaimonButtonHighlighter::registerButton(filterBtn);
        toolMenu->addChild(filterBtn);
    }

    {
        auto* spr = ButtonSprite::create(
            loc("layers.collapse_all").c_str(), 96, true, "bigFont.fnt",
            "GJ_button_04.png", 20.f, 0.3f);
        if (spr) {
            m_collapseLabel = spr->getChildByType<CCLabelBMFont>(0);
            auto* btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(CaptureLayerEditorPopup::onCollapseAllBtn));
            btn->setPosition({colX + 48.f, previewTop - 44.f});
            btn->setID("collapse-all"_spr);
            PaimonButtonHighlighter::registerButton(btn);
            toolMenu->addChild(btn);
        }
    }

    {
        auto* hint = CCLabelBMFont::create(loc("layers.hint").c_str(), "bigFont.fnt");
        hint->setScale(0.22f);
        hint->setAnchorPoint({0.f, 0.5f});
        hint->setOpacity(140);
        hint->setPosition({colX, previewTop - E::PREVIEW_H + 8.f});
        m_mainLayer->addChild(hint, 3);
    }

    buildList();

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({content.width * 0.5f, 19.f});
    btnMenu->setID("bottom-buttons"_spr);

    auto restoreSpr = ButtonSprite::create(
        loc("layers.restore_all").c_str(), 70, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.35f);
    if (restoreSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            restoreSpr, this, menu_selector(CaptureLayerEditorPopup::onRestoreAllBtn));
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    auto doneSpr = ButtonSprite::create(
        loc("layers.done").c_str(), 70, true, "bigFont.fnt", "GJ_button_02.png", 22.f, 0.35f);
    if (doneSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            doneSpr, this, menu_selector(CaptureLayerEditorPopup::onDoneBtn));
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

void CaptureLayerEditorPopup::populateLayers() {
    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* scene = CCDirector::get()->getRunningScene();

    m_layers.clear();

    bool needRecordOriginals = s_originalVisibilities.empty();
    std::set<CCNode*> addedNodes;

    auto addEntry = [&](CCNode* node, std::string const& name, bool isGroup, int depth, int parent) {
        LayerEntry entry;
        entry.node = node;
        entry.name = name;
        entry.currentVisibility = node ? node->isVisible() : true;
        entry.originalVisibility = entry.currentVisibility;
        entry.isGroup = isGroup;
        entry.depth = depth;
        entry.parentIndex = parent;

        if (!isGroup && node) {
            bool originalVisibility = node->isVisible();
            if (needRecordOriginals) {
                paimon::capture::snapshotVisibility(s_originalVisibilities, node);
            }
            (void)paimon::capture::tryGetRecordedVisibility(s_originalVisibilities, node, originalVisibility);
            entry.originalVisibility = originalVisibility;
        }

        int idx = static_cast<int>(m_layers.size());
        m_layers.push_back(std::move(entry));
        if (parent >= 0 && parent < static_cast<int>(m_layers.size())) {
            m_layers[parent].childIndices.push_back(idx);
        }

        if (!isGroup && node) {
            addedNodes.insert(node);
            paimon::capture::recordVisibility(m_originalVisibilities, node, m_layers[idx].originalVisibility);
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
            if (particlesGroup == -1) particlesGroup = addGroup(tr("Particulas", "Particles"), playerGroup, 1);
            addLeaf(node, name, particlesGroup, 2);
        };

        addTrail(player->m_regularTrail, tr("Trazo normal", "Regular trail"));
        addTrail(player->m_shipStreak, tr("Trazo ship", "Ship streak"));
        addTrail(player->m_waveTrail, tr("Trazo wave", "Wave trail"));
        addTrail(player->m_ghostTrail, tr("Trazo ghost", "Ghost trail"));

        addParticle(player->m_vehicleGroundParticles, tr("Polvo vehiculo", "Vehicle dust"));
        addParticle(player->m_robotFire, tr("Fuego robot", "Robot fire"));
        addParticle(player->m_playerGroundParticles, tr("Polvo suelo", "Ground particles"));
        addParticle(player->m_trailingParticles, tr("Particulas trail", "Trailing particles"));
        addParticle(player->m_shipClickParticles, tr("Click ship", "Ship click particles"));
        addParticle(player->m_ufoClickParticles, tr("Click UFO", "UFO click particles"));
        addParticle(player->m_robotBurstParticles, tr("Explosion robot", "Robot burst"));
        addParticle(player->m_dashParticles, tr("Particulas dash", "Dash particles"));
        addParticle(player->m_swingBurstParticles1, tr("Swing burst 1", "Swing burst 1"));
        addParticle(player->m_swingBurstParticles2, tr("Swing burst 2", "Swing burst 2"));
        addParticle(player->m_landParticles0, tr("Aterrizaje 0", "Landing particles 0"));
        addParticle(player->m_landParticles1, tr("Aterrizaje 1", "Landing particles 1"));
        addParticle(player->m_dashFireSprite, tr("Fuego dash", "Dash fire"));

        int extrasGroup = -1;
        std::set<CCNode*> skipped = {
            player,
            player->m_regularTrail, player->m_shipStreak, player->m_waveTrail, player->m_ghostTrail,
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
// Keep the other player out of the current player's tree; dual mode can nest
// either player under the other and would otherwise toggle both at once.
                if (auto* otherPlayer = typeinfo_cast<PlayerObject*>(nd)) {
                    if (otherPlayer != player) continue;
                }
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

    addPlayerGroup(pl->m_player1, loc("layers.player1"));
    addPlayerGroup(pl->m_player2, loc("layers.player2"));

    int sceneGroup = -1;
    auto ensureSceneGroup = [&]() {
        if (sceneGroup == -1) sceneGroup = addGroup(tr("Escenario", "Scenery"), -1, 0);
        return sceneGroup;
    };

    addLeaf(pl->m_background,   tr("Fondo (BG)", "Background (BG)"),     ensureSceneGroup(), 1);
    addLeaf(pl->m_middleground, tr("Medio fondo (MG)", "Middleground (MG)"), ensureSceneGroup(), 1);
    addLeaf(pl->m_groundLayer,  tr("Suelo 1", "Ground 1"),               ensureSceneGroup(), 1);
    addLeaf(pl->m_groundLayer2, tr("Suelo 2", "Ground 2"),               ensureSceneGroup(), 1);

    int objectGroup = -1;
    auto ensureObjectGroup = [&]() {
        if (objectGroup == -1) objectGroup = addGroup(tr("Capas de objetos", "Object layers"), -1, 0);
        return objectGroup;
    };

    addLeaf(pl->m_objectLayer,           tr("Objetos principal", "Main objects"),        ensureObjectGroup(), 1);
    addLeaf(pl->m_inShaderObjectLayer,    tr("Objetos in-shader", "In-shader objects"),   ensureObjectGroup(), 1);
    addLeaf(pl->m_aboveShaderObjectLayer, tr("Objetos sobre-shader", "Above-shader objects"), ensureObjectGroup(), 1);

    if (pl->m_batchNodes) {
        int batchGroup = -1;
        for (unsigned int bi = 0; bi < pl->m_batchNodes->count(); ++bi) {
            auto* bn = typeinfo_cast<CCNode*>(pl->m_batchNodes->objectAtIndex(bi));
            if (!bn || addedNodes.count(bn)) continue;
            if (batchGroup == -1) batchGroup = addGroup(tr("Batch nodes", "Batch nodes"), ensureObjectGroup(), 1);
            std::string batchName = describeNode(bn);
            if (batchName.empty()) batchName = "Batch #" + std::to_string(bi);
            addLeaf(bn, batchName, batchGroup, 2);
        }
    }

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

    int effectsGroup = -1;
    auto ensureEffectsGroup = [&]() {
        if (effectsGroup == -1) effectsGroup = addGroup(loc("layers.effects"), -1, 0);
        return effectsGroup;
    };

    addLeaf(pl->m_shaderLayer, tr("Shader layer", "Shader layer"), ensureEffectsGroup(), 1);

    int modGroup = -1;
    auto ensureModGroup = [&]() {
        if (modGroup == -1) modGroup = addGroup(tr("Mods / Overlays", "Mods / Overlays"), -1, 0);
        return modGroup;
    };

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

            if (nid.find('/') != std::string::npos) {
                addLeaf(obj, nid.empty() ? describeNode(obj) : nid, ensureModGroup(), 1);
                continue;
            }

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

    int overlayGroup = -1;
    auto ensureOverlayGroup = [&]() {
        if (overlayGroup == -1) overlayGroup = addGroup(tr("Overlays de escena", "Scene overlays"), -1, 0);
        return overlayGroup;
    };

    if (scene) {
        for (auto* obj : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!obj || obj == pl || addedNodes.count(obj)) continue;
            if (typeinfo_cast<FLAlertLayer*>(obj)) continue;
            std::string cls = typeid(*obj).name();
            if (cls.find("PauseLayer") != std::string::npos) continue;

            std::string nid = obj->getID();
            auto size = obj->getContentSize();
            bool hasVisualSize = size.width >= 0.1f || size.height >= 0.1f;
            bool hasChildren = obj->getChildren() && obj->getChildren()->count() > 0;
            if (!hasVisualSize && !hasChildren) continue;

            if (nid.find('/') != std::string::npos) {
                addLeaf(obj, nid, ensureModGroup(), 1);
            } else {
                addLeaf(obj, describeNode(obj), ensureOverlayGroup(), 1);
            }
        }
    }

    log::info("[LayerEditor] Enumerated {} layer entries", m_layers.size());
}

bool CaptureLayerEditorPopup::isEntryVisible(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return false;
    auto const& entry = m_layers[idx];

// A group is visible only when all children are visible; its checkbox controls all.
    if (!entry.childIndices.empty()) {
        for (int child : entry.childIndices) {
            if (!isEntryVisible(child)) return false;
        }
        return true;
    }

    return entry.node ? entry.node->isVisible() : entry.currentVisibility;
}

std::pair<int, int> CaptureLayerEditorPopup::visibleLeafCount(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return {0, 0};
    auto const& entry = m_layers[idx];

    if (entry.childIndices.empty()) {
        bool vis = entry.node ? entry.node->isVisible() : entry.currentVisibility;
        return {vis ? 1 : 0, 1};
    }

    int visible = 0, total = 0;
    for (int child : entry.childIndices) {
        auto [v, t] = visibleLeafCount(child);
        visible += v;
        total   += t;
    }
    return {visible, total};
}

bool CaptureLayerEditorPopup::isEntryHiddenByCollapse(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return false;
    int parent = m_layers[idx].parentIndex;
    while (parent >= 0) {
        if (m_layers[parent].collapsed) return true;
        parent = m_layers[parent].parentIndex;
    }
    return false;
}

void CaptureLayerEditorPopup::setEntryVisible(int idx, bool visible, bool cascadeChildren) {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    auto& entry = m_layers[idx];

    if (entry.isGroup) {
        entry.currentVisibility = visible;
        if (cascadeChildren) {
            for (int child : entry.childIndices) {
                setEntryVisible(child, visible, true);
            }
        }
    } else {
        entry.currentVisibility = visible;
        if (entry.node) {
            entry.node->setVisible(visible);
// Preserve explicit choices so capture's hide pass cannot override them.
            paimon::capture::setUserShown(entry.node, visible);
        }
        if (cascadeChildren) {
            for (int child : entry.childIndices) {
                setEntryVisible(child, visible, true);
            }
        }
    }
}

void CaptureLayerEditorPopup::refreshRowVisuals(int idx) {
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    auto& entry = m_layers[idx];

    bool vis = isEntryVisible(idx);

    if (entry.toggler) {
        bool desired = vis;
        if (entry.isGroup) {
            // Half-lit groups stay checked but amber, so folding one away does
            // not read as "everything under here is hidden".
            auto [visibleLeaves, totalLeaves] = visibleLeafCount(idx);
            bool const partial = visibleLeaves > 0 && visibleLeaves < totalLeaves;
            desired = vis || partial;
            if (auto* onButton = entry.toggler->m_onButton) {
                if (auto* spr = typeinfo_cast<CCSprite*>(onButton->getNormalImage())) {
                    spr->setColor(partial ? kPartial : kTextOn);
                }
            }
        }
        if (entry.toggler->isToggled() != desired) entry.toggler->toggle(desired);
    }

    if (entry.countLabel) {
        auto [visibleLeaves, totalLeaves] = visibleLeafCount(idx);
        entry.countLabel->setString(
            (std::to_string(visibleLeaves) + "/" + std::to_string(totalLeaves)).c_str());
        entry.countLabel->setColor(visibleLeaves == 0 ? kHeaderOff : ccColor3B{180, 220, 255});
    }

    if (entry.label) {
        if (entry.isGroup) {
            auto [visibleLeaves, _] = visibleLeafCount(idx);
            entry.label->setColor(visibleLeaves > 0 ? kHeaderOn : kHeaderOff);
        } else {
            entry.label->setColor(vis ? kTextOn : kTextOff);
        }
    }
}

void CaptureLayerEditorPopup::refreshAncestors(int idx) {
    int parentIdx = (idx >= 0 && idx < static_cast<int>(m_layers.size()))
        ? m_layers[idx].parentIndex : -1;
    while (parentIdx >= 0) {
        refreshRowVisuals(parentIdx);
        parentIdx = m_layers[parentIdx].parentIndex;
    }
}

bool CaptureLayerEditorPopup::entryMatchesFilter(int idx) const {
    if (m_filterGroupIndex < 0) return true;
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return false;
    if (idx == m_filterGroupIndex) return true;
    int parent = m_layers[idx].parentIndex;
    while (parent >= 0) {
        if (parent == m_filterGroupIndex) return true;
        parent = m_layers[parent].parentIndex;
    }
    return false;
}

void CaptureLayerEditorPopup::buildList() {
    namespace C = paimon::capture::layers;
    namespace E = paimon::capture::editor;

    float savedScroll = 0.f;
    bool hadScroll = false;
    if (m_scrollView && m_scrollView->m_contentLayer) {
        savedScroll = m_scrollView->m_contentLayer->getPositionY();
        hadScroll = true;
    }

    if (m_listRoot) {
        m_listRoot->removeFromParentAndCleanup(true);
        m_listRoot = nullptr;
        m_scrollView = nullptr;
    }

    for (auto& entry : m_layers) {
        entry.toggler = nullptr;
        entry.label = nullptr;
        entry.countLabel = nullptr;
    }

    auto content = m_mainLayer->getContentSize();

    const float listW   = content.width - E::SIDE_PAD * 2;
    const float rowH    = C::ROW_HEIGHT;
    const float listTop = content.height - E::HEADER_TOP_PAD - E::PREVIEW_H - E::LIST_GAP_BELOW_HEADER;
    const float listBot = E::LIST_BOT;
    const float viewH   = listTop - listBot;
    const float viewX   = E::SIDE_PAD;

    std::vector<int> visibleIndices;
    for (int i = 0; i < static_cast<int>(m_layers.size()); ++i) {
        if (!entryMatchesFilter(i)) continue;
        if (isEntryHiddenByCollapse(i)) continue;
        visibleIndices.push_back(i);
    }
    int numVisible = static_cast<int>(visibleIndices.size());

    m_listRoot = CCNode::create();
    m_listRoot->setID("list-root"_spr);
    m_mainLayer->addChild(m_listRoot, 2);

    auto panel = paimon::SpriteHelper::createDarkPanel(listW, viewH, 80);
    panel->setPosition({viewX, listBot});
    m_listRoot->addChild(panel, 0);

    float totalH = std::max(viewH, numVisible * rowH);

    m_scrollView = ScrollLayer::create({listW, viewH});
    m_scrollView->setPosition({viewX, listBot});
    m_scrollView->m_contentLayer->setContentSize({listW, totalH});

    for (int row = 0; row < numVisible; ++row) {
        int i = visibleIndices[row];
        auto& entry = m_layers[i];
        float y = totalH - rowH - row * rowH;

        auto rowNode = CCNode::create();
        rowNode->setContentSize({listW, rowH});
        rowNode->setPosition({0.f, y});
        rowNode->setAnchorPoint({0.f, 0.f});

        auto rowMenu = ClippedMenu::create(m_scrollView);
        rowMenu->setContentSize({listW, rowH});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setAnchorPoint({0.f, 0.f});

        float const indent = entry.depth * C::DEPTH_INDENT;

        if (entry.isGroup) {
            if (auto* bg = paimon::capture::ui::makeRowFill(
                    listW, rowH, {kAccent.r, kAccent.g, kAccent.b, E::GROUP_BG_ALPHA})) {
                rowNode->addChild(bg, -2);
            }
            if (auto* accent = paimon::capture::ui::makeRowFill(
                    E::GROUP_ACCENT_WIDTH, rowH - 4.f,
                    {kAccent.r, kAccent.g, kAccent.b, E::GROUP_ACCENT_ALPHA})) {
                accent->setPosition({3.f + indent, 2.f});
                rowNode->addChild(accent, -1);
            }
            if (auto* arrow = paimon::capture::ui::makeDisclosure(!entry.collapsed, E::ARROW_SCALE)) {
                arrow->setPosition({E::ARROW_X + indent, rowH * 0.5f});
                rowNode->addChild(arrow, 2);
            }
            if (auto* hit = paimon::capture::ui::makeRowHitArea(
                    listW - 34.f, rowH, this,
                    menu_selector(CaptureLayerEditorPopup::onToggleCollapse), i)) {
                hit->setPosition({(listW - 34.f) * 0.5f, rowH * 0.5f});
                rowMenu->addChild(hit);
            }
        } else if (row % 2 == 0) {
            if (auto* bg = paimon::capture::ui::makeRowFill(
                    listW, rowH, {255, 255, 255, E::ALT_ROW_ALPHA})) {
                rowNode->addChild(bg, -1);
            }
        }

        auto label = CCLabelBMFont::create(entry.name.c_str(), "bigFont.fnt");
        float labelScale = entry.isGroup ? C::LABEL_SCALE_GROUP
                         : (entry.depth >= 2 ? C::LABEL_SCALE_LEAF_D2 : C::LABEL_SCALE_LEAF_D0);
        float const labelX = C::LABEL_X_BASE + indent;
        // Mod node ids and shader layer names are long enough to slide under
        // the counter and the checkbox.
        label->limitLabelWidth(
            listW - C::CHECK_X_FROM_RIGHT - (entry.isGroup ? 44.f : 22.f) - labelX,
            labelScale, 0.16f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({labelX, rowH * 0.5f});
        rowNode->addChild(label, 2);
        entry.label = label;

        if (entry.isGroup) {
            auto* countLabel = CCLabelBMFont::create("", "bigFont.fnt");
            countLabel->setScale(C::COUNT_SCALE);
            countLabel->setAnchorPoint({1.f, 0.5f});
            countLabel->setPosition({listW - C::CHECK_X_FROM_RIGHT - 14.f, rowH * 0.5f});
            rowNode->addChild(countLabel, 2);
            entry.countLabel = countLabel;
        }

        float checkScale = entry.isGroup ? C::CHECK_SCALE_GROUP : C::CHECK_SCALE_LEAF;
        if (auto* toggler = paimon::capture::ui::makeCheck(
                checkScale, this, menu_selector(CaptureLayerEditorPopup::onToggleLayer),
                i, isEntryVisible(i))) {
            toggler->setPosition({listW - C::CHECK_X_FROM_RIGHT, rowH * 0.5f});
            rowMenu->addChild(toggler);
            entry.toggler = toggler;
        }

        rowNode->addChild(rowMenu, 3);
        refreshRowVisuals(i);

        m_scrollView->m_contentLayer->addChild(rowNode);
    }

    m_scrollView->scrollToTop();
    if (hadScroll && totalH > viewH) {
        float minY = viewH - totalH;
        m_scrollView->m_contentLayer->setPositionY(std::clamp(savedScroll, minY, 0.f));
    }
    m_listRoot->addChild(m_scrollView, 2);
}

void CaptureLayerEditorPopup::refreshPreview() {
    if (m_miniPreview) m_miniPreview->requestRefresh();
}

void CaptureLayerEditorPopup::rebuildListDeferred() {
    // Rebuilding destroys the menu the touch dispatcher is still unwinding.
    Ref<CaptureLayerEditorPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!self || !self->getParent()) return;
        self->buildList();
    });
}

void CaptureLayerEditorPopup::onToggleLayer(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int idx = toggler->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;

    bool newVisible = toggler->isToggled();
    bool cascade = m_layers[idx].isGroup;
    setEntryVisible(idx, newVisible, cascade);

    log::info("[LayerEditor] '{}' -> {}", m_layers[idx].name,
        newVisible ? "visible" : "hidden");

// Refresh this entry and related groups in place; avoid rebuilding the list.
    refreshRowVisuals(idx);
    refreshAncestors(idx);

    for (int child : m_layers[idx].childIndices) {
        refreshRowVisuals(child);
        for (int grandchild : m_layers[child].childIndices) {
            refreshRowVisuals(grandchild);
        }
    }

    refreshPreview();
}

void CaptureLayerEditorPopup::onToggleCollapse(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;

    int idx = node->getTag();
    if (idx < 0 || idx >= static_cast<int>(m_layers.size())) return;
    if (!m_layers[idx].isGroup) return;

    m_layers[idx].collapsed = !m_layers[idx].collapsed;
    rebuildListDeferred();
}

void CaptureLayerEditorPopup::onCollapseAllBtn(CCObject*) {
    m_allCollapsed = !m_allCollapsed;
    for (auto& entry : m_layers) {
        if (entry.isGroup) entry.collapsed = m_allCollapsed;
    }
    if (m_collapseLabel) {
        m_collapseLabel->setString(
            loc(m_allCollapsed ? "layers.expand_all" : "layers.collapse_all").c_str());
    }
    rebuildListDeferred();
}

void CaptureLayerEditorPopup::onFilterBtn(CCObject*) {
    namespace C = paimon::capture::layers;
    namespace E = paimon::capture::editor;

    if (m_filterDropdown) {
        closeFilterDropdown();
        return;
    }

    if (m_listRoot) m_listRoot->setVisible(false);

    auto content = m_mainLayer->getContentSize();
    const float areaTop = content.height - E::HEADER_TOP_PAD - E::PREVIEW_H - E::LIST_GAP_BELOW_HEADER;
    const float areaBot = E::LIST_BOT;
    const float areaH   = areaTop - areaBot;
    const float areaW   = content.width - E::SIDE_PAD * 2;
    const float areaX   = E::SIDE_PAD;

    struct FilterOption { int idx; std::string name; };
    std::vector<FilterOption> opts;
    opts.push_back({-1, loc("layers.filter_all")});
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
            if (auto* bg = paimon::capture::ui::makeRowFill(areaW, optH, {50, 150, 60, 90})) {
                rowNode->addChild(bg, -1);
            }
            if (auto* accent = paimon::capture::ui::makeRowFill(3.f, optH - 4.f, {80, 220, 90, 200})) {
                accent->setPosition({2.f, 2.f});
                rowNode->addChild(accent, 0);
            }
        } else if (o % 2 == 0) {
            if (auto* bg = paimon::capture::ui::makeRowFill(areaW, optH, {255, 255, 255, 8})) {
                rowNode->addChild(bg, -1);
            }
        }

        auto lbl = CCLabelBMFont::create(opts[o].name.c_str(), "bigFont.fnt");
        lbl->setScale(0.3f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({active ? 10.f : 8.f, optH * 0.5f});
        lbl->setColor(active ? ccColor3B{140, 255, 140} : ccColor3B{220, 220, 220});
        rowNode->addChild(lbl, 2);

        auto rowMenu = ClippedMenu::create(scroll);
        rowMenu->setContentSize({areaW, optH});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setAnchorPoint({0.f, 0.f});

        if (auto* btn = paimon::capture::ui::makeRowHitArea(
                areaW - 4.f, optH - 2.f, this,
                menu_selector(CaptureLayerEditorPopup::onFilterSelect), opts[o].idx)) {
            btn->setPosition({areaW * 0.5f, optH * 0.5f});
            rowMenu->addChild(btn);
        }
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
            name = loc("layers.filter_all");
        } else if (m_filterGroupIndex < static_cast<int>(m_layers.size())) {
            name = m_layers[m_filterGroupIndex].name;
        }
        m_filterLabel->setString(name.c_str());
    }

    // Defer: destroying the filter dropdown inline kills the CCMenu while
    // the touch dispatcher is still unwinding the activate.
    Ref<CaptureLayerEditorPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!self || !self->getParent()) return;
        self->closeFilterDropdown();
        self->buildList();
    });
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

    if (previewRef) {
        previewRef->liveRecapture(true);
    }
}

void CaptureLayerEditorPopup::onRestoreAllBtn(CCObject* sender) {
    if (!sender) return;

    paimon::capture::restoreVisibility(m_originalVisibilities);
    paimon::capture::clearUserShown();

    for (auto& entry : m_layers) {
        entry.currentVisibility = entry.isGroup ? true : entry.originalVisibility;
    }

    m_filterGroupIndex = -1;
    if (m_filterLabel) m_filterLabel->setString(loc("layers.filter_all").c_str());
    closeFilterDropdown();

    buildList();
    refreshPreview();

    PaimonNotify::create(loc("layers.restored").c_str(), NotificationIcon::Success)->show();
}
