#include "ButtonSkinRuntime.hpp"

#include "ButtonSkinStore.hpp"
#include "../../quick-hub/services/QuickHubButtonCapture.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/Debug.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace paimon::buttonskin {

namespace {

constexpr float kScanInterval = 0.3f;
// one scored search every this many light scans (~1.5 s).
constexpr int kLightScansPerSweep = 5;
// past this many buttons (editor palettes) light scans only tend the dressed ones.
constexpr size_t kWalkLimit = 400;
constexpr size_t kMaxShapes = 48;
constexpr ccColor3B kWhite{255, 255, 255};

char const* stateKey() { return "button-skin-state"_spr; }
char const* uiFlag() { return "button-skin-ui"_spr; }
char const* signatureKey() { return "button-skin-signature"_spr; }

ccColor3B multiply(ccColor3B a, ccColor3B b) {
    return {static_cast<GLubyte>(a.r * b.r / 255), static_cast<GLubyte>(a.g * b.g / 255),
            static_cast<GLubyte>(a.b * b.b / 255)};
}

GLubyte scaleOpacity(GLubyte value, float mul) {
    return static_cast<GLubyte>(std::lround(std::clamp(mul, 0.f, 1.f) * static_cast<float>(value)));
}

// the sprite items that draw a button: a toggle draws one of its two inner buttons.
std::vector<CCMenuItemSprite*> visualsOf(CCMenuItem* item) {
    std::vector<CCMenuItemSprite*> out;
    if (auto* toggler = typeinfo_cast<CCMenuItemToggler*>(item)) {
        for (auto* state : {toggler->m_offButton, toggler->m_onButton}) {
            if (state) out.push_back(state);
        }
    } else if (auto* sprite = typeinfo_cast<CCMenuItemSprite*>(item)) {
        out.push_back(sprite);
    }
    return out;
}

// cheap fingerprint of an asset: catches new frames and rebuilt text buttons on the same node.
struct AssetPrint {
    CCNode* node = nullptr;  // compared only
    CCTexture2D* texture = nullptr;
    CCRect rect;
    CCSize size;
    unsigned int children = 0;

    bool operator==(AssetPrint const& other) const {
        return node == other.node && texture == other.texture && rect.equals(other.rect) &&
               size.equals(other.size) && children == other.children;
    }
};

AssetPrint printOf(CCNode* node) {
    AssetPrint print;
    if (!node) return print;
    print.node = node;
    print.size = node->getContentSize();
    print.children = node->getChildrenCount();
    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        print.texture = sprite->getTexture();
        print.rect = sprite->getTextureRect();
    }
    return print;
}

std::vector<AssetPrint> printsOf(CCMenuItem* item) {
    std::vector<AssetPrint> prints;
    for (auto* visual : visualsOf(item)) prints.push_back(printOf(visual->getNormalImage()));
    return prints;
}

// paint copied over the asset; follows its transform every frame so animations never leave it behind.
class SkinOverlay : public CCSprite {
public:
    static SkinOverlay* create(CCTexture2D* texture, CCNode* follow, CCSize assetSize) {
        auto* ret = new SkinOverlay();
        if (!ret->initWithTexture(texture)) {
            delete ret;
            return nullptr;
        }
        ret->autorelease();
        ret->setFlipY(true);
        ret->setBlendFunc({GL_ONE, GL_ONE_MINUS_SRC_ALPHA});
        ret->setOpacityModifyRGB(true);
        auto size = ret->getContentSize();
        ret->m_fitX = size.width > 0.f ? assetSize.width / size.width : 1.f;
        ret->m_fitY = size.height > 0.f ? assetSize.height / size.height : 1.f;
        ret->m_follow = follow;
        ret->follow();
        return ret;
    }

    void visit() override {
        auto* node = m_follow.data();
        // the item swapped to its selected/disabled image: that one has no paint.
        if (node && !node->isVisible()) return;
        follow();
        CCSprite::visit();
    }

private:
    void follow() {
        auto* node = m_follow.data();
        if (!node) return;
        setAnchorPoint(node->getAnchorPoint());
        setPosition(node->getPosition());
        setScaleX(node->getScaleX() * m_fitX);
        setScaleY(node->getScaleY() * m_fitY);
        setRotationX(node->getRotationX());
        setRotationY(node->getRotationY());
        setSkewX(node->getSkewX());
        setSkewY(node->getSkewY());
    }

