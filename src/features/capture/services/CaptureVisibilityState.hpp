#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <algorithm>

namespace paimon::capture {
    // User-VISIBLE nodes capture must not hide; WeakRefs + prune on write
    // (raw pointers went stale when the level exited under the popup).
    inline std::vector<geode::WeakRef<cocos2d::CCNode>>& userShownNodes() {
        static auto& s = *new std::vector<geode::WeakRef<cocos2d::CCNode>>();
        return s;
    }

    inline void pruneUserShown() {
        auto& v = userShownNodes();
        v.erase(std::remove_if(v.begin(), v.end(),
            [](auto const& w) { return !w.lock(); }), v.end());
    }

    inline void setUserShown(cocos2d::CCNode* node, bool shown) {
        if (!node) return;
        pruneUserShown();
        auto& v = userShownNodes();
        if (shown) {
            for (auto const& w : v) {
                if (w.lock().data() == node) return;
            }
            v.emplace_back(node);
        } else {
            v.erase(std::remove_if(v.begin(), v.end(),
                [node](auto const& w) { return w.lock().data() == node; }), v.end());
        }
    }

    inline bool isUserShown(cocos2d::CCNode* node) {
        if (!node) return false;
        for (auto const& w : userShownNodes()) {
            if (w.lock().data() == node) return true;
        }
        return false;
    }

    inline void clearUserShown() {
        userShownNodes().clear();
    }

    struct VisibilityRecord {
        geode::WeakRef<cocos2d::CCNode> node;
        bool visible = true;
    };

    inline bool tryGetRecordedVisibility(std::vector<VisibilityRecord> const& records, cocos2d::CCNode* node, bool& outVisible) {
        if (!node) return false;

        for (auto const& record : records) {
            auto recordedNode = record.node.lock();
            if (recordedNode.data() == node) {
                outVisible = record.visible;
                return true;
            }
        }

        return false;
    }

    inline void recordVisibility(std::vector<VisibilityRecord>& records, cocos2d::CCNode* node, bool visible) {
        if (!node) return;

        bool ignored = false;
        if (tryGetRecordedVisibility(records, node, ignored)) return;

        records.push_back({node, visible});
    }

    inline void snapshotVisibility(std::vector<VisibilityRecord>& records, cocos2d::CCNode* node) {
        if (!node) return;
        recordVisibility(records, node, node->isVisible());
    }

    inline void restoreVisibility(std::vector<VisibilityRecord> const& records) {
        for (auto const& record : records) {
            if (auto node = record.node.lock()) {
                node->setVisible(record.visible);
            }
        }
    }
}
