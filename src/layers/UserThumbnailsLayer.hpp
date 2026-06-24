#pragma once
#include <Geode/Geode.hpp>
#include <atomic>
#include <memory>

class UserThumbnailsLayer : public cocos2d::CCLayer {
protected:
    std::string m_username;
    int m_accountID;
    geode::ScrollLayer* m_scrollLayer = nullptr;
    cocos2d::CCMenu* m_levelListMenu = nullptr;
    cocos2d::CCLabelBMFont* m_titleLabel = nullptr;
    cocos2d::CCLabelBMFont* m_loadingLabel = nullptr;
    cocos2d::CCLabelBMFont* m_errorLabel = nullptr;
    std::shared_ptr<std::atomic<bool>> m_requestAlive;

    bool init(std::string const& username, int accountID);
    void onExit() override;
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
