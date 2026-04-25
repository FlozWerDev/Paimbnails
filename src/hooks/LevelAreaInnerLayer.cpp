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
                log::info("[LevelAreaInnerLayer] Thumbnail added for level {}", levelID);
            },
            1, false
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

    $override

    void show() {
        FLAlertLayer::show();
        
        // Filtra para no afectar otros popups del juego
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        bool inLevelArea = false;
        if (auto* children = scene->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                if (typeinfo_cast<LevelAreaInnerLayer*>(child)) {
                    inLevelArea = true;
                    break;
                }
            }
        }
        if (!inLevelArea) return;

        this->getScheduler()->scheduleSelector(schedule_selector(InfoBtnHookFLAlertLayer::checkAndAddButton), this, 0.0f, 0, 0.0f, false);
    }
    
    void checkAndAddButton(float) {
        // No agrega boton en popup propio
        if (this->getID() == "simple-thumbnail-popup"_spr) return;
        
        int foundLevelID = 0;
        
        CCNode* container = this->m_mainLayer ? this->m_mainLayer : this;
        if (container) {
            auto children = container->getChildren();
            if (children) {
                 for (auto* child : CCArrayExt<CCNode*>(children)) {
                      if (auto label = typeinfo_cast<CCLabelBMFont*>(child)) {
                           std::string txt = label->getString();
                           if (txt == "The Tower") foundLevelID = 5001;
                           else if (txt == "The Sewers") foundLevelID = 5002;
                           else if (txt == "The Cellar") foundLevelID = 5003;
                           else if (txt == "The Secret Hollow") foundLevelID = 5004;
                           
                           if (foundLevelID > 0) break;
                      }
                 }
            }
        }
        
        if (foundLevelID > 0) {
            auto winSize = CCDirector::sharedDirector()->getWinSize();
            
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
                        CCDirector::sharedDirector()->getTouchDispatcher()->getTargetPrio() - 1
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
