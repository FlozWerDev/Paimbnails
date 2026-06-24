#pragma once

#include <Geode/Geode.hpp>
#include <vector>
#include <unordered_set>

namespace paimon::capture {
    // Nodes user-set VISIBLE in layer editor; capture must not hide these.
    inline std::unordered_set<cocos2d::CCNode*>& userShownNodes() {
        static auto& s = *new std::unordered_set<cocos2d::CCNode*>();
        return s;
    }

    inline void setUserShown(cocos2d::CCNode* node, bool shown) {
        if (!node) return;
        if (shown) userShownNodes().insert(node);
        else       userShownNodes().erase(node);
    }

    inline bool isUserShown(cocos2d::CCNode* node) {
        return node && userShownNodes().count(node) > 0;
    }

    inline void clearUserShown() {
        userShownNodes().clear();
    }

    struct VisibilityRecord {
        cocos2d::CCNode* node = nullptr;
        bool visible = true;
    };

    inline bool tryGetRecordedVisibility(std::vector<VisibilityRecord> const& records, cocos2d::CCNode* node, bool& outVisible) {
        if (!node) return false;

        for (auto const& record : records) {
            if (record.node == node) {
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

    inline void hideTemporarily(std::vector<VisibilityRecord>& hiddenRecords, cocos2d::CCNode* node) {
        if (!node) return;
        snapshotVisibility(hiddenRecords, node);
        node->setVisible(false);
    }

    inline void restoreVisibility(std::vector<VisibilityRecord> const& records) {
        for (auto const& record : records) {
            if (record.node) record.node->setVisible(record.visible);
        }
    }
}
