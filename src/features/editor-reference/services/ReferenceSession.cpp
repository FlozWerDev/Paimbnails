#include "../../../utils/FilesystemPath.hpp"
#include "ReferenceSession.hpp"
#include "ReferenceStore.hpp"

#include "../../editor-suite/EditorHelpers.hpp"
#include "../../../utils/ImageLoadHelper.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::editorref {

namespace {

constexpr int kZOrderBehind = -200;
constexpr int kZOrderFront = 200;
// Base world dimension at anchor scale = 1.0 (6 GD grid blocks), so GD's native
// Scale slider (0.50..2.00) smoothly spans 3..12 blocks without clamping small.
constexpr float kBaseWorldDim = 180.f;
constexpr size_t kMaxImageMB = 20;

int zOrderFor(ZPlacement z) {
    return z == ZPlacement::Front ? kZOrderFront : kZOrderBehind;
}

CCDrawNode* createSelectionBorder(float spriteW, float spriteH) {
    auto* border = CCDrawNode::create();
    if (!border) return nullptr;
    float const lineW = std::clamp(std::max(spriteW, spriteH) * 0.004f, 0.8f, 3.f);
    ccColor4F const fill{0.f, 0.f, 0.f, 0.f};
    ccColor4F const stroke{0.25f, 0.95f, 1.f, 0.92f};
    CCPoint verts[4] = {
        {0.f, 0.f},
        {spriteW, 0.f},
        {spriteW, spriteH},
        {0.f, spriteH},
    };
    border->drawPolygon(verts, 4, fill, lineW, stroke);
    border->setVisible(false);
    return border;
}

} // namespace

ReferenceSession& ReferenceSession::get() {
    // Heap-leaked singleton to avoid DllMain static teardown issues.
    static auto* instance = new ReferenceSession();
    return *instance;
}

bool ReferenceSession::isAttached(LevelEditorLayer* editor) const {
    return editor && m_editor == editor && LevelEditorLayer::get() == editor;
}

LevelEditorLayer* ReferenceSession::editor() const {
    return (m_editor && LevelEditorLayer::get() == m_editor) ? m_editor : nullptr;
}

ReferenceSession::RuntimeItem* ReferenceSession::findItem(int id) {
    for (auto& item : m_items) {
        if (item.entry.id == id) return &item;
    }
    return nullptr;
}

ReferenceSession::RuntimeItem const* ReferenceSession::findItem(int id) const {
    for (auto const& item : m_items) {
        if (item.entry.id == id) return &item;
    }
    return nullptr;
}

void ReferenceSession::applyAnchorSilentStyle(GameObject* anchor) {
    if (!anchor) return;
    anchor->m_isDontFade = true;
    anchor->m_isDontEnter = true;
    anchor->setOpacity(0);
    if (anchor->m_colorSprite) anchor->m_colorSprite->setOpacity(0);
    if (anchor->m_glowSprite) anchor->m_glowSprite->setOpacity(0);
}

bool ReferenceSession::spawnRuntimeItem(LevelEditorLayer* editor, RuntimeItem& item) {
    if (!editor || !editor->m_objectLayer) return false;

    auto const assetPath = store::resolveAssetPath(item.entry.assetFile);
    auto loaded = ImageLoadHelper::loadStaticImage(assetPath, kMaxImageMB, false);
    if (!loaded.success || !loaded.texture) {
        log::warn("[EditorReference] Could not load asset '{}': {}", item.entry.assetFile, loaded.error);
        return false;
    }

    auto* sprite = CCSprite::createWithTexture(loaded.texture);
    item.texWidth = std::max(1, loaded.width);
    item.texHeight = std::max(1, loaded.height);
    loaded.texture->release();
    if (!sprite) return false;

    sprite->setID(fmt::format("reference-image-sprite-{}"_spr, item.entry.id));
    sprite->setAnchorPoint({0.5f, 0.5f});

    auto const spritePts = sprite->getContentSize();
    auto* border = createSelectionBorder(
        std::max(1.f, spritePts.width),
        std::max(1.f, spritePts.height)
    );
    if (border) {
        sprite->addChild(border, 10);
    }

    item.entry.x = std::max(item.entry.x, 15.f);
    item.entry.y = std::max(item.entry.y, 15.f);
    auto* anchor = editor->createObject(kAnchorObjectID, {item.entry.x, item.entry.y}, true);
    if (!anchor) {
        return false;
    }

    anchor->setUserObject(kAnchorTagKey, CCInteger::create(item.entry.id));
    anchor->updateCustomScaleX(item.entry.scaleX);
    anchor->updateCustomScaleY(item.entry.scaleY);
    anchor->setRotationX(item.entry.rotationX);
    anchor->setRotationY(item.entry.rotationY);
    anchor->setSkewX(item.entry.skewX);
    anchor->setSkewY(item.entry.skewY);
    anchor->setFlipX(item.entry.flipX);
    anchor->setFlipY(item.entry.flipY);
    editor->updateObjectSection(anchor);
    applyAnchorSilentStyle(anchor);

    editor->m_objectLayer->addChild(sprite, zOrderFor(item.entry.zPlacement));

    item.anchor = anchor;
    item.sprite = sprite;
    item.border = border;
    item.deletedInEditor = false;

    bool const playtesting = editor->m_playbackMode != PlaybackMode::Not;
    syncItem(editor, item, playtesting);
    return true;
}

