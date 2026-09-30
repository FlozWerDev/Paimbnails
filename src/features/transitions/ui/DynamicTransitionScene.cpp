#include "DynamicTransitionScene.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <array>

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {

void roundedRect(CCDrawNode* draw, Rect rect, float radius, ccColor4F color) {
    std::array<CCPoint, 36> points;
    radius = std::clamp(radius, 0.f, std::min(rect.width, rect.height) / 2.f);
    std::array<CCPoint, 4> centers{{
        {rect.x + rect.width - radius, rect.y + radius},
        {rect.x + rect.width - radius, rect.y + rect.height - radius},
        {rect.x + radius, rect.y + rect.height - radius},
        {rect.x + radius, rect.y + radius}}};
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= 8; ++step) {
            float angle = (-.5f + corner * .5f + step / 16.f) * 3.14159265f;
            points[corner * 9 + step] = {centers[corner].x + radius * std::cos(angle),
                centers[corner].y + radius * std::sin(angle)};
        }
    }
    draw->drawPolygon(points.data(), points.size(), color, 0.f, {0.f, 0.f, 0.f, 0.f});
}

}

Visual* Visual::create(CCNode* from, CCNode* to, CCSize size, Config config,
    Rect origin, bool backwards, bool morphButton) {
    if (!from || !to || size.width < 8.f || size.height < 8.f) return nullptr;
    auto fromSurface = captureSurface(from, size, config);
    auto toSurface = captureSurface(to, size, config);
    return createFromSnapshots(fromSurface, toSurface, size, config, origin, backwards, morphButton);
}

Visual* Visual::createFromSnapshots(Ref<CCRenderTexture> from, Ref<CCRenderTexture> to,
    CCSize size, Config config, Rect origin, bool backwards, bool morphButton) {
    if (!from || !to || size.width < 8.f || size.height < 8.f) return nullptr;
    auto* visual = new Visual();
    visual->m_config = sanitize(config);
    visual->m_size = size;
    visual->m_origin = normalizeOrigin(origin, size.width, size.height);
    visual->m_backwards = backwards;
    visual->m_fromSurface = from;
    visual->m_toSurface = to;
    if (visual->initialize(morphButton)) {
        visual->autorelease();
        return visual;
    }
    delete visual;
    return nullptr;
}