    Ref<CCNode> m_follow;
    float m_fitX = 1.f;
    float m_fitY = 1.f;
};

struct PaintTarget {
    Ref<CCNode> node;
    ccColor3B color{};
    ccColor3B appliedColor{};
    GLubyte opacity = 255;
    GLubyte appliedOpacity = 255;
};

struct ScaledNode {
    Ref<CCNode> node;
    float scaleX = 1.f, scaleY = 1.f;
    CCPoint position;
    float appliedScaleX = 1.f, appliedScaleY = 1.f;
    CCPoint appliedPosition;
};

// everything the skin changed, so a restore puts back exactly the game's values.
class SkinState : public CCObject {
public:
    static SkinState* create() {
        auto* ret = new SkinState();
        ret->autorelease();
        return ret;
    }

    std::string skinId;
    size_t hash = 0;
    uint64_t generation = 0;
    // held so the raw pointers in prints can't be reused by a new node.
    std::vector<Ref<CCNode>> normals;
    std::vector<AssetPrint> prints;
    std::vector<Ref<CCNode>> overlays;

    ccColor3B tint = kWhite;
    float opacityMul = 1.f;
    std::vector<PaintTarget> colors;
    std::vector<PaintTarget> opacities;

    float scale = 1.f;
    std::vector<ScaledNode> images;
    bool sized = false;
    CCSize itemSize, appliedItemSize;
    // toggles grow as a whole: their inner buttons animate their own scale.
    bool scaledItem = false;
    float itemScaleX = 1.f, itemScaleY = 1.f;
    float appliedItemScaleX = 1.f, appliedItemScaleY = 1.f;

    CCPoint offset;
    bool moved = false;
    CCPoint itemPosition, appliedItemPosition;

    float rotation = 0.f;
    bool turned = false;
    float itemRotation = 0.f, appliedItemRotation = 0.f;

    bool hasSound = false;
    SkinSound sound;
};

SkinState* stateOf(CCMenuItem* item) {
    return item ? typeinfo_cast<SkinState*>(item->getUserObject(stateKey())) : nullptr;
}

std::vector<CCNode*> assetImages(CCMenuItemSprite* item) {
    std::vector<CCNode*> images;
    if (!item) return images;
    for (auto* node : {item->getNormalImage(), item->getSelectedImage(), item->getDisabledImage()}) {
        if (node && std::ranges::find(images, node) == images.end()) images.push_back(node);
    }
    return images;
}

// a cascading parent already reaches its children: painting both would apply it twice.
void collectTargets(CCNode* node, bool color, std::vector<PaintTarget>& out) {
    if (!node) return;
    if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
        PaintTarget target;
        target.node = node;
        target.color = rgba->getColor();
        target.opacity = rgba->getOpacity();
        out.push_back(std::move(target));
        if (color ? rgba->isCascadeColorEnabled() : rgba->isCascadeOpacityEnabled()) return;
    }
    if (typeinfo_cast<CCLabelBMFont*>(node)) return;
    auto* children = node->getChildren();
    if (!children) return;
    for (unsigned int i = 0; i < children->count(); ++i) {
        collectTargets(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), color, out);
    }
}

void paintColor(PaintTarget& target, ccColor3B tint) {
    auto* rgba = typeinfo_cast<CCRGBAProtocol*>(target.node.data());
    if (!rgba) return;
    target.appliedColor = multiply(target.color, tint);
    rgba->setColor(target.appliedColor);
}

void paintOpacity(PaintTarget& target, float mul) {
    auto* rgba = typeinfo_cast<CCRGBAProtocol*>(target.node.data());
    if (!rgba) return;
    target.appliedOpacity = scaleOpacity(target.opacity, mul);
    rgba->setOpacity(target.appliedOpacity);
}

void scaleNode(CCNode* node, float k, std::vector<ScaledNode>& out) {
    ScaledNode scaled;
    scaled.node = node;
    scaled.scaleX = node->getScaleX();
    scaled.scaleY = node->getScaleY();
    scaled.position = node->getPosition();
    scaled.appliedScaleX = scaled.scaleX * k;
    scaled.appliedScaleY = scaled.scaleY * k;
    scaled.appliedPosition = scaled.position * k;
    node->setScaleX(scaled.appliedScaleX);
    node->setScaleY(scaled.appliedScaleY);
    node->setPosition(scaled.appliedPosition);
    out.push_back(std::move(scaled));
}

