#pragma once

// Track chooser for the editor music panel. Reads the same library the menu
// music player uses, so anything imported or downloaded there shows up here.

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <string>
#include <vector>

namespace geode {
class ScrollLayer;
class TextInput;
}

namespace paimon::editormusic {

class EditorMusicPickerPopup : public geode::Popup {
public:
    static EditorMusicPickerPopup* create();

private:
    bool init() override;
    void rebuild();
    void scheduleRebuild();

    cocos2d::CCNode* trackRow(float width, std::string const& trackId);
    std::vector<std::string> filteredTracks() const;

    void syncDownloads();
    void importFolder();

    std::string m_search;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
};

} // namespace paimon::editormusic
