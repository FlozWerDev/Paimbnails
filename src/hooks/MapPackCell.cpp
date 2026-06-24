#include <Geode/Geode.hpp>
#include <Geode/modify/MapPackCell.hpp>
#include "../framework/HookConventions.hpp"
#include "../utils/ListThumbnailCarousel.hpp"
#include "TransparentListHelpers.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <sstream>

using namespace geode::prelude;

class $modify(PaimonMapPackCell, MapPackCell) {
    static void onModify(auto& self) {
        paimon::hooks::afterNodeIdsOrLate(self, "MapPackCell::loadFromMapPack");
    }

    struct Fields {
        Ref<ListThumbnailCarousel> m_carousel = nullptr;
        Ref<GJMapPack> m_pack = nullptr;
    };

    $override
    void loadFromMapPack(GJMapPack* pack) {
        MapPackCell::loadFromMapPack(pack);
        paimon::transparentlist::applyTransparentCellBg(this);

        if (!pack) return;
        
        m_fields->m_pack = pack;

        if (m_fields->m_carousel) {
            m_fields->m_carousel->removeFromParent();
            m_fields->m_carousel = nullptr;
        }

        WeakRef<PaimonMapPackCell> self = this;
        Loader::get()->queueInMainThread([self]() {
            if (paimon::isRuntimeShuttingDown()) return;
            auto cellRef = self.lock();
            if (auto* cell = static_cast<PaimonMapPackCell*>(cellRef.data()); cell && cell->getParent()) {
                cell->createCarousel();
            }
        });
    }

    void createCarousel() {
        auto pack = m_fields->m_pack;
        if (!pack) return;

        std::vector<int> levelIDs;
        
        if (pack->m_levels && pack->m_levels->count() > 0) {
            for (auto obj : CCArrayExt<CCObject*>(pack->m_levels)) {
                // Try as CCString
                if (auto str = typeinfo_cast<CCString*>(obj)) {
                    if (auto res = geode::utils::numFromString<int>(str->getCString())) {
                        levelIDs.push_back(res.unwrap());
                    }
                } 
                // Or as GJGameLevel
                else if (auto level = typeinfo_cast<GJGameLevel*>(obj)) {
                    levelIDs.push_back(level->m_levelID);
                }
            }
        }

        // Parse the level string if m_levels is empty
        if (levelIDs.empty() && !pack->m_levelStrings.empty()) {
            std::string levelsStr(pack->m_levelStrings.c_str());
            std::stringstream ss(levelsStr);
            std::string segment;
            while (std::getline(ss, segment, ',')) {
                if (auto res = geode::utils::numFromString<int>(segment)) {
                    auto val = res.unwrap();
                    if (val > 0) levelIDs.push_back(val);
                }
            }
        }

        if (levelIDs.empty()) return;

        auto size = this->getContentSize();
        
        // Force minimum height
        CCSize carouselSize = size;
        if (carouselSize.height < 90.0f) {
            carouselSize.height = 90.0f;
        }

        auto carousel = ListThumbnailCarousel::create(levelIDs, carouselSize);
        if (carousel) {
            carousel->setID("paimon-mappack-carousel"_spr);

            // Center it behind the text
            carousel->setPosition({size.width / 2, size.height / 2});
            
            carousel->setZOrder(-1); 
            
            // Original background goes further back
            if (auto bg = this->getChildByType<CCLayerColor>(0)) {
                bg->setZOrder(-2);
            } else if (auto firstChild = this->getChildByType<CCNode>(0)) {
                firstChild->setZOrder(-2);
            }
            
            carousel->setOpacity(255); 
            
            this->addChild(carousel);
            m_fields->m_carousel = carousel;
            
            carousel->startCarousel();
        }
    }
};