void ReferenceSession::attach(LevelEditorLayer* editor) {
    m_items.clear();
    m_nextId = 1;
    m_editor = editor;
    if (!editor || !editor->m_level) return;

    auto doc = store::loadLevelDocument(editor->m_level);
    m_nextId = std::max(1, doc.nextId);

    for (auto& entry : doc.entries) {
        RuntimeItem item;
        item.entry = std::move(entry);
        if (spawnRuntimeItem(editor, item)) {
            m_nextId = std::max(m_nextId, item.entry.id + 1);
            m_items.push_back(std::move(item));
        }
    }
}

void ReferenceSession::detach(LevelEditorLayer* editor, bool allowPersist) {
    if (!editor || m_editor != editor) return;
    if (allowPersist && LevelEditorLayer::get() == editor && editor->m_level) {
        persist(editor);
    }
    m_items.clear();
    m_editor = nullptr;
}

std::vector<ReferenceEntry> ReferenceSession::activeEntries() const {
    std::vector<ReferenceEntry> out;
    out.reserve(m_items.size());
    for (auto const& item : m_items) {
        if (item.deletedInEditor) continue;
        if (!item.anchor.lock()) continue;
        out.push_back(item.entry);
    }
    return out;
}

ReferenceEntry const* ReferenceSession::findEntry(int id) const {
    auto const* item = findItem(id);
    if (!item || item->deletedInEditor) return nullptr;
    return &item->entry;
}

int ReferenceSession::entryIdForAnchor(GameObject* object) const {
    if (!object) return 0;
    if (auto* tag = typeinfo_cast<CCInteger*>(object->getUserObject(kAnchorTagKey))) {
        return tag->getValue();
    }
    return 0;
}

bool ReferenceSession::isReferenceAnchor(GameObject* object) const {
    return entryIdForAnchor(object) > 0;
}

bool ReferenceSession::isLockedAnchor(LevelEditorLayer* editor, GameObject* object) const {
    if (!isAttached(editor) || !object) return false;
    int const id = entryIdForAnchor(object);
    if (id <= 0) return false;
    auto const* item = findItem(id);
    return item && !item->deletedInEditor && item->entry.locked;
}

Result<int> ReferenceSession::addImported(
    std::string const& assetFile,
    std::string const& name,
    int texW,
    int texH
) {
    auto* ed = editor();
    if (!ed || !ed->m_objectLayer) return Err("No active editor");
    if (activeEntries().size() >= static_cast<std::size_t>(kMaxReferencesPerLevel)) {
        return Err("Maximum reference images reached for this level");
    }

    auto const win = CCDirector::get()->getWinSize();
    auto const center = ed->m_objectLayer->convertToNodeSpace(win / 2.f);

    RuntimeItem item;
    item.entry.id = m_nextId++;
    item.entry.name = name;
    item.entry.assetFile = assetFile;
    item.entry.x = std::max(center.x, 15.f);
    item.entry.y = std::max(center.y, 45.f);
    item.entry.scaleX = 1.f;
    item.entry.scaleY = 1.f;
    item.entry.opacity = 145;
    item.entry.zPlacement = ZPlacement::Behind;
    item.entry.locked = false;
    item.entry.visible = true;
    item.entry.showInPlaytest = false;

    if (!spawnRuntimeItem(ed, item)) {
        return Err("Could not spawn reference image");
    }

    int const newId = item.entry.id;
    m_items.push_back(std::move(item));
    persist(ed);

    if (auto* ui = ed->m_editorUI) {
        if (auto* added = findItem(newId)) {
            if (auto anchor = added->anchor.lock()) {
                ui->deselectAll();
                ui->selectObject(anchor.data(), true);
                ui->updateButtons();
            }
        }
    }
    return Ok(newId);
}

