#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>
#include <Geode/binding/LevelCell.hpp>
#include "../framework/state/SessionState.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/ui/LevelCellSettingsPopup.hpp"
#include "../features/backgrounds/services/LayerBackgroundManager.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/HttpClient.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../managers/ThumbnailAPI.hpp"
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace {
struct LevelCellScanResult {
    std::vector<int> orderedLevelIDs;
    std::unordered_set<int> visibleLevelIDs;
    std::unordered_set<int> seen;
};

void collectLevelCellIDs(CCNode* node, LevelCellScanResult& result) {
    if (!node || !node->isVisible()) {
        if (!node) {
            return;
        }
    }

    if (auto* levelCell = typeinfo_cast<LevelCell*>(node)) {
        auto* level = levelCell->m_level;
        if (level) {
            int levelID = level->m_levelID.value();
            if (levelID > 0 && result.seen.insert(levelID).second) {
                result.orderedLevelIDs.push_back(levelID);
            }
            if (levelID > 0 && node->isVisible()) {
                result.visibleLevelIDs.insert(levelID);
            }
        }
    }

    auto* children = node->getChildren();
    if (!children) {
        return;
    }

    for (auto* child : CCArrayExt<CCNode*>(children)) {
        collectLevelCellIDs(child, result);
    }
}

std::vector<int> buildPredictiveWindow(LevelCellScanResult const& scan, size_t lookBehind, size_t lookAhead) {
    std::vector<int> predictive;
    if (scan.orderedLevelIDs.empty() || scan.visibleLevelIDs.empty()) {
        return predictive;
    }

    size_t firstVisibleIndex = scan.orderedLevelIDs.size();
    size_t lastVisibleIndex = 0;

    for (size_t index = 0; index < scan.orderedLevelIDs.size(); ++index) {
        if (!scan.visibleLevelIDs.contains(scan.orderedLevelIDs[index])) {
            continue;
        }
        firstVisibleIndex = std::min(firstVisibleIndex, index);
        lastVisibleIndex = std::max(lastVisibleIndex, index);
    }

    if (firstVisibleIndex == scan.orderedLevelIDs.size()) {
        return predictive;
    }

    size_t windowStart = firstVisibleIndex > lookBehind ? firstVisibleIndex - lookBehind : 0;
    size_t windowEnd = std::min(scan.orderedLevelIDs.size(), lastVisibleIndex + lookAhead + 1);

    predictive.reserve(windowEnd - windowStart);
    for (size_t index = windowStart; index < windowEnd; ++index) {
        int levelID = scan.orderedLevelIDs[index];
        if (scan.visibleLevelIDs.contains(levelID)) {
            continue;
        }
        predictive.push_back(levelID);
    }

    return predictive;
}
}

class $modify(PaimonLevelListLayer, LevelListLayer) {
    static void onModify(auto& self) {
        // Captura el ID de lista antes de init
        (void)self.setHookPriorityPre("LevelListLayer::init", geode::Priority::Normal);
    }

    $override
    bool init(GJLevelList* list) {
        // Guarda el ID de lista
        if (list) {
            paimon::SessionState::get().currentListID = list->m_listID;
            log::debug("Entered List: {}", list->m_listID);
        } else {
            paimon::SessionState::get().currentListID = 0;
        }

        return LevelListLayer::init(list);
    }
};

class $modify(ContextTrackingBrowser, LevelBrowserLayer) {
    static void onModify(auto& self) {
        // Corre despues de geode.node-ids
        (void)self.setHookPriorityAfterPost("LevelBrowserLayer::init", "geode.node-ids");
    }

    struct Fields {
        // IDs con manifest ya solicitado
        std::unordered_set<int> m_manifestFetchedIds;
    };

