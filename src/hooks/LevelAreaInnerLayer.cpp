#include <Geode/modify/LevelAreaInnerLayer.hpp>
#include "../utils/DynamicPopupRegistry.hpp"
#include <Geode/modify/FLAlertLayer.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include "../utils/PaimonNotification.hpp"
#include "../utils/PaimonLoadingOverlay.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../utils/SpriteHelper.hpp"

using namespace geode::prelude;

class SimpleThumbnailPopup : public geode::Popup {
protected:
    bool init(CCTexture2D* tex, std::string const& title) {
        if (!Popup::init(400.f, 280.f)) return false;

        this->setTitle(title.c_str());
        
        auto contentSize = this->m_mainLayer->getContentSize();

        auto spr = CCSprite::createWithTexture(tex);
        if (spr) {
            float maxWidth = 340.f;
            float maxHeight = 220.f; // Espacio para titulo y botones
            
            float scaleX = maxWidth / spr->getContentWidth();
            float scaleY = maxHeight / spr->getContentHeight();
            float scale = std::min(scaleX, scaleY);
            if (scale > 1.0f) scale = 1.0f; 
            
            spr->setScale(scale);
            spr->setPosition(contentSize / 2);
            this->m_mainLayer->addChild(spr);
        }
        
        this->setZOrder(10500);
        this->setID("simple-thumbnail-popup"_spr);
        paimon::markDynamicPopup(this);
        return true;
    }
    
public:
    static SimpleThumbnailPopup* create(CCTexture2D* tex, std::string const& title) {
        auto ret = new SimpleThumbnailPopup();
        if (ret && ret->init(tex, title)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};

class $modify(PaimonLevelAreaInnerLayer, LevelAreaInnerLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelAreaInnerLayer::init", geode::Priority::Late);
    }

    struct Fields {
        std::unordered_map<int, Ref<CCSprite>> m_doorThumbnails;
        bool m_thumbnailsAdded = false;
    };

    $override
    bool init(bool returning) {
        log::debug("[LevelAreaInnerLayer] init() called with returning={}", returning);

        if (!LevelAreaInnerLayer::init(returning)) {
            return false;
        }

        log::debug("[LevelAreaInnerLayer] Init successful, scheduling thumbnail addition");

        // Espera a que las puertas existan
        this->scheduleOnce(schedule_selector(PaimonLevelAreaInnerLayer::addThumbnailsToDoors), 0.1f);

        return true;
    }

    void addThumbnailsToDoors(float dt) {
        auto fields = m_fields.self();
        if (fields->m_thumbnailsAdded) return;
        fields->m_thumbnailsAdded = true;

        log::debug("[LevelAreaInnerLayer] Adding thumbnails to main level doors");

        // Niveles main del 1 al 21
        std::vector<int> mainLevelIDs;
        for (int i = 1; i <= 21; i++) {
            mainLevelIDs.push_back(i);
        }

        int addedCount = 0;
        for (int levelID : mainLevelIDs) {
            auto doorNode = this->findDoorForLevel(levelID);
            if (doorNode) {
                this->addThumbnailToDoor(doorNode, levelID);
                addedCount++;
            }
        }

        log::info("[LevelAreaInnerLayer] Added {} thumbnails to doors", addedCount);
    }

    CCNode* findDoorForLevel(int levelID) {
        // Busca las puertas
        auto children = CCArrayExt<CCNode*>(this->getChildren());
        
        for (auto child : children) {
            if (auto menu = typeinfo_cast<CCMenu*>(child)) {
                auto menuChildren = CCArrayExt<CCNode*>(menu->getChildren());
                for (auto menuChild : menuChildren) {
                    if (auto menuItem = typeinfo_cast<CCMenuItemSpriteExtra*>(menuChild)) {
                        int doorTag = menuItem->getTag();
                        if (doorTag == levelID || doorTag == (1000 + levelID)) {
                            return menuItem;
                        }
                    }
                }
            }
        }
        
        return nullptr;
    }

    void addThumbnailToDoor(CCNode* doorNode, int levelID) {
        if (!doorNode) return;

        auto fields = m_fields.self();
        if (fields->m_doorThumbnails.find(levelID) != fields->m_doorThumbnails.end()) {
            return;
        }

        log::info("[LevelAreaInnerLayer] Adding thumbnail for level {}", levelID);

        // Helper para construir y montar el sprite — reusable entre fast-path
        // (RAM cache hit, sincronico) y slow-path (requestLoad async).
        auto applyThumbnail = [doorNode, levelID](
            CCNode* layer, CCTexture2D* tex, std::unordered_map<int, Ref<CCSprite>>& thumbsMap
        ) {
            if (!tex || !doorNode) return;
            // Si la celda ya tiene un thumbnail (porque llegaron RAM hit y
            // requestLoad casi al mismo tiempo), no duplicar.
            if (thumbsMap.find(levelID) != thumbsMap.end()) return;

            auto thumbSprite = CCSprite::createWithTexture(tex);
            if (!thumbSprite) return;

            auto doorSize = doorNode->getContentSize();
            float scale = std::min(
                (doorSize.width * 0.8f) / thumbSprite->getContentWidth(),
                (doorSize.height * 0.8f) / thumbSprite->getContentHeight()
            );
            thumbSprite->setScale(scale);
            thumbSprite->setPosition(doorSize / 2);
            thumbSprite->setZOrder(-1);
            thumbSprite->setOpacity(180);
            doorNode->addChild(thumbSprite);

            thumbsMap[levelID] = thumbSprite;
        };

        // Fast path sincronico: si esta en RAM (precargado al iniciar el mod),
        // aplicar en el mismo frame sin pasar por la cola.
        if (auto* cached = ThumbnailLoader::get().tryGetCachedTexture(levelID, false)) {
            applyThumbnail(this, cached, fields->m_doorThumbnails);
            log::debug("[LevelAreaInnerLayer] Thumbnail RAM-hit for level {} (sync)", levelID);
            return;
        }

        // Slow path: cargar async desde disco/red.
        WeakRef<PaimonLevelAreaInnerLayer> self = this;
        Ref<CCNode> doorRef = doorNode;
        std::string fileName = fmt::format("{}.png", levelID);
        ThumbnailLoader::get().requestLoad(
            levelID,
            fileName,
            [self, doorRef, levelID](CCTexture2D* tex, bool) {
                auto layer = self.lock();
                if (!layer || !doorRef || !tex) return;

                auto layerFields = layer->m_fields.self();
                if (!layerFields) return;

                if (layerFields->m_doorThumbnails.find(levelID) !=
                    layerFields->m_doorThumbnails.end()) {
                    return;
                }

                auto thumbSprite = CCSprite::createWithTexture(tex);
                if (!thumbSprite) return;

                auto doorSize = doorRef->getContentSize();
                float scale = std::min(
                    (doorSize.width * 0.8f) / thumbSprite->getContentWidth(),
                    (doorSize.height * 0.8f) / thumbSprite->getContentHeight()
                );

                thumbSprite->setScale(scale);
                thumbSprite->setPosition(doorSize / 2);
                thumbSprite->setZOrder(-1);
                thumbSprite->setOpacity(180);
                doorRef->addChild(thumbSprite);

                layerFields->m_doorThumbnails[levelID] = thumbSprite;
                log::info("[LevelAreaInnerLayer] Thumbnail added for level {} (async)", levelID);
            },
            ThumbnailLoader::PriorityHero, false
        );
    }

    $override
    void onExit() {
        LevelAreaInnerLayer::onExit();
        
        auto fields = m_fields.self();
        fields->m_doorThumbnails.clear();
        fields->m_thumbnailsAdded = false;
    }
};

class $modify(InfoBtnHookFLAlertLayer, FLAlertLayer) {
    static void onModify(auto& self) {
        // Mantiene prioridad VeryLate para animaciones
        (void)self.setHookPriorityPost("FLAlertLayer::show", geode::Priority::VeryLate);
    }

