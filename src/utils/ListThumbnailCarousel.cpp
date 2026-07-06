#include "ListThumbnailCarousel.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ListThumbnailManager.hpp"
#include "../core/RuntimeLifecycle.hpp"
#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS
#include <excpt.h>
#endif

using namespace geode::prelude;

ListThumbnailCarousel::~ListThumbnailCarousel() {
    // safety-net only; lifecycle cleanup should happen in onExit
    if (m_alive) *m_alive = false;
}

void ListThumbnailCarousel::onExit() {
    if (m_alive) *m_alive = false;

    for (int id : m_levelIDs) {
        ThumbnailLoader::get().cancelLoad(id);
    }

    this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
    this->unschedule(schedule_selector(ListThumbnailCarousel::updatePan));
    CCNode::onExit();
}

ListThumbnailCarousel* ListThumbnailCarousel::create(std::vector<int> const& levelIDs, CCSize size) {
    auto ret = new ListThumbnailCarousel();
    if (ret && ret->init(levelIDs, size)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool ListThumbnailCarousel::init(std::vector<int> const& levelIDs, CCSize size) {
    if (!CCNode::init()) return false;
    
    m_alive = std::make_shared<bool>(true);
    m_levelIDs = levelIDs;
    m_size = size;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});
    
    m_loadingSpinner = geode::LoadingSpinner::create(16.f);
    if (m_loadingSpinner) {
        m_loadingSpinner->setPosition({size.width - 85.0f, size.height / 2});
        this->addChild(m_loadingSpinner);
    }

    return true;
}

void ListThumbnailCarousel::startCarousel() {
    if (m_levelIDs.empty()) return;
    
    tryShowNextImage();
}

void ListThumbnailCarousel::updateCarousel(float dt) {
    tryShowNextImage();
}

void ListThumbnailCarousel::updatePan(float dt) {
    if (!m_currentSprite) return;
    
    m_panElapsed += dt;
    float duration = 5.0f;
    
    float t = m_panElapsed / duration;
    if (t > 1.0f) t = 1.0f;
    
    float easeT = 0.5f * (1.0f - std::cos(t * M_PI));
    
    float currentX = m_panStartRect.origin.x + (m_panEndRect.origin.x - m_panStartRect.origin.x) * easeT;
    float currentY = m_panStartRect.origin.y + (m_panEndRect.origin.y - m_panStartRect.origin.y) * easeT;
    
    CCRect currentRect = m_panStartRect;
    currentRect.origin.x = currentX;
    currentRect.origin.y = currentY;
    
    m_currentSprite->setTextureRect(currentRect);
}

void ListThumbnailCarousel::tryShowNextImage() {
    if (paimon::isRuntimeShuttingDown()) return;
    if (m_levelIDs.empty()) return;
    
    int foundIndex = -1;
    size_t listSize = m_levelIDs.size();
    int triggeredDownloads = 0;

    // scan the list from the current index
    for (size_t i = 0; i < listSize; i++) {
        int idx = (m_currentIndex + i) % listSize;
        int levelID = m_levelIDs[idx];

        if (ThumbnailLoader::get().isFailed(levelID)) {
            continue;
        }

        if (ThumbnailLoader::get().isLoaded(levelID)) {
            foundIndex = idx;
            break;
        } else {
            // auto-download only the first 3; the rest only if cached
            if (idx < 3) {
                // max 3 requests per cycle
                if (triggeredDownloads < 3) {
                    if (!ThumbnailLoader::get().isPending(levelID)) {
                        std::string fileName = fmt::format("{}.png", levelID);
                        ThumbnailLoader::get().requestLoad(levelID, fileName, [](CCTexture2D*, bool){},
                            ThumbnailLoader::PriorityPredictivePrefetch);
                        triggeredDownloads++;
                    }
                }
            }
        }
    }

    if (foundIndex != -1) {
        int levelID = m_levelIDs[foundIndex];
        
        // shared alive flag for callbacks
        auto alive = m_alive;
        auto* self = this;
        std::string fileName = fmt::format("{}.png", levelID);
        
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, alive, levelID](CCTexture2D* tex, bool) {
            if (!alive || !*alive) return;
            if (!self->getParent()) return;

            if (self->m_loadingSpinner) {
                auto* circle = self->m_loadingSpinner;
                circle->runAction(CCSequence::create(
                    CCFadeOut::create(0.2f),
                    CCCallFunc::create(circle, callfunc_selector(CCNode::removeFromParent)),
                    nullptr
                ));
                self->m_loadingSpinner = nullptr;
            }
            if (tex) self->onImageLoaded(tex, levelID);
        }, ThumbnailLoader::PriorityVisibleCell);
        
        // next: the following item, wraps around
        m_currentIndex = (foundIndex + 1) % listSize;
        
        // schedule the next rotation
        this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
        this->schedule(schedule_selector(ListThumbnailCarousel::updateCarousel), 3.0f);
    } else {
        bool anyLoadable = false;
        for (int levelID : m_levelIDs) {
            if (!ThumbnailLoader::get().isFailed(levelID)) {
                anyLoadable = true;
                break;
            }
        }

        if (!anyLoadable) {
            this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
            if (m_loadingSpinner) {
                auto* circle = m_loadingSpinner;
                circle->runAction(CCSequence::create(
                    CCFadeOut::create(0.2f),
                    CCCallFunc::create(circle, callfunc_selector(CCNode::removeFromParent)),
                    nullptr
                ));
                m_loadingSpinner = nullptr;
            }
            return;
        }

        this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
        this->schedule(schedule_selector(ListThumbnailCarousel::updateCarousel), 0.5f);
    }

    int nextID = m_levelIDs[m_currentIndex];
    if (!ThumbnailLoader::get().isLoaded(nextID) &&
        !ThumbnailLoader::get().isFailed(nextID) &&
        !ThumbnailLoader::get().isPending(nextID)) {
        std::string fileName = fmt::format("{}.png", nextID);
        ThumbnailLoader::get().requestLoad(nextID, fileName, [](CCTexture2D*, bool){},
            ThumbnailLoader::PriorityPredictivePrefetch);
    }
}