Result<int> ReferenceSession::addFromFile(std::filesystem::path const& path) {
    std::string assetFile;
    std::string name;
    std::string error;
    int w = 0, h = 0;
    if (!store::importImageFile(path, assetFile, name, w, h, error)) {
        return Err(error);
    }
    store::recordHistoryFromAsset(assetFile, name, w, h);
    return addImported(assetFile, name, w, h);
}

Result<int> ReferenceSession::addFromClipboard() {
    std::string assetFile;
    std::string name;
    std::string error;
    int w = 0, h = 0;
    if (!store::importClipboardImage(assetFile, name, w, h, error)) {
        return Err(error);
    }
    store::recordHistoryFromAsset(assetFile, name, w, h);
    return addImported(assetFile, name, w, h);
}

Result<int> ReferenceSession::addFromHistory(std::string const& historyId) {
    std::string assetFile;
    std::string name;
    std::string error;
    int w = 0, h = 0;
    if (!store::importFromHistory(historyId, assetFile, name, w, h, error)) {
        return Err(error);
    }
    return addImported(assetFile, name, w, h);
}

Result<int> ReferenceSession::duplicateEntry(int id) {
    auto* ed = editor();
    if (!ed) return Err("No active editor");
    auto* src = findItem(id);
    if (!src || src->deletedInEditor) return Err("Reference not found");
    if (activeEntries().size() >= static_cast<std::size_t>(kMaxReferencesPerLevel)) {
        return Err("Maximum reference images reached for this level");
    }

    syncAll(ed);
    RuntimeItem copy;
    copy.entry = src->entry;
    copy.entry.id = m_nextId++;
    copy.entry.assetFile = store::cloneAssetFile(src->entry.assetFile);
    copy.entry.x += 30.f;
    copy.entry.y -= 30.f;
    copy.entry.locked = false;

    if (!spawnRuntimeItem(ed, copy)) {
        return Err("Could not duplicate reference image");
    }

    int const newId = copy.entry.id;
    m_items.push_back(std::move(copy));
    persist(ed);
    return Ok(newId);
}

void ReferenceSession::deselectAnchor(GameObject* anchor) {
    auto* ed = editor();
    auto* ui = ed ? ed->m_editorUI : nullptr;
    if (!ui || !anchor) return;
    bool const isSel = anchor->m_isSelected
        || ui->m_selectedObject == anchor
        || (ui->m_selectedObjects && ui->m_selectedObjects->containsObject(anchor));
    if (isSel) {
        ui->deselectObject(anchor);
        ui->updateButtons();
    }
}

namespace {
bool hasValidSections(GameObject* obj) {
    return obj
        && obj->m_outerSectionIndex >= 0
        && obj->m_middleSectionIndex >= 0
        && obj->m_innerSectionIndex >= 0;
}

void removeAnchorSafely(LevelEditorLayer* ed, GameObject* anchor, bool& internalFlag) {
    if (!ed || !anchor) return;
    if (hasValidSections(anchor) && ed->m_objects && ed->m_objects->containsObject(anchor)) {
        internalFlag = true;
        ed->removeObject(anchor, true);
        internalFlag = false;
    } else {
        if (ed->m_objects) ed->m_objects->removeObject(anchor, true);
        if (anchor->getParent()) anchor->removeFromParent();
    }
}
} // namespace

