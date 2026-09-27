#pragma once

#include "../GifImportTypes.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class ButtonSprite;
class PaimonLoadingOverlay;

namespace paimon::gifimport {

struct ProcessingProgress;
struct SourceLoadState;

class GifImportPopup : public geode::Popup {
public:
    static GifImportPopup* create();
    ~GifImportPopup();

private:
    bool init() override;
    void cancelSourceLoad();

    void pickSource();
    void loadSource(std::filesystem::path const& path);
    void loadAnimated(
        std::filesystem::path const& path,
        std::shared_ptr<std::vector<std::uint8_t>> bytes);
    void loadStill(
        std::filesystem::path const& path,
        std::shared_ptr<std::vector<std::uint8_t>> bytes);
    void loadVideo(std::filesystem::path const& path);
    void applySource(std::filesystem::path const& path, std::shared_ptr<SourceAnimation> source);
    void requestProcess();
    void startProcess();
    void applyProcessed(BuildResult result);
    void importObjects();
    void runBackground();

    void adjustResolution(int direction);
    void adjustColors(int direction);
    void adjustBudget(int direction);
    void adjustFrames(int direction);
    void adjustPixelSize(int direction);
    void adjustTolerance(int direction);
    void toggleMode();
    void toggleBackground();
    void toggleSampling();
    void toggleDither();
    void toggleLoop();
    void toggleGlow();

    void loadOptions();
    void saveOptions() const;
    void refreshControls();
    void pollSourceLoad();
    void pollProcessing();
    void pollPreview();
    void displayPixels(std::vector<std::uint8_t> const& pixels, int width, int height, bool alias);
    void displaySource();
    void refreshProgress();
    void refreshPreview();
    void tick(float dt);
    void showBusy(std::string const& text);
    void hideBusy();
    void onSpawnFailed();

    Options m_options;
    std::filesystem::path m_path;
    std::shared_ptr<SourceAnimation> m_source;
    std::shared_ptr<SourceAnimation> m_scaled;
    std::shared_ptr<ImportPlan> m_plan;
    std::shared_ptr<SourceLoadState> m_sourceLoad;
    std::shared_ptr<ProcessingProgress> m_progress;
    bool m_processing = false;
    bool m_reprocess = false;
    int m_scaledFor = 0;
    float m_scaledBlur = -1.f;
    int m_previewFrame = 0;
    float m_previewElapsed = 0.f;
    std::uint64_t m_previewVersion = 0;

    cocos2d::CCLabelBMFont* m_fileLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_resolutionValue = nullptr;
    cocos2d::CCLabelBMFont* m_colorsValue = nullptr;
    cocos2d::CCLabelBMFont* m_budgetValue = nullptr;
    cocos2d::CCLabelBMFont* m_framesValue = nullptr;
    cocos2d::CCLabelBMFont* m_pixelSizeValue = nullptr;
    cocos2d::CCLabelBMFont* m_backgroundValue = nullptr;
    cocos2d::CCLabelBMFont* m_toleranceValue = nullptr;
    cocos2d::CCLayerColor* m_progressTrack = nullptr;
    cocos2d::CCLayerColor* m_progressFill = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    ButtonSprite* m_modeSprite = nullptr;
    ButtonSprite* m_samplingSprite = nullptr;
    ButtonSprite* m_ditherSprite = nullptr;
    ButtonSprite* m_loopSprite = nullptr;
    ButtonSprite* m_glowSprite = nullptr;
    PaimonLoadingOverlay* m_busyOverlay = nullptr;
};

} // namespace paimon::gifimport
