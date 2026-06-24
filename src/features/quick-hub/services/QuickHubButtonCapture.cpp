#include "QuickHubButtonCapture.hpp"

#include "QuickHubManager.hpp"
#include "../ui/QuickButtonPopup.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace geode::prelude;

namespace paimon::quickhub {
namespace {

bool sameRect(CCRect const& a, CCRect const& b) {
    constexpr float epsilon = 0.01f;
    return std::abs(a.origin.x - b.origin.x) < epsilon &&
           std::abs(a.origin.y - b.origin.y) < epsilon &&
           std::abs(a.size.width - b.size.width) < epsilon &&
           std::abs(a.size.height - b.size.height) < epsilon;
}

std::string frameNameForSprite(CCSprite* sprite) {
    if (!sprite) return {};
    auto* displayed = sprite->displayFrame();
    auto* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    if (!displayed || !cache || !cache->m_pSpriteFrames) return {};

    for (auto [name, frame] : cache->m_pSpriteFrames->asExt<std::string_view, CCSpriteFrame>()) {
        if (!frame) continue;
        if (frame->getTexture() == displayed->getTexture() &&
            frame->isRotated() == displayed->isRotated() &&
            sameRect(frame->getRect(), displayed->getRect())) {
            return std::string(name);
        }
    }
    return {};
}

std::string findIconFrame(CCNode* node) {
    if (!node) return {};
    if (typeinfo_cast<CCLabelBMFont*>(node)) return {};
    if (auto* children = node->getChildren()) {
        for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
            auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
            auto name = findIconFrame(child);
            if (!name.empty()) return name;
        }
    }
    return frameNameForSprite(typeinfo_cast<CCSprite*>(node));
}

bool isActuallyVisible(CCNode* node) {
    for (auto* current = node; current; current = current->getParent()) {
        if (!current->isVisible()) return false;
    }
    return true;
}

CCMenuItem* findButtonAt(CCNode* node, CCPoint worldPoint) {
    if (!node || !node->isVisible()) return nullptr;

    if (auto* children = node->getChildren()) {
        for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
            auto* child = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
            if (auto* found = findButtonAt(child, worldPoint)) return found;
        }
    }

    auto* item = typeinfo_cast<CCMenuItem*>(node);
    if (!item || !item->isEnabled() || !isActuallyVisible(item)) return nullptr;
    auto size = item->getContentSize();
    if (size.width <= 0.f || size.height <= 0.f) return nullptr;
    auto local = item->convertToNodeSpace(worldPoint);
    return CCRect(0.f, 0.f, size.width, size.height).containsPoint(local) ? item : nullptr;
}

std::vector<int> makeNodePath(CCNode* node, CCScene* scene) {
    std::vector<int> reversePath;
    for (auto* current = node; current && current != scene; current = current->getParent()) {
        auto* parent = current->getParent();
        auto* children = parent ? parent->getChildren() : nullptr;
        if (!children) return {};
        unsigned int index = children->indexOfObject(current);
        if (index == CC_INVALID_INDEX) return {};
        reversePath.push_back(static_cast<int>(index));
    }
    std::ranges::reverse(reversePath);
    return reversePath;
}

CCNode* resolvePath(CCScene* scene, std::vector<int> const& path) {
    CCNode* current = scene;
    for (int index : path) {
        auto* children = current ? current->getChildren() : nullptr;
        if (!children || index < 0 || index >= static_cast<int>(children->count())) return nullptr;
        current = typeinfo_cast<CCNode*>(children->objectAtIndex(index));
    }
    return current;
}

std::string humanName(std::string value) {
    if (auto slash = value.rfind('/'); slash != std::string::npos) value.erase(0, slash + 1);
    if (value.ends_with(".png")) value.resize(value.size() - 4);
    for (char& ch : value) {
        if (ch == '-' || ch == '_') ch = ' ';
    }
    if (value.empty()) return "Boton rapido";
    value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
    return value;
}

std::string iconFrameOfItem(CCMenuItem* item) {
    auto* spriteItem = typeinfo_cast<CCMenuItemSprite*>(item);
    return spriteItem ? findIconFrame(spriteItem->getNormalImage()) : std::string();
}

std::string findLabelText(CCNode* node) {
    if (!node) return {};
    if (auto* bm = typeinfo_cast<CCLabelBMFont*>(node)) {
        if (auto const* s = bm->getString()) {
            if (std::string str = s; !str.empty()) return str;
        }
    }
    if (auto* ttf = typeinfo_cast<CCLabelTTF*>(node)) {
        if (auto const* s = ttf->getString()) {
            if (std::string str = s; !str.empty()) return str;
        }
    }
    if (auto* children = node->getChildren()) {
        for (int i = 0; i < static_cast<int>(children->count()); ++i) {
            auto text = findLabelText(typeinfo_cast<CCNode*>(children->objectAtIndex(i)));
            if (!text.empty()) return text;
        }
    }
    return {};
}
int pathPrefixScore(std::vector<int> const& a, std::vector<int> const& b) {
    int matched = 0;
    size_t common = std::min(a.size(), b.size());
    for (size_t i = 0; i < common; ++i) {
        if (a[i] == b[i]) ++matched;
        else break;
    }
    // Reward an exact full-path match a bit more.
    if (a.size() == b.size() && static_cast<size_t>(matched) == a.size()) matched += 5;
    return matched;
}