bool ReferenceSession::removeEntry(int id) {
    auto* ed = editor();
    auto it = std::find_if(m_items.begin(), m_items.end(), [id](RuntimeItem const& item) {
        return item.entry.id == id;
    });
    if (it == m_items.end()) return false;

    std::string const oldAsset = it->entry.assetFile;

    if (auto anchor = it->anchor.lock()) {
        deselectAnchor(anchor.data());
        if (ed && !it->deletedInEditor) {
            removeAnchorSafely(ed, anchor.data(), m_internalRemoval);
        }
    }
    if (auto sprite = it->sprite.lock()) {
        sprite->removeFromParent();
    }

    m_items.erase(it);

    ReferenceDocument doc;
    doc.nextId = m_nextId;
    for (auto const& item : m_items) {
        if (!item.deletedInEditor) doc.entries.push_back(item.entry);
    }
    store::deleteAssetIfUnused(oldAsset, doc);
    if (ed) persist(ed);
    return true;
}

bool ReferenceSession::deleteSelectedAnchors(EditorUI* ui) {
    auto* ed = editor();
    if (!ui || !ed || !isAttached(ed) || m_items.empty()) return false;

    std::vector<int> refIds;
    bool hasNormalSelected = false;

    if (ui->m_selectedObject) {
        int const id = entryIdForAnchor(ui->m_selectedObject);
        if (id > 0) refIds.push_back(id);
        else hasNormalSelected = true;
    } else if (ui->m_selectedObjects && ui->m_selectedObjects->count() > 0) {
        for (auto* obj : CCArrayExt<GameObject*>(ui->m_selectedObjects)) {
            if (!obj) continue;
            int const id = entryIdForAnchor(obj);
            if (id > 0) refIds.push_back(id);
            else hasNormalSelected = true;
        }
    }

    if (refIds.empty()) return false;

    for (int id : refIds) {
        removeEntry(id);
    }
    return !hasNormalSelected;
}

void ReferenceSession::setOpacity(int id, int opacity) {
    auto* item = findItem(id);
    if (!item) return;
    item->entry.opacity = std::clamp(opacity, 0, 255);
    if (auto sprite = item->sprite.lock()) {
        sprite->setOpacity(static_cast<GLubyte>(item->entry.opacity));
    }
    if (auto* ed = editor()) persist(ed);
}

void ReferenceSession::setLocked(int id, bool locked) {
    auto* item = findItem(id);
    if (!item) return;
    item->entry.locked = locked;
    if (locked) {
        if (auto anchor = item->anchor.lock()) {
            deselectAnchor(anchor.data());
        }
    }
    if (auto* ed = editor()) persist(ed);
}

void ReferenceSession::setVisible(int id, bool visible) {
    auto* item = findItem(id);
    if (!item) return;
    item->entry.visible = visible;
    if (auto* ed = editor()) {
        syncAll(ed);
        persist(ed);
    }
}

void ReferenceSession::setZPlacement(int id, ZPlacement z) {
    auto* item = findItem(id);
    if (!item) return;
    item->entry.zPlacement = z;
    if (auto sprite = item->sprite.lock()) {
        sprite->setZOrder(zOrderFor(z));
    }
    if (auto* ed = editor()) persist(ed);
}

void ReferenceSession::setShowInPlaytest(int id, bool show) {
    auto* item = findItem(id);
    if (!item) return;
    item->entry.showInPlaytest = show;
    if (auto* ed = editor()) {
        syncAll(ed);
        persist(ed);
    }
}

void ReferenceSession::flipEntry(int id, bool axisY) {
    auto* item = findItem(id);
    if (!item || item->deletedInEditor) return;
    if (auto anchor = item->anchor.lock()) {
        if (axisY) {
            anchor->setFlipY(!anchor->isFlipY());
        } else {
            anchor->setFlipX(!anchor->isFlipX());
        }
        if (auto* ed = editor()) {
            syncAll(ed);
            persist(ed);
        }
    }
}

void ReferenceSession::resetTransform(int id) {
    auto* item = findItem(id);
    if (!item || item->deletedInEditor) return;
    auto anchor = item->anchor.lock();
    if (!anchor) return;

    anchor->setRotationX(0.f);
    anchor->setRotationY(0.f);
    anchor->setSkewX(0.f);
    anchor->setSkewY(0.f);
    anchor->setFlipX(false);
    anchor->setFlipY(false);
    anchor->updateCustomScaleX(1.f);
    anchor->updateCustomScaleY(1.f);

    if (auto* ed = editor()) {
        syncAll(ed);
        persist(ed);
    }
}

