#include "ListThumbnailCarousel.hpp"
#include "../features/thumbnails/services/ThumbnailLoader.hpp"
#include "../features/thumbnails/services/ListThumbnailManager.hpp"
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
    if (m_levelIDs.empty()) return;
    
    int foundIndex = -1;
    size_t listSize = m_levelIDs.size();
    int triggeredDownloads = 0;

    // escanear lista desde indice
    for (size_t i = 0; i < listSize; i++) {
        int idx = (m_currentIndex + i) % listSize;
        int levelID = m_levelIDs[idx];

        // fallo antes -> skip
        if (ThumbnailLoader::get().isFailed(levelID)) {
            continue;
        }

        // ya cargado = candidato
        if (ThumbnailLoader::get().isLoaded(levelID)) {
            foundIndex = idx;
            break;
        } else {
            // no cargado -> encolar download
            
            // auto-descargar solo primeros 3
            // resto: solo si en cache
            if (idx < 3) {
                // max 3 requests por ciclo
                if (triggeredDownloads < 3) {
                    if (!ThumbnailLoader::get().isPending(levelID)) {
                        std::string fileName = fmt::format("{}.png", levelID);
                        ThumbnailLoader::get().requestLoad(levelID, fileName, [](CCTexture2D*, bool){}, 1);
                        triggeredDownloads++;
                    }
                }
            }
        }
    }

    if (foundIndex != -1) {
        // img valida
        int levelID = m_levelIDs[foundIndex];
        
        // alive compartido pa callbacks
        auto alive = m_alive;
        auto* self = this;
        std::string fileName = fmt::format("{}.png", levelID);
        
        ThumbnailLoader::get().requestLoad(levelID, fileName, [self, alive, levelID](CCTexture2D* tex, bool) {
            if (!alive || !*alive) return;
            // double-check padre
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
        }, 0);
        
        // siguiente: item despues, rota
        m_currentIndex = (foundIndex + 1) % listSize;
        
        // schedule siguiente rotacion
        this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
        this->schedule(schedule_selector(ListThumbnailCarousel::updateCarousel), 3.0f);
    } else {
        // nada listo, check pronto
        this->unschedule(schedule_selector(ListThumbnailCarousel::updateCarousel));
        this->schedule(schedule_selector(ListThumbnailCarousel::updateCarousel), 0.5f);
    }
    
    // siguiente en cola
    int nextID = m_levelIDs[m_currentIndex];
    if (!ThumbnailLoader::get().isLoaded(nextID) && 
        !ThumbnailLoader::get().isFailed(nextID) && 
        !ThumbnailLoader::get().isPending(nextID)) {
         std::string fileName = fmt::format("{}.png", nextID);
         // prioridad 1 cola
         ThumbnailLoader::get().requestLoad(nextID, fileName, [](CCTexture2D*, bool){}, 1);
    }
}

void ListThumbnailCarousel::onImageLoaded(CCTexture2D* texture, int index) {
    if (!texture) {
        return;
    }

    // no adjunto -> no sprites
    if (!this->getParent()) {
        return;
    }

    // check textura valida
    if (!ThumbnailLoader::isTextureSane(texture)) {
        return;
    }
    
    CCSprite* sprite = nullptr;
    
    // texture validity already verified by isTextureSane above
    sprite = CCSprite::createWithTexture(texture);

    if (!sprite) return;
    
    // 1) calcular un rect visible con ajuste de aspecto
    float targetAspect = m_size.width / m_size.height;
    float texWidth = texture->getContentSize().width;
    float texHeight = texture->getContentSize().height;
    
    float maxW = texWidth;
    float maxH = texWidth / targetAspect;
    
    if (maxH > texHeight) {
        maxH = texHeight;
        maxW = texHeight * targetAspect;
    }
    
    // zoom pa pan
    float zoom = 1.06f;
    float visibleW = maxW / zoom;
    float visibleH = maxH / zoom;
    
    // 3) calcular la holgura disponible
    float totalSlackW = texWidth - visibleW;
    
    // 4) determinar el rango de paneo
    // limit move 10% width
    float maxPan = visibleW * 0.10f;
    float travelX = std::min(totalSlackW, maxPan);
    
    // centrar rango en holgura
    float unusedSlackX = totalSlackW - travelX;
    float offsetX = unusedSlackX / 2.0f;
    
    // direccion pan random
    bool panRight = (rand() % 2) == 0;
    
    float startX = panRight ? offsetX : (offsetX + travelX);
    float endX = panRight ? (offsetX + travelX) : offsetX;
    
    // centrar Y
    float startY = (texHeight - visibleH) / 2.0f;
    float endY = startY;
    
    m_panStartRect = CCRect(startX, startY, visibleW, visibleH);
    m_panEndRect = CCRect(endX, endY, visibleW, visibleH);
    m_panElapsed = 0.0f;
    
    sprite->setTextureRect(m_panStartRect);
    
    // scale sprite
    float scale = m_size.width / visibleW;
    sprite->setScale(scale);
    
    // centrar nodo
    sprite->setPosition(m_size / 2);
    sprite->setOpacity(0);
    
    // shader si no (compat mods)
    if (!sprite->getShaderProgram()) {
        sprite->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
    }

    this->addChild(sprite);
    
    // fade in nuevo sprite
    sprite->runAction(CCFadeTo::create(0.5f, m_opacity));
    
    // fade out y quitar sprite anterior
    if (m_currentSprite) {
        m_currentSprite->runAction(CCSequence::create(
            CCFadeOut::create(0.5f),
            CCCallFunc::create(m_currentSprite, callfunc_selector(CCNode::removeFromParent)),
            nullptr
        ));
    }
    
    m_currentSprite = sprite;
    
    // schedule updatePan
    this->unschedule(schedule_selector(ListThumbnailCarousel::updatePan));
    this->schedule(schedule_selector(ListThumbnailCarousel::updatePan));
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
