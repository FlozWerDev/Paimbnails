#pragma once

#include "ProfileLevelCatalog.hpp"
#include "ProfileScrollLayer.hpp"
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

namespace paimon::profile_redesign {

class ProfileLevelsPanel : public cocos2d::CCNode {
public:
    static ProfileLevelsPanel* create(int userID, cocos2d::CCSize size);
    void setActive(bool active);
    void onEnter() override;
    void onExit() override;

private:
    bool initialize(int userID, cocos2d::CCSize size);
    void cancelLoading();
    void requestNextPage();
    void retryLoading();
    void acceptPage(bool ok, std::string response);
    void renderPage();
    void showStatus(std::string text);
    void showError(std::string message = {});
    void changePage(int direction);

    int m_userID = 0;
    bool m_active = false, m_loading = false;
    std::uint64_t m_generation = 0;
    std::size_t m_page = 0;
    ProfileLevelCatalog m_catalog;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
    ProfileScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_pageLabel = nullptr;
    CCMenuItemSpriteExtra* m_previous = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    CCMenuItemSpriteExtra* m_retry = nullptr;
    std::vector<geode::Ref<GJGameLevel>> m_visibleLevels;
};

} // namespace paimon::profile_redesign