void restoreState(CCMenuItem* item, SkinState* state) {
    if (!item || !state) return;
    for (auto& overlay : state->overlays) {
        if (auto* node = overlay.data()) node->removeFromParent();
    }

    // only values still ours go back; whatever the game rewrote since then stays.
    for (auto& target : state->colors) {
        auto* rgba = typeinfo_cast<CCRGBAProtocol*>(target.node.data());
        if (rgba && sameColor(rgba->getColor(), target.appliedColor)) rgba->setColor(target.color);
    }
    for (auto& target : state->opacities) {
        auto* rgba = typeinfo_cast<CCRGBAProtocol*>(target.node.data());
        if (rgba && rgba->getOpacity() == target.appliedOpacity) rgba->setOpacity(target.opacity);
    }
    for (auto& image : state->images) {
        auto* node = image.node.data();
        if (!node) continue;
        if (node->getScaleX() == image.appliedScaleX) node->setScaleX(image.scaleX);
        if (node->getScaleY() == image.appliedScaleY) node->setScaleY(image.scaleY);
        if (node->getPosition().equals(image.appliedPosition)) node->setPosition(image.position);
    }
    if (state->sized && item->getContentSize().equals(state->appliedItemSize)) {
        item->setContentSize(state->itemSize);
    }
    if (state->scaledItem) {
        if (item->getScaleX() == state->appliedItemScaleX) item->setScaleX(state->itemScaleX);
        if (item->getScaleY() == state->appliedItemScaleY) item->setScaleY(state->itemScaleY);
    }
    if (state->moved && item->getPosition().equals(state->appliedItemPosition)) {
        item->setPosition(state->itemPosition);
    }
    if (state->turned && item->getRotation() == state->appliedItemRotation) {
        item->setRotation(state->itemRotation);
    }
    item->setUserObject(stateKey(), nullptr);
}

// keeps a live skin true to its settings; false asks for restore + fresh apply.
bool maintainState(CCMenuItem* item, SkinState* state) {
    // asset swapped or redrawn (toggles, likes, new captions): its paint
    // belongs to the old one.
    if (printsOf(item) != state->prints) return false;
    for (auto& overlay : state->overlays) {
        if (overlay && !overlay->getParent()) return false;
    }

    // the game rewrote a color or opacity we own (disabled look): collect the
    // originals again in one pass, so nine-slices and their slices don't end
    // up tinted twice. running tint/fade actions rewrite them every frame, so
    // those wait until the action ends.
    for (auto const* list : {&state->colors, &state->opacities}) {
        bool const color = list == &state->colors;
        for (auto const& target : *list) {
            auto* node = target.node.data();
            auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node);
            if (!rgba || node->numberOfRunningActions() > 0) continue;
            bool const drifted = color ? !sameColor(rgba->getColor(), target.appliedColor)
                                       : rgba->getOpacity() != target.appliedOpacity;
            if (drifted) return false;
        }
    }

    // layouts move buttons around; running actions own the transform until they end.
    bool const animating = item->numberOfRunningActions() > 0;
    if (state->moved && !animating && !item->getPosition().equals(state->appliedItemPosition)) {
        state->itemPosition = item->getPosition();
        state->appliedItemPosition = state->itemPosition + state->offset;
        item->setPosition(state->appliedItemPosition);
    }
    if (state->turned && !animating && item->getRotation() != state->appliedItemRotation) {
        state->itemRotation = item->getRotation();
        state->appliedItemRotation = state->itemRotation + state->rotation;
        item->setRotation(state->appliedItemRotation);
    }
    if (state->scaledItem && !animating &&
        (item->getScaleX() != state->appliedItemScaleX || item->getScaleY() != state->appliedItemScaleY)) {
        state->itemScaleX = item->getScaleX();
        state->itemScaleY = item->getScaleY();
        state->appliedItemScaleX = state->itemScaleX * state->scale;
        state->appliedItemScaleY = state->itemScaleY * state->scale;
        item->setScaleX(state->appliedItemScaleX);
        item->setScaleY(state->appliedItemScaleY);
    }
    return true;
}

