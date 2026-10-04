#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <array>
#include <string>
#include <vector>

class PaimonLoadingOverlay;
#include "../../../utils/HttpClient.hpp"
#include "../../editor-suite/EditorPopupKit.hpp"

class ProfileReviewsPopup : public PaimonPopup {
protected:
    struct Review {
        std::string username;
        std::string message;
        float stars = 0.f;
        int bucket = 0;
        int order = 0;
        bool own = false;
    };

    int m_accountID = 0;
    uint64_t m_requestGeneration = 0;
    cocos2d::CCLabelBMFont* m_averageLabel = nullptr;
    cocos2d::CCLabelBMFont* m_countLabel = nullptr;
    cocos2d::CCLabelBMFont* m_shownLabel = nullptr;
    cocos2d::CCNode* m_averageStars = nullptr;
    std::array<cocos2d::CCLayerColor*, 5> m_barFills{};
    std::array<cocos2d::CCLabelBMFont*, 5> m_barCounts{};
    geode::ScrollLayer* m_scrollView = nullptr;
    cocos2d::CCNode* m_scrollbar = nullptr;
    cocos2d::CCLabelBMFont* m_emptyLabel = nullptr;
    PaimonLoadingOverlay* m_spinner = nullptr;

    std::vector<Review> m_reviews;
    std::vector<paimon::editor::kit::Pill> m_filterPills;
    paimon::editor::kit::Pill m_sortPill;
    int m_filter = 0;
    int m_sort = 0;

    bool init(int accountID);
    void buildSummary();
    void buildToolbar();
    void loadReviews();
    void applyResponse(float average, int count, matjson::Value const& reviews);
    void updateSummary(float average, int count);
    void rebuildList();
    void refreshToolbar();
    cocos2d::CCNode* createReviewCell(Review const& review, float width, int index);
    void animateReviewCells(std::vector<cocos2d::CCNode*> const& cells);

public:
    static ProfileReviewsPopup* create(int accountID);
};