struct Eval {
    long score = 0;
    bool strong = false;
};

Eval evaluateCandidate(CCMenuItem* item, CustomQuickButton const& def) {
    Eval e;
    if (!def.targetNodeId.empty() && item->getID() == def.targetNodeId) {
        e.score += 1000;
        e.strong = true;
    }
    if (!def.labelText.empty()) {
        auto text = findLabelText(item);
        if (!text.empty() && text == def.labelText) {
            e.score += 400;
            e.strong = true;
        }
    }
    if (!def.icon.empty()) {
        auto frame = iconFrameOfItem(item);
        if (!frame.empty() && frame == def.icon) {
            e.score += 200;
            e.strong = true;
        }
    }
    if (def.tag != 0 && item->getTag() == def.tag) {
        e.score += 150;
        e.strong = true;
    }
    if (!def.parentId.empty()) {
        auto* parent = item->getParent();
        if (parent && parent->getID() == def.parentId) e.score += 60; // context only
    }
    return e;
}

bool hasAnyIdentity(CustomQuickButton const& def) {
    return !def.targetNodeId.empty() || !def.labelText.empty() ||
           !def.icon.empty() || def.tag != 0;
}

struct ButtonMatch {
    CCMenuItem* item = nullptr;
    long score = std::numeric_limits<long>::min();
};// saved identity, using the recorded node path only to break ties.
void scanBestButton(CCNode* node, CCScene* scene, CustomQuickButton const& def, ButtonMatch& best) {
    if (!node || !node->isVisible()) return;

    if (auto* children = node->getChildren()) {
        for (int i = static_cast<int>(children->count()) - 1; i >= 0; --i) {
            scanBestButton(typeinfo_cast<CCNode*>(children->objectAtIndex(i)), scene, def, best);
        }
    }

    auto* item = typeinfo_cast<CCMenuItem*>(node);
    if (!item || !item->isEnabled() || !isActuallyVisible(item)) return;

    auto eval = evaluateCandidate(item, def);
    if (!eval.strong) return;
    long score = eval.score;
    if (score > best.score) {
        best.score = score;
        best.item = item;
    }
}

}

bool handleQuickButtonRightClick() {
    if (QuickButtonPopup::isOpen()) return true;
    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene) return false;

    auto* button = findButtonAt(scene, geode::cocos::getMousePos());
    if (!button) return false;

    auto* spriteItem = typeinfo_cast<CCMenuItemSprite*>(button);
    std::string icon = spriteItem ? findIconFrame(spriteItem->getNormalImage()) : std::string();
    std::string nodeId = button->getID();
    std::string suggestedName = humanName(!nodeId.empty() ? nodeId : icon);

    CustomQuickButton candidate;
    candidate.name = suggestedName;
    candidate.id = QuickHubManager::get().makeUniqueCustomId(suggestedName);
    candidate.targetNodeId = std::move(nodeId);
    candidate.icon = std::move(icon);
    candidate.nodePath = makeNodePath(button, scene);
    candidate.tag = button->getTag();
    candidate.labelText = findLabelText(button);
    if (auto* parent = button->getParent()) candidate.parentId = parent->getID();

    if (auto* popup = QuickButtonPopup::create(std::move(candidate))) popup->show();
    return true;
}

bool activateCustomQuickButton(std::string const& id) {
    auto definition = QuickHubManager::get().getCustomButton(id);
    if (!definition) return false;

    auto* director = CCDirector::get();
    auto* scene = director ? director->getRunningScene() : nullptr;
    if (!scene || paimon::isRuntimeShuttingDown()) return true;

    bool const anyIdentity = hasAnyIdentity(*definition);
    CCMenuItem* item = typeinfo_cast<CCMenuItem*>(resolvePath(scene, definition->nodePath));
    if (item) {
        bool ok = item->isEnabled() && isActuallyVisible(item);
        if (ok && anyIdentity) ok = evaluateCandidate(item, *definition).strong;
        if (!ok) item = nullptr;
    }
    if (!item && anyIdentity) {
        ButtonMatch best;
        scanBestButton(scene, scene, *definition, best);
        item = best.item;
    }
    if (!item && !anyIdentity) {
        item = typeinfo_cast<CCMenuItem*>(resolvePath(scene, definition->nodePath));
    }

    if (!item || !item->isEnabled()) {
        PaimonNotify::create(
            "Ese boton no esta en esta pantalla. Abre la pantalla donde lo guardaste.",
            NotificationIcon::Warning)->show();
        return true;
    }

    item->activate();
    return true;
}

} // namespace paimon::quickhub