// buttons only live in menus and never hold other buttons (a toggle's states
// belong to it): label glyphs, sprite batches, level objects, gameplay layers,
// editor build palettes, particles and button insides are skipped whole.
void collectItems(CCNode* node, std::vector<CCMenuItem*>& out) {
    if (!node || typeinfo_cast<CCLabelBMFont*>(node) || typeinfo_cast<CCSpriteBatchNode*>(node) ||
        typeinfo_cast<GameObject*>(node) || typeinfo_cast<CCParticleSystem*>(node) ||
        typeinfo_cast<GJBaseGameLayer*>(node) || typeinfo_cast<EditButtonBar*>(node)) {
        return;
    }
    if (auto* item = typeinfo_cast<CCMenuItem*>(node)) {
        out.push_back(item);
        return;
    }
    auto* children = node->getChildren();
    if (!children) return;
    for (unsigned int i = 0; i < children->count(); ++i) {
        collectItems(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), out);
    }
}

// gameplay and the level editor own the frame budget; popups opened over them still get scanned.
bool gameplayRunning() {
    if (auto* play = PlayLayer::get(); play && !play->m_isPaused) return true;
    if (LevelEditorLayer::get() != nullptr) return true;
    return false;
}

std::string shapeKey(CCNode* node) {
    auto* sprite = typeinfo_cast<CCSprite*>(node);
    if (!sprite || sprite->getChildrenCount() > 0 || !sprite->getTexture()) return {};
    auto rect = sprite->getTextureRect();
    auto size = sprite->getContentSize();
    return fmt::format("{}|{:.2f},{:.2f},{:.2f},{:.2f}|{}{}{}|{:.2f}x{:.2f}",
        static_cast<void*>(sprite->getTexture()), rect.origin.x, rect.origin.y,
        rect.size.width, rect.size.height, sprite->isTextureRectRotated(),
        sprite->isFlipX(), sprite->isFlipY(), size.width, size.height);
}

// outermost screen layer above a node, memoized per node: items of one menu share the walk.
class OwnerMemo {
public:
    std::string const& of(CCNode* node) {
        static std::string const kNone;
        if (!node) return kNone;
        if (auto found = m_cache.find(node); found != m_cache.end()) return found->second;
        std::string result = of(node->getParent());
        if (result.empty()) result = quickhub::screenLayerClassOf(node);
        // node-based map: the reference survives later inserts.
        return m_cache.emplace(node, std::move(result)).first->second;
    }

private:
    std::unordered_map<CCNode*, std::string> m_cache;
};

// signature cached on the item until its asset changes; scans revisit the same buttons.
class SignatureMemo : public CCObject {
public:
    std::vector<AssetPrint> prints;
    std::string signature;
};

std::string const& signatureOf(CCMenuItem* item, std::optional<std::string>& cached) {
    if (cached) return *cached;
    auto prints = printsOf(item);
    auto* memo = typeinfo_cast<SignatureMemo*>(item->getUserObject(signatureKey()));
    // text buttons read their caption, which can change without touching the asset.
    bool const textOnly = memo && memo->signature.starts_with("label:");
    if (!memo || memo->prints != prints || textOnly) {
        if (!memo) {
            memo = new SignatureMemo();
            memo->autorelease();
            item->setUserObject(signatureKey(), memo);
        }
        memo->prints = std::move(prints);
        memo->signature = quickhub::buttonVisualSignature(item);
    }
    cached = memo->signature;
    return *cached;
}

bool matchesWideScope(ButtonSkin const& skin, CCMenuItem* item, std::optional<std::string>& signature,
                      OwnerMemo& owners) {
    if (skin.scope == SkinScope::SameScreen) {
        auto wanted = sameScreenClass(skin.target);
        if (wanted.empty() || owners.of(item) != wanted) return false;
        if (!skin.target.targetNodeId.empty() && item->getID() == skin.target.targetNodeId) return true;
        return !skin.signature.empty() && signatureOf(item, signature) == skin.signature;
    }
    return !skin.signature.empty() && signatureOf(item, signature) == skin.signature;
}

ButtonSkin const* findSaved(std::string const& id) {
    for (auto const& skin : ButtonSkinStore::get().all()) {
        if (skin.id == id) return &skin;
    }
    return nullptr;
}

