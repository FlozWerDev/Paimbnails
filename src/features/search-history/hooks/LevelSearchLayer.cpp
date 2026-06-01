#include "../SearchHistory.hpp"
#include "../ui/SearchHistoryPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/modify/LevelSearchLayer.hpp>
#include <vector>

using namespace geode::prelude;

// Hook separado del de Paimbnails (src/hooks/LevelSearchLayer.cpp). Geode
// encadena ambos $modify sobre la misma clase sin conflicto.
class $modify(PaimonSearchHistoryLayer, LevelSearchLayer) {
    static void onModify(auto& self) {
        // Correr init despues de que node-ids asigne "other-filter-menu".
        (void)self.setHookPriorityAfterPost("LevelSearchLayer::init", "geode.node-ids");
    }

    static bool incognito() {
        return Mod::get()->getSettingValue<bool>("incognito-mode");
    }

    $override
    bool init(int type) {
        if (!LevelSearchLayer::init(type)) return false;

        // En incognito no mostramos el boton ni guardamos nada.
        if (!incognito()) {
            if (auto menu = this->getChildByID("search-button-menu")) {
                // Usar el mismo estilo cuadrado que rate-profile-btn (GJ_button_04.png)
                auto bg = CCScale9Sprite::create("GJ_button_04.png");
                if (!bg) bg = CCScale9Sprite::create("GJ_button_01.png");
                if (bg) {
                    bg->setContentSize({30.f, 30.f});
                    
                    // Icono de reloj para historial
                    auto clockIcon = CCSprite::createWithSpriteFrameName("GJ_timeIcon_001.png");
                    if (clockIcon) {
                        // Escalar el icono para que quepa bien en el boton cuadrado
                        float targetSize = 18.f;
                        float iconSize = clockIcon->getContentSize().width;
                        if (iconSize > 0.f) {
                            clockIcon->setScale(targetSize / iconSize);
                        }
                        clockIcon->setPosition({15.f, 15.f});
                        bg->addChild(clockIcon);
                    }
                    
                    auto btn = CCMenuItemSpriteExtra::create(
                        bg, this, menu_selector(PaimonSearchHistoryLayer::onHistory));
                    btn->setID("search-history-button"_spr);
                    btn->setPosition({ -200.f, 130.f });
                    menu->addChild(btn);
                }
            }
        }
        return true;
    }

    void onHistory(CCObject*) {
        SearchHistoryPopup::create([this](int index) {
            auto& history = paimon::searchhistory::history;
            if (index < 0 || index >= (int)history.size()) return;
            auto& object = history[index];
            auto glm = GameLevelManager::get();

            if (object.type == 0) {
                glm->setBoolForKey(object.uncompleted, "uncompleted_filter");
                glm->setBoolForKey(object.completed, "completed_filter");
                glm->setBoolForKey(object.original, "original_filter");
                glm->setBoolForKey(object.coins, "coin_filter");
                glm->setBoolForKey(object.twoPlayer, "twoP_filter");
                glm->setBoolForKey(object.song, "enable_songFilter");
                glm->setBoolForKey(object.noStar, "nostar_filter");
                glm->setBoolForKey(object.featured, "featured_filter");
                glm->setBoolForKey(object.epic, "epic_filter");
                // Nota: GD intercambia estas dos claves internamente.
                glm->setBoolForKey(object.mythic, "legendary_filter");
                glm->setBoolForKey(object.legendary, "mythic_filter");
                glm->setBoolForKey(object.customSong, "customsong_filter");
                glm->setIntForKey(object.songID, "song_filter");
            }

            if (object.type == 0 || object.type == 1) {
                if (glm->getBoolForKey("star_filter") != object.star) this->toggleStar(nullptr);
                for (auto diff : object.difficulties) {
                    if (diff > -4 && diff < 6 && diff != 0) {
                        this->toggleDifficultyNum(diff == -1 ? 0 : diff == -2 ? 6 : diff == -3 ? -1 : diff, true);
                    }
                }
                bool demonToggled = this->checkDiff(6);
                if (m_demonTypeButton) {
                    m_demonTypeButton->setEnabled(demonToggled);
                    m_demonTypeButton->setVisible(demonToggled);
                }
                if (object.type == 0) {
                    for (auto len : object.lengths) {
                        if (len > -1 && len < 6) this->toggleTimeNum(len, true);
                    }
                }
                this->demonFilterSelectClosed(object.demonFilter);
            }

            m_searchInput->setString(object.query);
        })->show();
    }

    $override
    void onSearch(CCObject* sender) {
        LevelSearchLayer::onSearch(sender);
        if (incognito()) return;

        std::vector<int> difficulties;
        if (this->checkDiff(0)) difficulties.push_back(-1);       // auto
        else if (this->checkDiff(6)) difficulties.push_back(-2);  // demon
        else if (this->checkDiff(7)) difficulties.push_back(-3);  // n/a
        else {
            for (int i = 1; i < 6; ++i) {
                if (this->checkDiff(i)) difficulties.push_back(i);
            }
        }

        std::vector<int> lengths;
        for (int i = 0; i < 6; ++i) {
            if (this->checkTime(i)) lengths.push_back(i);
        }

        paimon::searchhistory::add(
            this->getSearchObject(SearchType::Search, m_searchInput->getString()),
            std::move(difficulties), std::move(lengths), m_type);
    }

    $override
    void onSearchUser(CCObject* sender) {
        LevelSearchLayer::onSearchUser(sender);
        if (incognito()) return;
        paimon::searchhistory::add(
            this->getSearchObject(SearchType::Users, m_searchInput->getString()), {}, {}, 2);
    }
};
