#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>

class PaimonLoadingOverlay;
#include <Geode/ui/LoadingSpinner.hpp>
#include "../services/ProfileMusicManager.hpp"
#include <vector>

/**
 * ProfileMusicPopup, UI para configurarel perfil
 */
class ProfileMusicPopup : public geode::Popup {
protected:
    // Datos
    int m_accountID;
    int m_songID = 0;
    int m_startMs = 0;
    int m_endMs = 20000;
    int m_songDurationMs = 0;
    std::string m_songName;
    std::string m_artistName;
    std::string m_previewPath;
    bool m_isCustomFile = false;
    std::string m_customFilePath;

    // UI Components
    cocos2d::CCMenu* m_mainMenu = nullptr;
    geode::TextInput* m_songIdInput = nullptr;
    cocos2d::CCLabelBMFont* m_songInfoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_durationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_selectionLabel = nullptr;
    cocos2d::CCNode* m_waveformContainer = nullptr;
    cocos2d::CCLayerColor* m_selectionOverlay = nullptr;
    cocos2d::CCNode* m_startHandle = nullptr;
    cocos2d::CCNode* m_endHandle = nullptr;
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;

    // Waveform data
    std::vector<float> m_peaks;
    std::vector<cocos2d::CCNode*> m_waveformBars;

    // Waveform dimensions
    float m_waveformX = 0;
    float m_waveformY = 0;
    float m_waveformWidth = 380.f;
    float m_waveformHeight = 60.f;

    // Dragging state
    bool m_isDraggingStart = false;
    bool m_isDraggingEnd = false;
    bool m_isDraggingSelection = false;
    float m_dragStartX = 0;
    int m_dragStartMs = 0;

    // Max fragment duration (20 seconds)
    static constexpr int MAX_FRAGMENT_MS = 20000;
    static constexpr int MIN_FRAGMENT_MS = 5000;

    bool init(int accountID);

    void onClose(cocos2d::CCObject*) override;

    // UI Creation
    void createSongIdInput();
    void createWaveformDisplay();
    void createControlButtons();

    // Actions
    void onLoadSong(cocos2d::CCObject*);
    void onLoadCustomFile(cocos2d::CCObject*);
    void onPlayPreview(cocos2d::CCObject*);
    void onStopPreview(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onDelete(cocos2d::CCObject*);
    void onDownloadSong(cocos2d::CCObject*);

    // Waveform
    void loadWaveform();
    void renderWaveform();
    void drawSelectionBars();
    void updateSelectionOverlay();
    void updateSelectionLabel();

    // Helpers
    int positionToMs(float x);
    float msToPosition(int ms);
    void clampSelection();
    cocos2d::CCNode* createHandleVisual(float height, cocos2d::ccColor3B color, bool isStart);
    void addSeparatorLine(float y);

    // Touch handling for waveform
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

    void showLoading();
    void hideLoading();
    void showError(std::string const& message);

public:
    static ProfileMusicPopup* create(int accountID);

    void loadExistingConfig();
};