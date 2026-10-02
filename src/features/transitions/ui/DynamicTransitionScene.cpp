#include "DynamicTransitionScene.hpp"
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <array>

#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

using namespace geode::prelude;

namespace paimon::transitions::dynamic {
namespace {

using Vertex = ccV2F_C4B_T2F;
constexpr int kMaxCornerSegments = 24;
constexpr int kMaxOutline = (kMaxCornerSegments + 1) * 4;
constexpr ccColor4B kBackdrop{12, 15, 25, 255};
constexpr ccColor4B kClear{0, 0, 0, 0};

using Outline = std::array<CCPoint, kMaxOutline>;

struct Mapping {
    Rect content;
    float maxS = 1.f, maxT = 1.f;
};

GLubyte channel(float value) {
    return static_cast<GLubyte>(std::lround(std::clamp(value, 0.f, 1.f) * 255.f));
}

ccColor4B premultiplied(float gray, float alpha) {
    auto value = channel(gray * alpha);
    return {value, value, value, channel(alpha)};
}

bool coversScreen(Rect rect, float radius, float width, float height) {
    constexpr float kSlack = .01f;
    return radius <= .5f && rect.x <= kSlack && rect.y <= kSlack &&
        rect.x + rect.width >= width - kSlack && rect.y + rect.height >= height - kSlack;
}

int cornerSegments(float radius, float pixels) {
    if (radius <= .01f) return 0;
    return std::clamp(static_cast<int>(std::sqrt(radius * pixels) * 1.2f), 2, kMaxCornerSegments);
}

Rect grow(Rect rect, float amount, float drop = 0.f) {
    return {rect.x - amount, rect.y - amount - drop, rect.width + amount * 2.f, rect.height + amount * 2.f};
}

// Outlines with the same segment count line up point by point, so rings can join them directly.
int outline(Rect rect, float radius, int segments, Outline& points) {
    radius = std::clamp(radius, 0.f, std::min(rect.width, rect.height) / 2.f);
    std::array<CCPoint, 4> centers{{
        {rect.x + rect.width - radius, rect.y + radius},
        {rect.x + rect.width - radius, rect.y + rect.height - radius},
        {rect.x + radius, rect.y + rect.height - radius},
        {rect.x + radius, rect.y + radius}}};
    int count = 0;
    for (int corner = 0; corner < 4; ++corner) {
        for (int step = 0; step <= segments; ++step) {
            float fraction = segments ? static_cast<float>(step) / static_cast<float>(segments) : .5f;
            float angle = (static_cast<float>(corner - 1) + fraction) * kPi * .5f;
            points[count++] = CCPoint{centers[corner].x + radius * std::cos(angle),
                centers[corner].y + radius * std::sin(angle)};
        }
    }
    return count;
}

class Mesh {
public:
    Mesh(std::vector<Vertex>& vertices, std::vector<Visual::Batch>& batches, float pixels)
        : m_vertices(vertices), m_batches(batches), m_pixels(pixels), m_feather(1.f / pixels) {}

    void use(CCTexture2D* texture, Mapping mapping = {}) {
        m_mapping = mapping;
        m_mapping.content.width = std::max(m_mapping.content.width, .001f);
        m_mapping.content.height = std::max(m_mapping.content.height, .001f);
        m_textured = texture != nullptr;
        if (m_batches.empty() || m_batches.back().texture != texture)
            m_batches.push_back({texture, static_cast<GLint>(m_vertices.size()), 0});
    }

    void quad(Rect rect, ccColor4B color) {
        CCPoint a{rect.x, rect.y}, b{rect.x + rect.width, rect.y};
        CCPoint c{rect.x + rect.width, rect.y + rect.height}, d{rect.x, rect.y + rect.height};
        triangle(a, b, c, color, color, color);
        triangle(a, c, d, color, color, color);
    }

    void shape(Rect rect, float radius, ccColor4B color) {
        int segments = cornerSegments(radius, m_pixels);
        Outline inner, outer;
        int count = outline(rect, radius, segments, inner);
        outline(grow(rect, m_feather), segments ? radius + m_feather : 0.f, segments, outer);
        CCPoint center{rect.x + rect.width / 2.f, rect.y + rect.height / 2.f};
        for (int i = 0; i < count; ++i)
            triangle(center, inner[i], inner[(i + 1) % count], color, color, color);
        ring(inner, outer, count, color, kClear);
    }