void ListThumbnailCarousel::onImageLoaded(CCTexture2D* texture, int index) {
    if (!texture) {
        return;
    }

    // not attached -> no sprites
    if (!this->getParent()) {
        return;
    }

    if (!ThumbnailLoader::isTextureSane(texture)) {
        return;
    }
    
    CCSprite* sprite = nullptr;
    
    // texture validity already verified by isTextureSane above
    sprite = CCSprite::createWithTexture(texture);

    if (!sprite) return;
    
    // 1) compute a visible rect with aspect fit
    float targetAspect = m_size.width / m_size.height;
    float texWidth = texture->getContentSize().width;
    float texHeight = texture->getContentSize().height;
    
    float maxW = texWidth;
    float maxH = texWidth / targetAspect;
    
    if (maxH > texHeight) {
        maxH = texHeight;
        maxW = texHeight * targetAspect;
    }
    
    // zoom for pan
    float zoom = 1.06f;
    float visibleW = maxW / zoom;
    float visibleH = maxH / zoom;
    
    // 3) compute the available slack
    float totalSlackW = texWidth - visibleW;
    
    // 4) determine the pan range
    // limit move 10% width
    float maxPan = visibleW * 0.10f;
    float travelX = std::min(totalSlackW, maxPan);
    
    // center the range within the slack
    float unusedSlackX = totalSlackW - travelX;
    float offsetX = unusedSlackX / 2.0f;
    
    // random pan direction
    bool panRight = (rand() % 2) == 0;
    
    float startX = panRight ? offsetX : (offsetX + travelX);
    float endX = panRight ? (offsetX + travelX) : offsetX;
    
    float startY = (texHeight - visibleH) / 2.0f;
    float endY = startY;
    
    m_panStartRect = CCRect(startX, startY, visibleW, visibleH);
    m_panEndRect = CCRect(endX, endY, visibleW, visibleH);
    m_panElapsed = 0.0f;
    
    sprite->setTextureRect(m_panStartRect);
    
    float scale = m_size.width / visibleW;
    sprite->setScale(scale);
    
    sprite->setPosition(m_size / 2);
    sprite->setOpacity(0);
    
    // set shader if missing (mod compat)
    if (!sprite->getShaderProgram()) {
        sprite->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
    }

    this->addChild(sprite);
    
    // fade in the new sprite
    sprite->runAction(CCFadeTo::create(0.5f, m_opacity));
    
    // fade out and remove the previous sprite
    if (m_currentSprite) {
        m_currentSprite->runAction(CCSequence::create(
            CCFadeOut::create(0.5f),
            CCCallFunc::create(m_currentSprite, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }
    
    m_currentSprite = sprite;
    
    // schedule updatePan at 30Hz (visual panning doesn't need 60fps precision)
    this->unschedule(schedule_selector(ListThumbnailCarousel::updatePan));
    this->schedule(schedule_selector(ListThumbnailCarousel::updatePan), 1.f / 30.f);
}

void ListThumbnailCarousel::setOpacity(GLubyte opacity) {
    m_opacity = opacity;
    if (m_currentSprite) {
        m_currentSprite->setOpacity(opacity);
    }
}

void ListThumbnailCarousel::visit() {
    CCNode::visit();
}
