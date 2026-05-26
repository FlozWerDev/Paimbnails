#pragma once

#include <Geode/Geode.hpp>
#include <vector>

namespace paimon::capture {
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
