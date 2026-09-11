#include "../services/GradientImage.hpp"
#include <Geode/modify/CCSprite.hpp>

using namespace geode::prelude;

namespace {
constexpr auto imageStateKey = "gradient-image-state"_spr;
class ImageState;
// Raw observer entries: the sprite's user object owns each state. Keep the
// small registry alive through cocos shutdown so destructors can unregister.
auto& imageStates() {
    static auto* states = new std::unordered_map<CCSprite*, ImageState*>;
    return *states;
}
class ImageState : public CCObject {
public:
    CCSprite* owner = nullptr;
    std::shared_ptr<paimon::icon_gradients::GradientImageAtlas> atlas;
    Ref<CCGLProgram> program;
    GLint originLoc = -1, uLoc = -1, vLoc = -1, gridLoc = -1;
    GLuint programID = 0;

    ~ImageState() override {
        imageStates().erase(owner);
    }
};
}

class $modify(GradientImageSprite, CCSprite) {
    void draw() {
        // Never ask Geode for node metadata on unrelated sprites: that lookup
        // can allocate metadata and used to run for every sprite in a level.
        auto& states = imageStates();
        if (states.empty()) return CCSprite::draw();
        auto found = states.find(this);
        if (found == states.end()) return CCSprite::draw();
        auto fields = found->second;
        if (fields && fields->atlas->texture && fields->program == getShaderProgram()) {
            auto program = getShaderProgram();
            program->use();
            ccGLBindTexture2DN(1, fields->atlas->texture->getName());
            // Use the actual quad so packed rotation and flipped frames map
            // the image consistently, without allocating a sprite frame.
            auto const& quad = m_sQuad;
            auto origin = quad.tl.texCoords;
            glUniform2f(fields->originLoc, origin.u, origin.v);
            glUniform2f(fields->uLoc,
                quad.tr.texCoords.u - origin.u, quad.tr.texCoords.v - origin.v);
            glUniform2f(fields->vLoc,
                quad.bl.texCoords.u - origin.u, quad.bl.texCoords.v - origin.v);
            glUniform2f(fields->gridLoc, fields->atlas->columns, fields->atlas->rows);
            glActiveTexture(GL_TEXTURE0);
        }
        CCSprite::draw();
    }
};

void paimon::icon_gradients::setGradientImage(CCSprite* sprite, std::shared_ptr<GradientImageAtlas> atlas) {
    auto& states = imageStates();
    auto found = states.find(sprite);
    auto fields = found == states.end() ? nullptr : found->second;
    if (!atlas) {
        if (fields) sprite->setUserObject(imageStateKey, nullptr);
        return;
    }
    if (!fields) {
        fields = new ImageState();
        fields->owner = sprite;
        sprite->setUserObject(imageStateKey, fields);
        states.emplace(sprite, fields);
        fields->release();
    }
    fields->atlas = std::move(atlas);
    auto program = sprite->getShaderProgram();
    auto id = program->getProgram();
    if (fields->program != program || fields->programID != id) {
        fields->program = program;
        fields->programID = id;
        fields->originLoc = glGetUniformLocation(id, "u_imageOrigin");
        fields->uLoc = glGetUniformLocation(id, "u_imageU");
        fields->vLoc = glGetUniformLocation(id, "u_imageV");
        fields->gridLoc = glGetUniformLocation(id, "u_imageAtlasGrid");
        program->use();
        glUniform1i(glGetUniformLocation(id, "u_image"), 1);
    }
}
