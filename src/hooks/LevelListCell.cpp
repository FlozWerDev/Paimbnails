#include <Geode/Geode.hpp>
#include <Geode/modify/LevelListCell.hpp>
#include "../utils/ListThumbnailCarousel.hpp"
#include "../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

namespace {
    // Disabled while stabilizing search/list scene transitions. Level list cells
    // are destroyed aggressively by GD's browser layer, and the async carousel
    // can leave actions/callbacks alive during CCNode teardown.
    constexpr bool kEnableLevelListCarousel = false;
}

class $modify(PaimonLevelListCell, LevelListCell) {
    static void onModify(auto& self) {
        (void)self.setHookPriorityPost("LevelListCell::loadFromList", geode::Priority::Late);
    }

    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<CCNode> m_listThumbnail = nullptr;
        int m_currentListID = 0;
    };

    // init hook removido por errores de compilacion

    void clearPaimonListThumbnailNodes() {
        if (m_fields->m_carousel) {
            m_fields->m_carousel->stopAllActions();
            if (m_fields->m_carousel->getParent()) {
                m_fields->m_carousel->removeFromParentAndCleanup(true);
            }
            m_fields->m_carousel = nullptr;
        }

        if (m_fields->m_listThumbnail) {
            m_fields->m_listThumbnail->stopAllActions();
            if (m_fields->m_listThumbnail->getParent()) {
                m_fields->m_listThumbnail->removeFromParentAndCleanup(true);
            }
            m_fields->m_listThumbnail = nullptr;
        }
    }

    $override
    void onExit() {
        clearPaimonListThumbnailNodes();
        LevelListCell::onExit();
    }

    
    $override
    void loadFromList(GJLevelList* list) {
        LevelListCell::loadFromList(list);

        if (!list) {
            log::warn("PaimonLevelListCell: list is null");
            return;
        }

        m_fields->m_currentListID = list->m_listID;
        log::debug("PaimonLevelListCell: loadFromList called for list ID: {}", list->m_listID);

        clearPaimonListThumbnailNodes();
        if (!kEnableLevelListCarousel) {
            return;
        }

        // Obtiene IDs de niveles
        std::vector<int> levelIDs;
        
        // Verifica si m_levels es accesible
        log::debug("PaimonLevelListCell: m_levels size: {}", list->m_levels.size());

        for (int id : list->m_levels) {
            if (id != 0) {
                levelIDs.push_back(id);
            }
            // Sin limite para permitir carousel completo
        }

        auto size = this->getContentSize();
        // log::info("PaimonLevelListCell: Cell content size: {}, {}", size.width, size.height);
        
        // Fallback si el size es 0
        if (size.width == 0 || size.height == 0) {
            size = CCSize(356, 50);
            this->setContentSize(size); // force update content size
        }

        // Fuerza altura para cubrir toda la celda
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }
        
        // Crea carousel
        if (!levelIDs.empty()) {
            auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
            if (carousel) {
                carousel->setID("paimon-thumbnail-carousel"_spr);

                // Centra el carousel y sube 20px
                carousel->setPosition({size.width / 2, size.height / 2 + 20.0f});
                
                // z=-1 detras de texto/botones
                carousel->setZOrder(-1);
                
                // Mueve el fondo detras del carousel
                if (auto bg = this->getChildByType<CCLayerColor>(0)) {
                    bg->setZOrder(-2);
                } else if (auto firstChild = this->getChildByType<CCNode>(0)) {
                    firstChild->setZOrder(-2);
                }
                
                carousel->setOpacity(255); 
                
                this->addChild(carousel);
                m_fields->m_carousel = carousel;
                
                carousel->startCarousel();
                log::debug("PaimonLevelListCell: Carousel created and added at {}, {}", size.width/2, size.height/2);
            } else {
                log::error("PaimonLevelListCell: Failed to create carousel");
            }
        }
    }
};
