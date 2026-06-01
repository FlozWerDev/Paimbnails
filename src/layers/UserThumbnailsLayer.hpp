#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>

/**
 * UserThumbnailsLayer - Displays all levels where a user has uploaded thumbnails
 */
class UserThumbnailsLayer : public cocos2d::CCLayer {
protected:
    std::string m_username;
    int m_accountID;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_levelListMenu = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_loadingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_errorLabel = nullptr;
    
    bool init(std::string const& username, int accountID);
    void keyBackClicked() override;
    
    void loadUserThumbnails();
    void displayLevels(std::vector<int> const& levelIds);
    void showError(std::string const& message);
    void onBack(cocos2d::CCObject*);
    void onLevelClicked(cocos2d::CCObject* sender);
    
public:
    static UserThumbnailsLayer* create(std::string const& username, int accountID);
    static cocos2d::CCScene* scene(std::string const& username, int accountID);
};

// Made with Bob