    // Two rings approximate a gaussian falloff; the shadow sits slightly below its card.
    void shadow(Rect rect, float radius, float blur, float strength) {
        int segments = cornerSegments(radius + blur, m_pixels);
        Outline edge, middle, outer;
        int count = outline(rect, radius, segments, edge);
        outline(grow(rect, blur * .35f, blur * .12f), radius + blur * .35f, segments, middle);
        outline(grow(rect, blur, blur * .3f), radius + blur, segments, outer);
        ccColor4B dark{0, 0, 0, channel(strength)};
        ccColor4B soft{0, 0, 0, channel(strength * .38f)};
        ring(edge, middle, count, dark, soft);
        ring(middle, outer, count, soft, kClear);
    }

private:
    void ring(Outline const& inner, Outline const& outer, int count, ccColor4B innerColor, ccColor4B outerColor) {
        for (int i = 0; i < count; ++i) {
            int j = (i + 1) % count;
            triangle(inner[i], outer[i], outer[j], innerColor, outerColor, outerColor);
            triangle(inner[i], outer[j], inner[j], innerColor, outerColor, innerColor);
        }
    }

    void triangle(CCPoint a, CCPoint b, CCPoint c, ccColor4B ca, ccColor4B cb, ccColor4B cc) {
        push(a, ca);
        push(b, cb);
        push(c, cc);
    }

    void push(CCPoint point, ccColor4B color) {
        Vertex vertex{{point.x, point.y}, color, {0.f, 0.f}};
        if (m_textured) {
            auto const& content = m_mapping.content;
            vertex.texCoords.u = std::clamp((point.x - content.x) / content.width, 0.f, 1.f) * m_mapping.maxS;
            vertex.texCoords.v = std::clamp((point.y - content.y) / content.height, 0.f, 1.f) * m_mapping.maxT;
        }
        m_vertices.push_back(vertex);
        ++m_batches.back().count;
    }

    std::vector<Vertex>& m_vertices;
    std::vector<Visual::Batch>& m_batches;
    Mapping m_mapping;
    float m_pixels, m_feather;
    bool m_textured = false;
};

void sealAlpha() {
    // Translucent sprites lower the destination alpha; a sealed capture never shows what is behind it.
    GLfloat clear[4];
    GLboolean mask[4];
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (scissor) glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    if (scissor) glEnable(GL_SCISSOR_TEST);
}

GLint maxTextureSize() {
    static GLint size = 0;
    if (size <= 0) {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &size);
        if (size <= 0) size = 2048;
    }
    return size;
}

}

float Visual::pixelsPerPoint() {
    auto* view = CCDirector::get()->getOpenGLView();
    if (!view) return 1.f;
    float scale = std::max(view->getScaleX(), view->getScaleY()) * geode::utils::getDisplayFactor();
    return limit(scale, 1.f, .25f, 16.f);
}

Visual* Visual::create(Ref<CCTexture2D> from, Ref<CCTexture2D> to, CCSize size, Config config,
    Rect origin, bool backwards, bool morphButton, bool panel) {
    if (!from || size.width < 8.f || size.height < 8.f) return nullptr;
    auto* visual = new Visual();
    if (!visual->init()) {
        delete visual;
        return nullptr;
    }
    visual->m_config = sanitize(config);
    if (!morphButton) visual->m_config.buttonBlend = 0.f;
    visual->m_size = size;
    visual->m_origin = normalizeOrigin(origin, size.width, size.height);
    visual->m_backwards = backwards;
    visual->m_panel = panel;
    visual->m_from = from;
    visual->m_to = to;
    visual->m_pixels = pixelsPerPoint();
    visual->m_vertices.reserve(4096);
    visual->setContentSize(size);
    visual->autorelease();
    return visual;
}

Ref<CCTexture2D> Visual::capture(CCNode* node, CCSize size, float quality) {
    if (!node || size.width < 8.f || size.height < 8.f) return nullptr;
    auto* director = CCDirector::get();
    float pixelScale = std::max(1.f, director->getContentScaleFactor());
    float budget = std::min(2048.f, static_cast<float>(maxTextureSize()));
    float scale = std::min({limit(quality, 1.f, .5f, 1.f) * pixelsPerPoint() / pixelScale,
        budget / (size.width * pixelScale), budget / (size.height * pixelScale),
        std::sqrt(2097152.f / (size.width * size.height)) / pixelScale});
    int width = std::max(1, static_cast<int>(size.width * scale));
    int height = std::max(1, static_cast<int>(size.height * scale));
    // Keep stencil for clipped nodes; only the texture survives the capture.
    Ref<CCRenderTexture> surface = CCRenderTexture::create(width, height,
        kCCTexture2DPixelFormat_RGBA8888, GL_DEPTH24_STENCIL8);
    if (!surface || !surface->getSprite() || !surface->getSprite()->getTexture()) return nullptr;
    surface->beginWithClear(0.f, 0.f, 0.f, 1.f, 1.f, 0);
    kmGLMatrixMode(KM_GL_MODELVIEW);
    kmGLPushMatrix();
    kmGLScalef(width / size.width, height / size.height, 1.f);
    node->visit();
    kmGLPopMatrix();
    sealAlpha();
    surface->end();
    Ref<CCTexture2D> texture = surface->getSprite()->getTexture();
    texture->setAntiAliasTexParameters();
    return texture;
}