    $override
    bool init(GJSearchObject* p0) {
        // Limpia el contexto al entrar a busqueda
        paimon::SessionState::get().currentListID = 0;
        if (!LevelBrowserLayer::init(p0)) return false;

        // Aplica fondo custom
        LayerBackgroundManager::get().applyBackground(this, "browser");

        // Boton de settings
        addSettingsGearButton();

        // Boton de refrescar thumbnails
        addRefreshButton();

        this->schedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells), 1.0f);
        this->prefetchVisibleLevelCells(0.0f);

        return true;
    }

    $override
    void onExit() {
        this->unschedule(schedule_selector(ContextTrackingBrowser::prefetchVisibleLevelCells));
        LevelBrowserLayer::onExit();
    }

    CCMenuItemSpriteExtra* createGearButton(float sprScale) {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_optionsBtn02_001.png");
        if (!spr) return nullptr;
        spr->setScale(sprScale);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ContextTrackingBrowser::onLevelCellSettings));
        btn->setID("paimon-levelcell-settings-btn"_spr);
        return btn;
    }

    void addSettingsGearButton() {
        CCMenu* searchMenu = nullptr;
        if (auto node = this->getChildByID("search-menu")) {
            searchMenu = typeinfo_cast<CCMenu*>(node);
        }

        // Busca search-menu entre los menus hijos
        if (!searchMenu) {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                    if (menu->getPosition().y > CCDirector::sharedDirector()->getWinSize().height * 0.7f) {
                        searchMenu = menu;
                        break;
                    }
                }
            }
        }

        if (!searchMenu) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto gearMenu = CCMenu::create();
            gearMenu->setPosition({0, 0});
            gearMenu->setID("paimon-levelcell-settings-menu"_spr);
            this->addChild(gearMenu, 100);

            if (auto btn = createGearButton(0.45f)) {
                btn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
                gearMenu->addChild(btn);
            }
            return;
        }

        auto gearBtn = createGearButton(0.5f);
        if (!gearBtn) return;

        if (searchMenu->getLayout()) {
            searchMenu->addChild(gearBtn);
            searchMenu->updateLayout();
        } else {
            float rightMostX = 0.f;
            if (auto children = searchMenu->getChildren()) {
                for (auto* child : CCArrayExt<CCNode*>(children)) {
                    float r = child->getPositionX() + child->getContentSize().width * child->getScaleX() * 0.5f;
                    if (r > rightMostX) rightMostX = r;
                }
            }
            gearBtn->setPosition({rightMostX + 25.f, searchMenu->getContentSize().height / 2.f});
            searchMenu->addChild(gearBtn);
        }
    }

    void onLevelCellSettings(CCObject*) {
        auto popup = LevelCellSettingsPopup::create();
        if (!popup) return;

        popup->setOnSettingsChanged([]() {
            log::info("[LevelBrowserLayer] LevelCell settings changed, will apply on next cell load");
        });

        popup->show();
    }

    void addRefreshButton() {
        auto spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_updateBtn_001.png");
        if (!spr) spr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_replayBtn_001.png");
        if (!spr) return;
        spr->setScale(0.7f);

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(ContextTrackingBrowser::onRefreshThumbnails));
        btn->setID("paimon-refresh-thumbs-btn"_spr);

        // Busca search-menu como en addSettingsGearButton
        CCMenu* searchMenu = nullptr;
        if (auto node = this->getChildByID("search-menu")) {
            searchMenu = typeinfo_cast<CCMenu*>(node);
        }
        if (!searchMenu) {
            for (auto* child : CCArrayExt<CCNode*>(this->getChildren())) {
                if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                    if (menu->getPosition().y > CCDirector::sharedDirector()->getWinSize().height * 0.7f) {
                        searchMenu = menu;
                        break;
                    }
                }
            }
        }

        if (searchMenu) {
            if (searchMenu->getLayout()) {
                searchMenu->addChild(btn);
                searchMenu->updateLayout();
            } else {
                float rightMostX = 0.f;
                if (auto children = searchMenu->getChildren()) {
                    for (auto* child : CCArrayExt<CCNode*>(children)) {
                        float r = child->getPositionX() + child->getContentSize().width * child->getScaleX() * 0.5f;
                        if (r > rightMostX) rightMostX = r;
                    }
                }
                btn->setPosition({rightMostX + 25.f, searchMenu->getContentSize().height / 2.f});
                searchMenu->addChild(btn);
            }
        } else {
            // Menu propio en esquina superior derecha
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            auto menu = CCMenu::create();
            menu->setPosition({0, 0});
            menu->setID("paimon-refresh-menu"_spr);
            btn->setPosition({winSize.width - 30.f, winSize.height - 30.f});
            menu->addChild(btn);
            this->addChild(menu, 100);
        }
    }

    void onRefreshThumbnails(CCObject*) {
        LevelCellScanResult scan;
        collectLevelCellIDs(this, scan);

        if (scan.orderedLevelIDs.empty()) {
            PaimonNotify::create("No thumbnails to refresh", geode::NotificationIcon::Warning, 2.f)->show();
            return;
        }

        auto& loader = ThumbnailLoader::get();

        // Invalida cada nivel de la lista
        for (int levelID : scan.orderedLevelIDs) {
            loader.invalidateLevel(levelID, false);
            loader.invalidateLevel(levelID, true);
        }

        // Limpia tracking de manifest
        for (int levelID : scan.orderedLevelIDs) {
            m_fields->m_manifestFetchedIds.erase(levelID);
        }

        // Re-solicita manifest y re-descarga
        HttpClient::get().fetchManifest(scan.orderedLevelIDs, nullptr);
        loader.prefetchLevels(scan.orderedLevelIDs, ThumbnailLoader::PriorityVisibleCell);

        auto msg = fmt::format("Refreshing {} thumbnails...", scan.orderedLevelIDs.size());
        PaimonNotify::create(msg, geode::NotificationIcon::Info, 2.f)->show();
        log::info("[LevelBrowserLayer] Refreshing {} thumbnails", scan.orderedLevelIDs.size());
    }

    void prefetchVisibleLevelCells(float) {
        LevelCellScanResult scan;
        collectLevelCellIDs(this, scan);

        auto& visibleLevelIDs = scan.visibleLevelIDs;
        std::vector<int> levelIDs;
        levelIDs.reserve(visibleLevelIDs.size());
        for (int levelID : scan.orderedLevelIDs) {
            if (visibleLevelIDs.contains(levelID)) {
                levelIDs.push_back(levelID);
            }
        }

        if (levelIDs.empty()) {
            return;
        }

        auto predictiveIDs = buildPredictiveWindow(scan, 2, 5);

        // Colecta IDs sin manifest para prefetch
        std::vector<int> newManifestIds;
        for (int id : levelIDs) {
            if (m_fields->m_manifestFetchedIds.find(id) == m_fields->m_manifestFetchedIds.end()) {
                newManifestIds.push_back(id);
            }
        }
        for (int id : predictiveIDs) {
            if (m_fields->m_manifestFetchedIds.find(id) == m_fields->m_manifestFetchedIds.end()) {
                newManifestIds.push_back(id);
            }
        }

        if (!newManifestIds.empty()) {
            // Marca como solicitado para evitar duplicados
            for (int id : newManifestIds) {
                m_fields->m_manifestFetchedIds.insert(id);
            }

            // Solicita manifest una sola vez para todos los visibles
            HttpClient::get().fetchManifest(newManifestIds, nullptr);
        }

        ThumbnailLoader::get().prefetchLevels(levelIDs, ThumbnailLoader::PriorityVisiblePrefetch);
        if (!predictiveIDs.empty()) {
            ThumbnailLoader::get().prefetchLevels(predictiveIDs, ThumbnailLoader::PriorityPredictivePrefetch);
        }

        // Precarga URL base del thumbnail para que LevelInfoLayer tenga RAM cache hit
        // instantáneo al abrirse, sin esperar la descarga desde la red.
        for (int levelID : levelIDs) {
            std::string url = ThumbnailAPI::get().getThumbnailURL(levelID);
            if (!url.empty()) {
                ThumbnailLoader::get().requestUrlLoad(url, [](cocos2d::CCTexture2D*, bool){}, ThumbnailLoader::PriorityVisiblePrefetch);
            }
        }
    }

};

// NOTE: limpieza de current-list-id esta en MenuLayer::init