Ref<CCRenderTexture> Visual::captureSurface(CCNode* node, CCSize size, Config config) {
    if (!node || size.width < 8.f || size.height < 8.f) return nullptr;
    config = sanitize(config);
    auto* director = CCDirector::get();
    auto* view = director->getOpenGLView();
    if (!view) return nullptr;
    float pixelScale = std::max(1.f, director->getContentScaleFactor());
    float displayScale = std::max(view->getScaleX(), view->getScaleY()) * geode::utils::getDisplayFactor();
    GLint maxTexture = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    float budget = std::min(2048.f, static_cast<float>(maxTexture));
    float scale = std::min({config.quality * displayScale / pixelScale, budget / (size.width * pixelScale),
        budget / (size.height * pixelScale),
        std::sqrt(2097152.f / (size.width * size.height)) / pixelScale});
    int width = std::max(1, static_cast<int>(size.width * scale));
    int height = std::max(1, static_cast<int>(size.height * scale));
    Ref<CCRenderTexture> surface = CCRenderTexture::create(width, height, kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
    if (!surface || !surface->getSprite()) return nullptr;
    // The matrix scales captured pixels without changing the live scene's nodes.
    surface->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
    kmGLMatrixMode(KM_GL_MODELVIEW);
    kmGLPushMatrix();
    kmGLScalef(width / size.width, height / size.height, 1.f);
    node->visit();
    kmGLPopMatrix();
    surface->end();
    auto* original = surface->getSprite();
    original->getTexture()->setAntiAliasTexParameters();
    return surface;
}

bool Visual::initialize(bool morphButton) {
    if (!CCNode::init()) return false;
    setContentSize(m_size);
    auto* backdrop = CCLayerColor::create({12, 15, 25, 255}, m_size.width, m_size.height);
    if (!backdrop) return false;
    addChild(backdrop, -1);
    auto makeSprite = [](CCRenderTexture* surface) {
        auto* original = surface->getSprite();
        if (!original) return static_cast<CCSprite*>(nullptr);
        auto* sprite = CCSprite::createWithTexture(original->getTexture(), original->getTextureRect());
        if (sprite) sprite->setFlipY(true);
        return sprite;
    };
    auto* fromSprite = makeSprite(m_fromSurface.data());
    auto* toSprite = makeSprite(m_toSurface.data());
    if (!fromSprite || !toSprite) return false;
    m_background = m_backwards ? toSprite : fromSprite;
    m_foreground = m_backwards ? fromSprite : toSprite;
    m_background->setPosition(m_size / 2.f);
    addChild(m_background);

    m_shadow = PaimonDrawNode::create();
    m_stencil = PaimonDrawNode::create();
    if (!m_shadow || !m_stencil) return false;
    m_clip = CCClippingNode::create(m_stencil);
    if (!m_clip) return false;
    m_clip->setAlphaThreshold(.05f);
    m_clip->setContentSize(m_size);
    addChild(m_shadow, 1);
    addChild(m_clip, 2);
    m_clip->addChild(m_foreground);

    if (morphButton && m_config.style == Style::App && m_config.buttonBlend > 0.f) {
        auto* sprite = m_backwards ? toSprite : fromSprite;
        auto textureRect = sprite->getTextureRect();
        float sx = textureRect.size.width / m_size.width;
        float sy = textureRect.size.height / m_size.height;
        CCRect patch{textureRect.origin.x + m_origin.x * sx,
            textureRect.origin.y + (m_size.height - m_origin.y - m_origin.height) * sy,
            m_origin.width * sx, m_origin.height * sy};
        m_button = CCSprite::createWithTexture(sprite->getTexture(), patch);
        if (m_button) {
            m_button->setFlipY(true);
            m_clip->addChild(m_button, 1);
        }
    }
    if (!m_button) m_config.buttonBlend = 0.f;
    setProgress(0.f);
    return true;
}

void Visual::setProgress(float progress) {
    auto frame = evaluate(m_config, m_origin, m_size.width, m_size.height, progress, m_backwards);
    auto rect = frame.rect;
    m_stencil->clear();
    m_stencil->setPosition({rect.x, rect.y});
    m_stencil->setContentSize({rect.width, rect.height});
    roundedRect(m_stencil, {0.f, 0.f, rect.width, rect.height}, frame.radius, {1.f, 1.f, 1.f, 1.f});
    m_shadow->clear();
    if (frame.shadow > .001f) {
        for (int i = 3; i > 0; --i) {
            float spread = i * 3.f;
            roundedRect(m_shadow, {rect.x - spread, rect.y - spread - 2.f,
                rect.width + spread * 2.f, rect.height + spread * 2.f},
                frame.radius + spread, {0.f, 0.f, 0.f, frame.shadow / 3.f});
        }
    }
    m_background->setScaleX(m_size.width / m_background->getContentSize().width * frame.backgroundScale);
    m_background->setScaleY(m_size.height / m_background->getContentSize().height * frame.backgroundScale);
    auto brightness = static_cast<GLubyte>(frame.backgroundBrightness * 255.f);
    m_background->setColor({brightness, brightness, brightness});

    CCPoint center{rect.x + rect.width / 2.f, rect.y + rect.height / 2.f};
    m_foreground->setPosition(center);
    m_foreground->setScale(std::max(rect.width / m_foreground->getContentSize().width,
        rect.height / m_foreground->getContentSize().height));
    m_foreground->setOpacity(static_cast<GLubyte>(frame.opacity * 255.f));
    if (m_button) {
        m_button->setPosition(center);
        m_button->setScaleX(rect.width / m_button->getContentSize().width);
        m_button->setScaleY(rect.height / m_button->getContentSize().height);
        m_button->setOpacity(static_cast<GLubyte>(frame.buttonOpacity * 255.f));
    }
}

DynamicTransitionScene* DynamicTransitionScene::create(CCScene* destination,
    Config config, Rect origin, bool backwards, bool morphButton) {
    if (!destination || destination == CCDirector::get()->getRunningScene()) return nullptr;
    auto* scene = new DynamicTransitionScene();
    scene->m_config = sanitize(config);
    scene->m_origin = origin;
    scene->m_backwards = backwards;
    scene->m_morphButton = morphButton;
    float duration = backwards ? scene->m_config.backDuration : scene->m_config.duration;
    if (scene->initWithDuration(duration, destination)) {
        scene->autorelease();
        return scene;
    }
    delete scene;
    return nullptr;
}

void DynamicTransitionScene::onEnter() {
    CCTransitionScene::onEnter();
    m_size = CCDirector::get()->getWinSize();
    m_visual = Visual::create(m_pOutScene, m_pInScene, m_size, m_config,
        m_origin, m_backwards, m_morphButton);
    if (!m_visual) {
        log::warn("[DynamicTransition] Capture unavailable; finishing scene navigation");
        complete();
        return;
    }
    addChild(m_visual);
    scheduleUpdate();
}

void DynamicTransitionScene::draw() {
    if (!m_visual) CCTransitionScene::draw();
}

void DynamicTransitionScene::update(float dt) {
    if (m_finished) return;
    m_elapsed = std::min(m_fDuration, m_elapsed + limit(dt, 0.f, 0.f, 2.f));
    auto size = CCDirector::get()->getWinSize();
    if (!size.equals(m_size)) {
        m_visual->setVisible(false);
        m_visual = nullptr;
        complete();
        return;
    }
    m_visual->setProgress(m_elapsed / m_fDuration);
    if (m_elapsed >= m_fDuration) complete();
}

void DynamicTransitionScene::complete() {
    if (m_finished) return;
    m_finished = true;
    unscheduleUpdate();
    finish();
}

void DynamicTransitionScene::onExit() {
    unscheduleAllSelectors();
    CCTransitionScene::onExit();
}

}
