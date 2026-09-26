#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <string>

// hides vanilla brown CommentCell backgrounds (InfoLayer + ProfilePage).
// `using namespace prelude` stays in function bodies, not the header.
namespace paimon::commentbg {

inline bool shouldHideVanillaCommentBgNode(cocos2d::CCNode* node) {
    using namespace geode::prelude;
    if (!node) return false;
    // getID() returns a view; converting to std::string allocated once per node.
    auto const nodeID = node->getID();
    if (!nodeID.empty()) {
        if (nodeID.view().find("paimon-") != std::string_view::npos) return false;
        if (nodeID == "background" || nodeID == "comment-background" ||
            nodeID == "left-border" || nodeID == "right-border" ||
            nodeID == "top-border" || nodeID == "bottom-border") {
            return true;
        }
    }
    return typeinfo_cast<CCLayerColor*>(node) || typeinfo_cast<CCScale9Sprite*>(node);
}

// hide vanilla decorative bgs of cells that already have a paimon panel.
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
                // solid panel or image/gif clip both count as paimon bg.
                bool hasPaimonBg =
                    child->getChildByID("paimon-comment-bg-panel"_spr) ||
                    child->getChildByID("paimon-comment-bg-clip"_spr);
                if (!hasPaimonBg) {
                    // no nested CommentCells inside a cell; skip subtree.
                    continue;
                }

                // skip if already processed (loadFromComment clears this).
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
                child->setUserObject("paimon-comment-bgs-hidden"_spr, cocos2d::CCBool::create(true));
                continue;
            }

            self(self, child);
        }
    };

    findCells(findCells, listNode);
}

} // namespace paimon::commentbg