class SkinTicker : public CCNode {
public:
    static SkinTicker* shared() {
        // lives for the session, like the quick hub's pending activation.
        static SkinTicker* s_instance = [] {
            auto* ticker = new SkinTicker();
            ticker->init();
            return ticker;
        }();
        return s_instance;
    }

    void start() {
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) return;
        scheduler->scheduleSelector(schedule_selector(SkinTicker::onTick), this, 0.f, false);
    }

    void stop() {
        auto* director = CCDirector::get();
        auto* scheduler = director ? director->getScheduler() : nullptr;
        if (!scheduler) return;
        scheduler->unscheduleSelector(schedule_selector(SkinTicker::onTick), this);
    }

private:
    void onTick(float dt) {
        ButtonSkinRuntime::get().tick(dt);
    }
};

std::atomic<bool> s_buttonSkinRuntimeInitialized{false};

} // namespace

std::string sameScreenClass(quickhub::CustomQuickButton const& target) {
    auto owner = quickhub::usableOwnerClassOf(target);
    return owner.empty() ? target.sceneClass : owner;
}

bool ButtonSkinRuntime::isInitialized() {
    return s_buttonSkinRuntimeInitialized.load(std::memory_order_acquire);
}

bool ButtonSkinRuntime::hasVisibleSkin(CCMenuItem* item) {
    auto* state = stateOf(item);
    if (!state) return false;
    return !state->overlays.empty() || !state->colors.empty() || !state->opacities.empty();
}

ButtonSkinRuntime& ButtonSkinRuntime::get() {
    // never destroyed: its caches hold gl textures that must not die after the context.
    static auto* instance = []() {
        auto* rt = new ButtonSkinRuntime();
        s_buttonSkinRuntimeInitialized.store(true, std::memory_order_release);
        return rt;
    }();
    return *instance;
}

void ButtonSkinRuntime::markSkinUi(CCNode* node) {
    if (node) node->setUserFlag(uiFlag(), true);
}

CCMenuItem* ButtonSkinRuntime::skinnableItemFor(CCMenuItem* item) {
    // the menu touches the toggle, not the state the cursor is over.
    if (auto* toggler = item ? typeinfo_cast<CCMenuItemToggler*>(item->getParent()) : nullptr) {
        return toggler;
    }
    return item;
}

void ButtonSkinRuntime::ensureTicker() {
    if (m_tickerStarted) return;
    m_tickerStarted = true;
    SkinTicker::shared()->start();
}

void ButtonSkinRuntime::requestScan() {
    m_scanRequested = true;
    ensureTicker();
}

void ButtonSkinRuntime::onSceneSwitch(CCScene* scene) {
    ensureTicker();
    PaimonDebug::log("[skin-dbg] switch skins={} applied={} hasScene={}", ButtonSkinStore::get().all().size(),
        m_hasApplied, scene != nullptr);
    // shapes and bakes belong to the leaving scene's sprites.
    m_shapes.clear();
    clearRenderCaches();
    if (!scene || paimon::isRuntimeShuttingDown()) return;
    if (ButtonSkinStore::get().all().empty() && !m_hasApplied) return;
    m_hasApplied = scanScene(scene, true) > 0;
}

void ButtonSkinRuntime::dressEntered(CCScene* scene) {
    if (!scene || paimon::isRuntimeShuttingDown()) return;
    ensureTicker();
    if (ButtonSkinStore::get().all().empty() && !m_hasApplied) return;
    m_hasApplied = scanScene(scene, true) > 0;
    // this scan covers the scene, so the next tick only needs its light work on it.
    m_scanRequested = false;
    m_lastScene = scene;
    m_lastChildCount = scene->getChildrenCount();
    m_lastRevision = ButtonSkinStore::get().revision();
    PaimonDebug::log("[skin-dbg] entered dressed={} children={}", m_dressed.size(), m_lastChildCount);
}

