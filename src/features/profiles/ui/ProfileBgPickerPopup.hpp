#pragma once
#include <Geode/Geode.hpp>

class ProfileBgPickerPopup : public geode::Popup {
public:
    using SimpleCallback = geode::CopyableFunction<void()>;

    static ProfileBgPickerPopup* create(int accountID);

    void setOnPickMedia(SimpleCallback cb)      { m_onPickMedia      = std::move(cb); }
    void setOnPickGradient(SimpleCallback cb)   { m_onPickGradient   = std::move(cb); }
    void setOnPickVideoAudio(SimpleCallback cb) { m_onPickVideoAudio = std::move(cb); }
    void setOnPickReset(SimpleCallback cb)      { m_onPickReset      = std::move(cb); }

protected:
    int m_accountID = 0;
    SimpleCallback m_onPickMedia;
    SimpleCallback m_onPickGradient;
    SimpleCallback m_onPickVideoAudio;
    SimpleCallback m_onPickReset;

    bool init(int accountID);

    void onPickMedia(cocos2d::CCObject*);
    void onPickGradient(cocos2d::CCObject*);
    void onPickVideoAudio(cocos2d::CCObject*);
    void onPickReset(cocos2d::CCObject*);
    void onInfo(cocos2d::CCObject*);
};