bool ReferenceSession::selectAndFocus(int id, bool unlockIfLocked) {
    auto* ed = editor();
    auto* ui = ed ? ed->m_editorUI : nullptr;
    auto* item = findItem(id);
    if (!ed || !ui || !item || item->deletedInEditor) return false;
    auto anchor = item->anchor.lock();
    if (!anchor) return false;

    if (item->entry.locked) {
        if (!unlockIfLocked) return false;
        item->entry.locked = false;
    }
    if (!item->entry.visible) {
        item->entry.visible = true;
    }

    paimon::editor::focusCameraOnPoint(ed, anchor->getPosition());
    ui->deselectAll();
    ui->selectObject(anchor.data(), true);
    ui->updateButtons();
    syncAll(ed);
    persist(ed);
    return true;
}

void ReferenceSession::focusEntry(int id) {
    auto* ed = editor();
    auto* item = findItem(id);
    if (!ed || !item || item->deletedInEditor) return;
    if (auto anchor = item->anchor.lock()) {
        paimon::editor::focusCameraOnPoint(ed, anchor->getPosition());
    }
}

int ReferenceSession::singleSelectedReferenceId(EditorUI* ui) const {
    if (!ui) return 0;
    if (ui->m_selectedObject) {
        return entryIdForAnchor(ui->m_selectedObject);
    }
    if (ui->m_selectedObjects && ui->m_selectedObjects->count() == 1) {
        auto* obj = typeinfo_cast<GameObject*>(ui->m_selectedObjects->objectAtIndex(0));
        return entryIdForAnchor(obj);
    }
    return 0;
}

GameObject* ReferenceSession::pickUnlockedAnchorAtScreenPos(
    LevelEditorLayer* editor,
    CCPoint screenPos
) const {
    if (!isAttached(editor) || m_items.empty()) return nullptr;
    if (editor->m_playbackMode != PlaybackMode::Not) return nullptr;

    // Prefer Front-layered references first, then newest added.
    for (int pass = 0; pass < 2; ++pass) {
        ZPlacement const want = (pass == 0) ? ZPlacement::Front : ZPlacement::Behind;
        for (auto it = m_items.rbegin(); it != m_items.rend(); ++it) {
            auto const& item = *it;
            if (item.deletedInEditor || item.entry.locked || !item.entry.visible) continue;
            if (item.entry.zPlacement != want) continue;
            auto sprite = item.sprite.lock();
            auto anchor = item.anchor.lock();
            if (!sprite || !anchor || !sprite->isVisible()) continue;
            auto const local = sprite->convertToNodeSpace(screenPos);
            auto const sz = sprite->getContentSize();
            if (local.x >= 0.f && local.y >= 0.f && local.x <= sz.width && local.y <= sz.height) {
                return anchor.data();
            }
        }
    }
    return nullptr;
}

void ReferenceSession::syncItem(LevelEditorLayer* editor, RuntimeItem& item, bool playtesting) {
    auto sprite = item.sprite.lock();
    if (!sprite) return;

    auto anchor = item.anchor.lock();
    if (item.deletedInEditor || !anchor) {
        sprite->setVisible(false);
        return;
    }

    applyAnchorSilentStyle(anchor.data());

    float const fw = static_cast<float>(std::max(1, item.texWidth));
    float const fh = static_cast<float>(std::max(1, item.texHeight));
    float const aspect = fw / fh;
    float const baseWorldW = (fw >= fh) ? kBaseWorldDim : kBaseWorldDim * aspect;
    float const baseWorldH = (fh > fw) ? kBaseWorldDim : kBaseWorldDim / aspect;

    float const spriteW = std::max(1.f, sprite->getContentSize().width);
    float const spriteH = std::max(1.f, sprite->getContentSize().height);

    float const ax = anchor->getPositionX();
    float const ay = anchor->getPositionY();
    float const sx = anchor->getScaleX();
    float const sy = anchor->getScaleY();
    float const rx = anchor->getRotationX();
    float const ry = anchor->getRotationY();
    float const skx = anchor->getSkewX();
    float const sky = anchor->getSkewY();
    bool const fx = anchor->isFlipX();
    bool const fy = anchor->isFlipY();

    sprite->setPosition({ax, ay});
    sprite->setScaleX(sx * (baseWorldW / spriteW));
    sprite->setScaleY(sy * (baseWorldH / spriteH));
    sprite->setRotationX(rx);
    sprite->setRotationY(ry);
    sprite->setSkewX(skx);
    sprite->setSkewY(sky);
    sprite->setFlipX(fx);
    sprite->setFlipY(fy);
    sprite->setOpacity(static_cast<GLubyte>(std::clamp(item.entry.opacity, 0, 255)));

    bool const show = item.entry.visible && (!playtesting || item.entry.showInPlaytest);
    sprite->setVisible(show);

    if (auto border = item.border.lock()) {
        border->setVisible(show && !playtesting && anchor->m_isSelected);
    }

    item.entry.x = ax;
    item.entry.y = ay;
    item.entry.scaleX = sx;
    item.entry.scaleY = sy;
    item.entry.rotationX = rx;
    item.entry.rotationY = ry;
    item.entry.skewX = skx;
    item.entry.skewY = sky;
    item.entry.flipX = fx;
    item.entry.flipY = fy;
}

