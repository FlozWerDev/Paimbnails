#pragma once
#include "../../../ui/PaimonPopup.hpp"

// every public detail of a profile in one list: ids, stats, icons, privacy and socials from
// the score, plus the creation date and old names from gdhistory. tapping a row copies it.

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include "../services/GDHistoryClient.hpp"
#include <string>
#include <vector>

class GJUserScore;

namespace paimon::info {

class ProfileInfoPopup : public PaimonPopup {
public:
    // presence is the already known online/last seen text, empty when the page has none yet.
    static ProfileInfoPopup* create(GJUserScore* score, std::string presence = {});

protected:
    enum class RowKind { Section, Field, HistoryPrompt };

    struct Row {
        RowKind kind = RowKind::Field;
        std::string label;
        std::string value;
        bool accent = false;
    };

    enum class HistoryState { Idle, Loading, Ready, Missing, Unavailable };

    bool init(GJUserScore* score, std::string presence);

    // rows that never change, split around the spot where the history rows go.
    void collectRows(GJUserScore* score, std::string const& presence);
    std::vector<Row> historyRows() const;
    void rebuildRows();
    void queueRebuild();
    void onRebuildTick(float dt);

    void requestHistory(bool userInitiated);
    void applyHistory(gdhistory::UserHistory const& history, bool ok);

    std::string plainText() const;

    void onRow(cocos2d::CCObject* sender);
    void onCopyIDs();
    void onCopyAll();

    int m_userID = 0;
    int m_accountID = 0;
    std::string m_userName;

    std::vector<Row> m_head;
    std::vector<Row> m_tail;
    std::vector<Row> m_rows;

    gdhistory::UserHistory m_history;
    HistoryState m_historyState = HistoryState::Idle;

    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCNode* m_listBg = nullptr;
};

} // namespace paimon::info