    struct Fields {
        // ID del nivel-torre capturado al mostrar el FLAlertLayer.
        // Antes infereiamos esto por el string del titulo (lo que rompia con
        // mods de localizacion). Ahora leemos LevelAreaInnerLayer::m_levelID
        // directamente.
        int m_capturedLevelID = -1;
    };

    $override

    void show() {
        FLAlertLayer::show();

        // Filtra para no afectar otros popups del juego
        auto* scene = CCDirector::get()->getRunningScene();
        if (!scene) return;
        LevelAreaInnerLayer* lai = nullptr;
        if (auto* children = scene->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (auto* l = typeinfo_cast<LevelAreaInnerLayer*>(child)) {
                    lai = l;
                    break;
                }
            }
        }
        if (!lai) return;

        // Tower secret levels: 5001 (The Tower), 5002 (The Sewers),
        // 5003 (The Cellar), 5004 (The Secret Hollow).
        // Constantes de GD; estables entre versiones del juego.
        int levelID = lai->m_levelID;
        if (levelID < 5001 || levelID > 5004) return;

        m_fields->m_capturedLevelID = levelID;

        this->getScheduler()->scheduleSelector(schedule_selector(InfoBtnHookFLAlertLayer::checkAndAddButton), this, 0.0f, 0, 0.0f, false);
    }