void Visual::setDestination(Ref<CCTexture2D> to) {
    m_to = to;
    m_dirty = true;
}

void Visual::setProgress(float progress) {
    progress = limit(progress, 0.f, 0.f, 1.f);
    if (progress == m_progress && !m_dirty) return;
    m_progress = progress;
    m_dirty = true;
}

void Visual::rebuild() {
    m_vertices.clear();
    m_batches.clear();
    if (!m_from) return;
    Mesh mesh(m_vertices, m_batches, m_pixels);
    float width = m_size.width, height = m_size.height;
    Rect screen{0.f, 0.f, width, height};
    auto mapping = [](CCTexture2D* texture, Rect content) {
        return Mapping{content, texture->getMaxS(), texture->getMaxT()};
    };
    if (!m_to) {
        mesh.use(m_from, mapping(m_from, screen));
        mesh.quad(screen, premultiplied(1.f, 1.f));
        return;
    }

    auto frame = evaluate(m_config, m_origin, width, height, m_progress, m_backwards, m_panel);
    CCTexture2D* back = m_backwards ? m_to.data() : m_from.data();
    CCTexture2D* front = m_backwards ? m_from.data() : m_to.data();
    auto drawLayer = [&](CCTexture2D* texture, Layer const& layer) {
        if (layer.opacity <= .002f) return;
        mesh.use(texture, mapping(texture, layer.content));
        auto color = premultiplied(layer.brightness, layer.opacity);
        if (coversScreen(layer.clip, layer.radius, width, height)) mesh.quad(screen, color);
        else mesh.shape(layer.clip, layer.radius, color);
    };
    auto const& foreground = frame.foreground;
    bool covered = foreground.opacity >= .999f &&
        coversScreen(foreground.clip, foreground.radius, width, height);
    if (!covered) {
        auto const& background = frame.background;
        if (!coversScreen(background.clip, background.radius, width, height)) {
            mesh.use(nullptr);
            mesh.quad(screen, kBackdrop);
        }
        drawLayer(back, background);
        if (frame.shadow > .002f) {
            mesh.use(nullptr);
            mesh.shadow(foreground.clip, foreground.radius, frame.shadowBlur, frame.shadow);
        }
        drawLayer(back, frame.button);
    }
    drawLayer(front, foreground);
}

void Visual::draw() {
    if (m_dirty) {
        rebuild();
        m_dirty = false;
    }
    if (m_vertices.empty()) return;
    auto* shaders = CCShaderCache::sharedShaderCache();
    auto* textured = shaders->programForKey(kCCShader_PositionTextureColor);
    auto* plain = shaders->programForKey(kCCShader_PositionColor);
    if (!textured || !plain) return;

    ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    // Client arrays keep foreign VBO state out of the draw.
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    constexpr GLsizei stride = sizeof(Vertex);
    for (auto const& batch : m_batches) {
        if (batch.count <= 0) continue;
        auto const* base = &m_vertices[static_cast<size_t>(batch.first)];
        auto* program = batch.texture ? textured : plain;
        program->use();
        program->setUniformsForBuiltins();
        if (batch.texture) {
            ccGLBindTexture2D(batch.texture->getName());
            ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);
            glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, stride, &base->texCoords);
        } else {
            ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_Color);
        }
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, stride, &base->vertices);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, &base->colors);
        glDrawArrays(GL_TRIANGLES, 0, batch.count);
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif
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
    // The destination is captured on the next frame so the two captures never share one hitch.
    auto from = Visual::capture(m_pOutScene, m_size, m_config.quality);
    m_visual = from ? Visual::create(from, nullptr, m_size, m_config, m_origin,
        m_backwards, m_morphButton) : nullptr;
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
    if (m_finished || !m_visual) return;
    if (!CCDirector::get()->getWinSize().equals(m_size)) {
        abandonVisual();
        complete();
        return;
    }
    if (!m_visual->hasDestination()) {
        auto to = Visual::capture(m_pInScene, m_size, m_config.quality);
        if (!to) {
            abandonVisual();
            complete();
            return;
        }
        m_visual->setDestination(to);
        m_firstStep = true;
        return;
    }
    m_elapsed = std::min(m_fDuration, m_elapsed + frameStep(dt, m_firstStep));
    m_firstStep = false;
    m_visual->setProgress(m_fDuration > 0.f ? m_elapsed / m_fDuration : 1.f);
    if (m_elapsed >= m_fDuration) complete();
}

void DynamicTransitionScene::abandonVisual() {
    if (m_visual) m_visual->setVisible(false);
    m_visual = nullptr;
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
