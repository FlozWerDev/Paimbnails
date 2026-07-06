#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::menumusic {

class ExternalSongsPopup : public geode::Popup {
public:
    static ExternalSongsPopup* create();

protected:
    bool init(float width, float height);
    void onExit() override;

    void buildHeader();
    void buildList();
    void rebuildList();

    void playSongPath(const std::string& path);

    void onShuffleAll(cocos2d::CCObject*);
    void onSearchChanged(const std::string& query);
    void onPlayTapped(cocos2d::CCObject* sender);

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput* m_searchBar = nullptr;
    cocos2d::CCLabelBMFont* m_summaryLabel = nullptr;
    std::string m_query;

    struct Row {
        std::string path;
        std::string label;
        std::string source;
    };
    std::vector<Row> m_rows;
};

} // namespace paimon::menumusic