void ButtonSkinRuntime::tick(float dt) {
    if (paimon::isRuntimeShuttingDown()) return;
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene) return;

    auto& store = ButtonSkinStore::get();
    if (store.all().empty() && !m_hasApplied && !m_scanRequested) return;

    unsigned int const childCount = scene->getChildrenCount();
    bool const changed = m_scanRequested || scene != m_lastScene || childCount != m_lastChildCount ||
                         store.revision() != m_lastRevision;
    static int s_dbgTicks = 0;
    if (PaimonDebug::isEnabled() && ++s_dbgTicks % 240 == 1) {
        PaimonDebug::log("[skin-dbg] tick children={} changed={} req={} sameScene={} gameplay={} skins={} applied={} dressed={}",
            childCount, changed, m_scanRequested, scene == m_lastScene, gameplayRunning(),
            store.all().size(), m_hasApplied, m_dressed.size());
    }
    // gameplay owns the frame: only popups (pause, end screen) trigger a scan there.
    if (gameplayRunning() && !changed) return;

    m_elapsed += std::isfinite(dt) ? dt : 0.f;
    if (!changed && m_elapsed < kScanInterval) return;

    // periodic scans keep dressed buttons and catch new ones by id; every few
    // (and on any change) the scored search looks for the rest.
    bool const thorough = changed || ++m_lightScans >= kLightScansPerSweep;
    if (thorough) m_lightScans = 0;

    m_scanRequested = false;
    m_elapsed = 0.f;
    m_lastScene = scene;
    m_lastChildCount = childCount;
    m_lastRevision = store.revision();
    m_hasApplied = scanScene(scene, thorough) > 0;
}

bool ButtonSkinRuntime::dressOrKeep(CCMenuItem* item, ButtonSkin const& skin, size_t hash) {
    auto* state = stateOf(item);
    if (state && state->skinId == skin.id && state->hash == hash && state->generation == m_generation &&
        maintainState(item, state)) {
        return true;
    }
    if (state) restoreState(item, state);
    applyFresh(item, skin, hash);
    return stateOf(item) != nullptr;
}

int ButtonSkinRuntime::tendDressed() {
    auto& store = ButtonSkinStore::get();
    std::vector<paimon::SafeNodeHandle<CCMenuItem>> kept;
    kept.reserve(m_dressed.size());
    for (auto const& weak : m_dressed) {
        auto* item = weak.get();
        if (!item || !item->isRunning()) continue;
        auto* state = stateOf(item);
        if (!state) continue;
        auto const* skin = findSaved(state->skinId);
        if (!skin) {
            restoreState(item, state);
            continue;
        }
        if (dressOrKeep(item, *skin, store.hashOf(skin->id))) kept.push_back(weak);
    }
    m_dressed = std::move(kept);
    return static_cast<int>(m_dressed.size());
}

