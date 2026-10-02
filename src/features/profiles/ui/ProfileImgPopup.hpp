#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>


class ProfileImgPopup : public PaimonPopup {
protected:
    int m_accountID;
    geode::Ref<cocos2d::CCTexture2D> m_texture;
    cocos2d::CCClippingNode* m_imgClip = nullptr;

    bool init(int accountID, cocos2d::CCTexture2D* texture);

public:
    static ProfileImgPopup* create(int accountID, cocos2d::CCTexture2D* texture);
};