    void checkAndAddButton(float) {
        // No agrega boton en popup propio
        if (this->getID() == "simple-thumbnail-popup"_spr) return;

        int foundLevelID = m_fields->m_capturedLevelID;
        if (foundLevelID < 5001 || foundLevelID > 5004) return;

        CCNode* container = this->m_mainLayer ? this->m_mainLayer : this;
        if (!container) return;

        if (foundLevelID > 0) {
            auto winSize = CCDirector::get()->getWinSize();
            
            // Cadena de fallback para el icono
            CCSprite* iconSpr = CCSprite::create("paim_BotonMostrarThumbnails.png"_spr);
            if (!paimon::SpriteHelper::isValidSprite(iconSpr)) iconSpr = nullptr;
            if (!iconSpr) iconSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_plusBtn_001.png");
            if (!iconSpr) iconSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png");
            if (!iconSpr) iconSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_square01.png");

            if (iconSpr) {
                iconSpr->setRotation(-90.0f);
                // Reduce el icono 20%
                iconSpr->setScale(0.8f);
                
                // Boton circular verde
                auto btnSprite = CircleButtonSprite::create(
                    iconSpr,
                    CircleBaseColor::Green,
                    CircleBaseSize::Small
                );

                if (!btnSprite) return;

                auto btn = CCMenuItemSpriteExtra::create(
                    btnSprite,
                    this,
                    menu_selector(InfoBtnHookFLAlertLayer::onShowThumbnailTheTower)
                );
                btn->setID("paimbnails-tower-btn"_spr);
                btn->setTag(foundLevelID);
    
                if (this->m_buttonMenu) {
                    this->m_buttonMenu->addChild(btn);
                    btn->setPosition({160.f, 100.f}); 
                } else {
                    auto menu = CCMenu::create();
                    menu->setPosition(winSize / 2);
                    menu->addChild(btn);
                    btn->setPosition({160.f, 100.f});
                    
                    container->addChild(menu, 10);
                    // Prioridad dinamica para no bloquear otros mods
                    menu->setTouchPriority(
                        CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 1
                    ); 
                }
            }
        }
    }
    
    void onShowThumbnailTheTower(CCObject* sender) {
         int levelID = sender->getTag();
         std::string levelName = "Thumbnail";
         
         if (levelID == 5001) levelName = "The Tower";
         else if (levelID == 5002) levelName = "The Sewers";
         else if (levelID == 5003) levelName = "The Cellar";
         else if (levelID == 5004) levelName = "The Secret Hollow";
         
         auto spinner = PaimonLoadingOverlay::create("Loading...", 30.f);
         spinner->show(this, 100);
         Ref<PaimonLoadingOverlay> loading = spinner;
         
         ThumbnailLoader::get().requestLoad(levelID, "", [loading, levelName](CCTexture2D* tex, bool success){
             if (loading) loading->dismiss();
             
             if (success && tex) {
                  auto popup = SimpleThumbnailPopup::create(tex, levelName);
                  popup->show();
             } else {
                  PaimonNotify::create("Thumbnail not found for this level", NotificationIcon::Error)->show();
             }
         });
    }
};