int ButtonSkinRuntime::scanScene(CCScene* scene, bool thorough) {
    if (!scene) return 0;
    // transitions draw their scenes themselves: dress the incoming one so it arrives ready.
    if (auto* transition = typeinfo_cast<CCTransitionScene*>(scene)) {
        return transition->m_pInScene ? scanScene(transition->m_pInScene, thorough) : 0;
    }
    // big screens: between sweeps only the dressed buttons need care.
    if (!thorough && scene == m_walkedScene && m_walkedItems > kWalkLimit) return tendDressed();

    std::vector<CCMenuItem*> items;
    if (auto* children = scene->getChildren()) {
        for (unsigned int i = 0; i < children->count(); ++i) {
            auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
            if (!child || child->getUserFlag(uiFlag())) continue;
            collectItems(child, items);
        }
    }
    m_walkedScene = scene;
    m_walkedItems = items.size();
    m_dressed.clear();
    if (items.empty()) return 0;

    auto& store = ButtonSkinStore::get();
    auto const& skins = store.all();

    // a button carrying an older version of a skin (edited from a twin) proves nothing.
    std::unordered_map<std::string, CCMenuItem*> carriers;
    for (auto* item : items) {
        if (auto* state = stateOf(item); state && state->hash == store.hashOf(state->skinId)) {
            carriers.try_emplace(state->skinId, item);
        }
    }

    std::vector<std::optional<std::string>> signatures(items.size());
    OwnerMemo owners;
    std::unordered_set<CCMenuItem*> inScene(items.begin(), items.end());

    std::unordered_map<CCMenuItem*, ButtonSkin const*> desired;
    std::unordered_set<CCMenuItem*> exactItems;

    // 1. single-button skins win over the wider ones; a later save beats an older one.
    for (auto const& skin : skins) {
        if (skin.scope != SkinScope::Exact || !changesAnything(skin)) continue;
        CCMenuItem* found = nullptr;
        if (auto carrier = carriers.find(skin.id); carrier != carriers.end()) {
            found = carrier->second;
        } else {
            auto wanted = quickhub::usableOwnerClassOf(skin.target);
            std::vector<CCMenuItem*> candidates;
            if (thorough) {
                candidates.reserve(items.size());
                for (auto* item : items) {
                    if (wanted.empty() || owners.of(item) == wanted) candidates.push_back(item);
                }
                // its screen isn't here: nothing to find.
                if (!wanted.empty() && candidates.empty()) continue;
            }
            found = quickhub::locateButtonIn(scene, skin.target, candidates, thorough);
            if (found && !inScene.contains(found)) found = nullptr;
            if (!found && thorough) {
                PaimonDebug::log("[skin-dbg] miss {} owner={} node={} cands={} items={}",
                    skin.id, wanted, skin.target.targetNodeId, candidates.size(), items.size());
            }
        }
        if (!found) continue;
        desired[found] = &skin;
        exactItems.insert(found);
    }

    // 2. same-screen skins before game-wide ones; newest first inside each scope.
    std::vector<ButtonSkin const*> wide;
    for (auto scope : {SkinScope::SameScreen, SkinScope::Everywhere}) {
        for (auto it = skins.rbegin(); it != skins.rend(); ++it) {
            if (it->scope == scope && changesAnything(*it)) wide.push_back(&*it);
        }
    }
    if (!wide.empty()) {
        for (size_t i = 0; i < items.size(); ++i) {
            auto* item = items[i];
            if (exactItems.contains(item)) continue;
            for (auto const* skin : wide) {
                if (matchesWideScope(*skin, item, signatures[i], owners)) {
                    desired[item] = skin;
                    break;
                }
            }
        }
    }

    // 3. dress, keep or undress every button in the scene.
    for (auto* item : items) {
        auto wanted = desired.find(item);
        if (wanted == desired.end()) {
            if (auto* state = stateOf(item)) restoreState(item, state);
            continue;
        }
        auto const& skin = *wanted->second;
        if (dressOrKeep(item, skin, store.hashOf(skin.id))) m_dressed.emplace_back(item);
    }
    if (thorough) {
        PaimonDebug::log("[skin-dbg] scan items={} exact={} desired={} dressed={}",
            items.size(), exactItems.size(), desired.size(), m_dressed.size());
    }
    return static_cast<int>(m_dressed.size());
}

AssetShape ButtonSkinRuntime::shapeFor(CCNode* normal) {
    auto key = shapeKey(normal);
    if (!key.empty()) {
        if (auto found = m_shapes.find(key); found != m_shapes.end()) return found->second.shape;
    }
    auto shape = snapshotAsset(normal);
    if (shape && !key.empty()) {
        if (m_shapes.size() >= kMaxShapes) m_shapes.clear();
        auto* sprite = typeinfo_cast<CCSprite*>(normal);
        m_shapes[key] = {shape, sprite ? sprite->getTexture() : nullptr};
    }
    return shape;
}

