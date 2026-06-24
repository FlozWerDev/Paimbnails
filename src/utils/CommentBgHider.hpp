#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <string>

// Hides the vanilla brown CommentCell backgrounds. Shared by InfoLayer and
// ProfilePage. `using namespace geode::prelude;` is confined to each inline
// function body so it doesn't leak to includers.
namespace paimon::commentbg {

inline bool shouldHideVanillaCommentBgNode(cocos2d::CCNode* node) {
    using namespace geode::prelude;
    if (!node) return false;
    std::string nodeID = node->getID();
    if (!nodeID.empty()) {
        if (nodeID.find("paimon-") != std::string::npos) return false;
        if (nodeID == "background" || nodeID == "comment-background" ||
            nodeID == "left-border" || nodeID == "right-border" ||
            nodeID == "top-border" || nodeID == "bottom-border") {
            return true;
        }
    }
    return typeinfo_cast<CCLayerColor*>(node) || typeinfo_cast<CCScale9Sprite*>(node);
}

// Walk a GJCommentListLayer (or any subtree) and hide the vanilla decorative
// backgrounds of each CommentCell that already has a paimon panel. The
// "paimon-comment-bgs-hidden" user object caches processed cells to avoid
// reprocessing non-recycled ones.
inline void hideCommentCellBgs(cocos2d::CCNode* listNode) {
    using namespace geode::prelude;
    if (!listNode) return;

    auto findCells = [&](auto const& self, CCNode* node) -> void {
        if (!node) return;
        auto* children = node->getChildren();
        if (!children) return;
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (!child) continue;

            if (typeinfo_cast<CommentCell*>(child)) {
                // only hide vanilla if the cell already has a paimon panel
                if (!child->getChildByIDRecursive("paimon-comment-bg-panel"_spr)) {
                    self(self, child);
                    continue;
                }

                // FPS: skip recursion if the cell was already processed and not
                // recycled (loadFromComment clears this flag)
                if (child->getUserObject("paimon-comment-bgs-hidden"_spr)) {
                    continue;
                }

                auto hideBgsRecursive = [](auto const& recurse, CCNode* node) -> void {
                    if (!node) return;
                    auto* kids = node->getChildren();
                    if (!kids) return;
                    for (auto* k : CCArrayExt<CCNode*>(kids)) {
                        if (!k) continue;

                        if (!shouldHideVanillaCommentBgNode(k)) {
                            if (!typeinfo_cast<CCMenu*>(k)) {
                                recurse(recurse, k);
                            }
                            continue;
                        }

                        k->setVisible(false);
                    }
                };

                hideBgsRecursive(hideBgsRecursive, child);
                // mark processed until the next loadFromComment
                child->setUserObject("paimon-comment-bgs-hidden"_spr, cocos2d::CCBool::create(true));
            }

            self(self, child);
        }
    };

    findCells(findCells, listNode);
}

} // namespace paimon::commentbg
