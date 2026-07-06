#pragma once

#include <Geode/Geode.hpp>

class PaimonLoadingOverlay;
#include "../services/ProfileMusicManager.hpp"
#include <vector>

class ProfileMusicPopup : public geode::Popup {
protected:
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

    cocos2d::CCMenu* m_mainMenu = nullptr;
    geode::TextInput* m_songIdInput = nullptr;
    geode::TextInput* m_startTimeInput = nullptr;
    geode::TextInput* m_endTimeInput = nullptr;
    cocos2d::CCLabelBMFont* m_songInfoLabel = nullptr;
    cocos2d::CCLabelBMFont* m_durationLabel = nullptr;
    cocos2d::CCLabelBMFont* m_selectionLabel = nullptr;
    cocos2d::CCLabelBMFont* m_startConvLabel = nullptr;
    cocos2d::CCLabelBMFont* m_endConvLabel = nullptr;
    cocos2d::CCNode* m_waveformContainer = nullptr;

    // Guards so the two-way sync between the draggable selection and the
    // editable time inputs doesn't fight the user while they type.
    bool m_suppressTimeInput = false;
    bool m_editingTimeInput  = false;
    float m_timeEditorY = 0.f;
    cocos2d::CCLayerColor* m_selectionOverlay = nullptr;
    cocos2d::CCNode* m_startHandle = nullptr;
    cocos2d::CCNode* m_endHandle = nullptr;
    PaimonLoadingOverlay* m_loadingSpinner = nullptr;

    cocos2d::CCNode* m_playbackCursor = nullptr;
    bool m_isPreviewPlaying = false;
    bool m_cursorScheduled  = false;
    float m_cursorPulse = 0.f;

    std::vector<float> m_peaks;
    std::vector<cocos2d::CCNode*> m_waveformBars;

    float m_waveformX = 0;
    float m_waveformY = 0;
    float m_waveformWidth = 380.f;
    float m_waveformHeight = 60.f;

    bool m_isDraggingStart = false;
    bool m_isDraggingEnd = false;
    bool m_isDraggingSelection = false;
    float m_dragStartX = 0;
    int m_dragStartMs = 0;

    static constexpr int MAX_FRAGMENT_MS = 20000;
    static constexpr int MIN_FRAGMENT_MS = 5000;

    bool init(int accountID);

    void onClose(cocos2d::CCObject*) override;
    void onExit() override;

    void createSongIdInput();
    void createWaveformDisplay();
    void createTimeEditor();
    void createControlButtons();

    void onLoadSong(cocos2d::CCObject*);
    void onLoadCustomFile(cocos2d::CCObject*);
    void onPlayPreview(cocos2d::CCObject*);
    void onStopPreview(cocos2d::CCObject*);
    void onSave(cocos2d::CCObject*);
    void onDelete(cocos2d::CCObject*);
    void onDownloadSong(cocos2d::CCObject*);
    void onSearchSong(cocos2d::CCObject*);

    void loadWaveform();
    void renderWaveform();
    void drawSelectionBars();
    void updateSelectionOverlay();
    void updateSelectionLabel();

    void onNudgeTime(cocos2d::CCObject*);
    void onStartTimeChanged(std::string const& text);
    void onEndTimeChanged(std::string const& text);
    void applyStartMs(int newStartMs);
    void applyEndMs(int newEndMs);
    void syncTimeInputsFromSelection();
    void updateConversionLabels();

    void buildPlaybackCursor();
    void schedulePlaybackTracking();
    void unschedulePlaybackTracking();
    void updatePlaybackCursorPosition();
    void updatePlaybackCursor(float dt);

    int positionToMs(float x);
    float msToPosition(int ms);
    void clampSelection();
    cocos2d::CCNode* createHandleVisual(float height, cocos2d::ccColor3B color, bool isStart);
    void addSeparatorLine(float y);
    std::string formatSongInfoLine() const;

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