void ButtonSkinRuntime::applyFresh(CCMenuItem* item, ButtonSkin const& skin, size_t hash) {
    if (!item) return;
    auto* state = SkinState::create();
    state->skinId = skin.id;
    state->hash = hash;
    state->generation = m_generation;
    state->scale = skin.scale;
    if (changesSound(skin)) {
        state->hasSound = true;
        state->sound.sfx = skin.target;
        state->sound.custom = hasCustomSound(skin);
        state->sound.mute = true;
    }

    bool const toggler = typeinfo_cast<CCMenuItemToggler*>(item) != nullptr;
    bool const tint = skin.tintEnabled && !sameColor(skin.tint, kWhite);
    float const opacityMul = skin.hideOriginal ? 0.f : skin.baseOpacity;
    bool const grow = std::abs(skin.scale - 1.f) > 0.001f;
    state->tint = tint ? skin.tint : kWhite;
    state->opacityMul = opacityMul;

    for (auto* visual : visualsOf(item)) {
        CCNode* normal = visual->getNormalImage();
        state->normals.emplace_back(normal);
        state->prints.push_back(printOf(normal));
        if (!normal) continue;

        // the paint is cut from the clean asset, so it goes first.
        if (hasFill(skin)) {
            if (auto shape = shapeFor(normal)) {
                if (auto* baked = bakeFill(skin, shape)) {
                    if (auto* overlay = SkinOverlay::create(baked, normal, shape.assetSize)) {
                        overlay->setBlendFunc(overlayBlend(skin.blend));
                        overlay->setOpacity(scaleOpacity(255, skin.fillOpacity));
                        overlay->setID("button-skin-overlay"_spr);
                        visual->addChild(overlay, normal->getZOrder() + 1);
                        state->overlays.emplace_back(overlay);
                    }
                }
            }
        }

        PaimonDebug::log("[skin-dbg] fresh {} fill={} overlays={} visible={} size={}x{}", skin.id,
            hasFill(skin), state->overlays.size(), normal->isVisible(),
            normal->getContentSize().width, normal->getContentSize().height);
        auto images = assetImages(visual);
        if (tint) {
            size_t const first = state->colors.size();
            for (auto* image : images) collectTargets(image, true, state->colors);
            for (size_t i = first; i < state->colors.size(); ++i) paintColor(state->colors[i], state->tint);
        }
        if (opacityMul < 0.999f) {
            size_t const first = state->opacities.size();
            for (auto* image : images) collectTargets(image, false, state->opacities);
            for (size_t i = first; i < state->opacities.size(); ++i) paintOpacity(state->opacities[i], opacityMul);
        }

        // scaling the asset (not the item) keeps the press animation and m_baseScale intact;
        // the hit area follows through the content size.
        if (grow && !toggler) {
            for (auto* image : images) scaleNode(image, skin.scale, state->images);
            state->sized = true;
            state->itemSize = item->getContentSize();
            state->appliedItemSize = state->itemSize * skin.scale;
            item->setContentSize(state->appliedItemSize);
        }
    }

    if (grow && toggler) {
        state->scaledItem = true;
        state->itemScaleX = item->getScaleX();
        state->itemScaleY = item->getScaleY();
        state->appliedItemScaleX = state->itemScaleX * skin.scale;
        state->appliedItemScaleY = state->itemScaleY * skin.scale;
        item->setScaleX(state->appliedItemScaleX);
        item->setScaleY(state->appliedItemScaleY);
    }
    if (std::abs(skin.rotation) > 0.01f) {
        state->turned = true;
        state->rotation = skin.rotation;
        state->itemRotation = item->getRotation();
        state->appliedItemRotation = state->itemRotation + skin.rotation;
        item->setRotation(state->appliedItemRotation);
    }
    if (std::abs(skin.offsetX) > 0.01f || std::abs(skin.offsetY) > 0.01f) {
        state->moved = true;
        state->offset = ccp(skin.offsetX, skin.offsetY);
        state->itemPosition = item->getPosition();
        state->appliedItemPosition = state->itemPosition + state->offset;
        item->setPosition(state->appliedItemPosition);
    }

    item->setUserObject(stateKey(), state);
}

std::string ButtonSkinRuntime::release(CCMenuItem* item) {
    auto* state = stateOf(item);
    if (!state) return {};
    std::string id = state->skinId;
    restoreState(item, state);
    requestScan();
    return id;
}

void ButtonSkinRuntime::adopt(CCMenuItem* item, std::string const& skinId) {
    if (!item) return;
    if (auto* state = stateOf(item)) restoreState(item, state);
    if (auto const* skin = findSaved(skinId); skin && changesAnything(*skin)) {
        applyFresh(item, *skin, ButtonSkinStore::get().hashOf(skinId));
        m_hasApplied = true;
    }
    requestScan();
}

std::string ButtonSkinRuntime::appliedSkinId(CCMenuItem* item) {
    auto* state = stateOf(item);
    return state ? state->skinId : std::string();
}

std::optional<SkinSound> ButtonSkinRuntime::soundFor(CCMenuItem* item) {
    auto* state = stateOf(item);
    if (!state || !state->hasSound) return std::nullopt;
    return state->sound;
}

void ButtonSkinRuntime::onGLContextReload() {
    // dead textures: every live skin repaints on its next scan.
    ++m_generation;
    m_shapes.clear();
    clearRenderCaches();
    m_scanRequested = true;
}

void ButtonSkinRuntime::shutdown() {
    ++m_generation;
    m_shapes.clear();
    clearRenderCaches();
    m_dressed.clear();
    if (m_tickerStarted) {
        SkinTicker::shared()->stop();
        m_tickerStarted = false;
    }
}

} // namespace paimon::buttonskin
