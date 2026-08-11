#pragma once

#include <Geode/Geode.hpp>
#include <filesystem>
#include <memory>
#include "../../../utils/GIFDecoder.hpp"

class PaimonLoadingOverlay;

namespace paimon::dev {

// Dev tool: converts an animated GIF into a grid spritesheet PNG plus a JSON
// (frameW/frameH/cols/rows/count/delaysMs) compatible with SheetAnimSprite.
class GifToSheetPopup : public geode::Popup {
public:
    static GifToSheetPopup* create();

protected:
    ~GifToSheetPopup() override;
    bool init() override;

    void onPickGif();
    void onExport();
    void loadGif(std::filesystem::path const& path);
    void applyDecoded(std::filesystem::path const& path, std::shared_ptr<GIFDecoder::GIFData> gif);
    void exportTo(std::filesystem::path pngPath);
    void refreshInfo();
    int currentCols() const;
    void showBusy(std::string const& text);
    void hideBusy();

    std::filesystem::path m_gifPath;
    std::shared_ptr<GIFDecoder::GIFData> m_gif;
    int m_autoCols = 1;

    cocos2d::CCLabelBMFont* m_fileLabel = nullptr;
    cocos2d::CCLabelBMFont* m_infoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_previewHint = nullptr;
    geode::TextInput* m_colsInput = nullptr;
    cocos2d::CCNode* m_previewBox = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    PaimonLoadingOverlay* m_busyOverlay = nullptr;
};

} // namespace paimon::dev