void ReferenceSession::syncAll(LevelEditorLayer* editor) {
    if (!isAttached(editor) || m_items.empty()) return;
    bool const playtesting = editor->m_playbackMode != PlaybackMode::Not;
    for (auto& item : m_items) {
        syncItem(editor, item, playtesting);
    }
}

void ReferenceSession::onObjectRemoved(LevelEditorLayer* editor, GameObject* object) {
    if (m_internalRemoval || !isAttached(editor) || !object) return;
    int const id = entryIdForAnchor(object);
    if (id <= 0) return;
    if (auto* item = findItem(id)) {
        item->deletedInEditor = true;
        if (auto sprite = item->sprite.lock()) {
            sprite->setVisible(false);
        }
    }
}

void ReferenceSession::reconcileUndoRedo(LevelEditorLayer* editor) {
    if (!isAttached(editor) || !editor->m_objects || m_items.empty()) return;
    for (auto& item : m_items) {
        auto anchor = item.anchor.lock();
        if (!anchor) {
            item.deletedInEditor = true;
            continue;
        }
        item.deletedInEditor = !editor->m_objects->containsObject(anchor.data());
    }
    syncAll(editor);
}

std::vector<StashedAnchor> ReferenceSession::stripAnchorsForSerialization(LevelEditorLayer* editor) {
    std::vector<StashedAnchor> stashed;
    if (!isAttached(editor) || !editor->m_objects || m_items.empty()) return stashed;

    syncAll(editor);

    unsigned int const count = editor->m_objects->count();
    for (unsigned int i = count; i > 0; --i) {
        unsigned int const idx = i - 1;
        auto* obj = typeinfo_cast<GameObject*>(editor->m_objects->objectAtIndex(idx));
        if (obj && isReferenceAnchor(obj)) {
            stashed.push_back(StashedAnchor{idx, obj});
            editor->m_objects->removeObjectAtIndex(idx, false);
        }
    }
    return stashed;
}

void ReferenceSession::restoreAnchorsAfterSerialization(
    LevelEditorLayer* editor,
    std::vector<StashedAnchor>& stashed
) {
    if (!editor || !editor->m_objects || stashed.empty()) return;
    for (auto it = stashed.rbegin(); it != stashed.rend(); ++it) {
        if (!it->object) continue;
        unsigned int const insertIdx = std::min<unsigned int>(it->index, editor->m_objects->count());
        editor->m_objects->insertObject(it->object.data(), insertIdx);
    }
    stashed.clear();
}

void ReferenceSession::persist(LevelEditorLayer* editor) {
    if (!isAttached(editor) || !editor->m_level) return;
    syncAll(editor);

    ReferenceDocument doc;
    doc.nextId = m_nextId;
    for (auto const& item : m_items) {
        if (item.deletedInEditor) continue;
        if (!item.anchor.lock()) continue;
        doc.entries.push_back(item.entry);
    }
    store::saveDocument(store::levelKey(editor->m_level), doc);
}

} // namespace paimon::editorref
