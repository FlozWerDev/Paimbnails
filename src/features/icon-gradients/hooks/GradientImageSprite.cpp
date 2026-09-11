#include "../services/GradientImage.hpp"
#include <Geode/modify/CCSprite.hpp>

using namespace geode::prelude;

namespace {
constexpr auto imageStateKey = "gradient-image-state"_spr;
class ImageState : public CCObject {
public:
    Ref<CCTexture2D> image;
    Ref<CCGLProgram> program;
};
}

class $modify(GradientImageSprite, CCSprite) {
    void draw() {
        auto fields = static_cast<ImageState*>(getUserObject(imageStateKey));
        if (fields && fields->image && fields->program == getShaderProgram()) {
            auto program = getShaderProgram();
            program->use();
            ccGLBindTexture2DN(1, fields->image->getName());
            program->setUniformLocationWith1i(program->getUniformLocationForName("u_image"), 1);
            program->setUniformLocationWith1i(program->getUniformLocationForName("u_imageMode"), 1);
            // Use the actual quad so packed rotation and flipped frames map
            // the image consistently, without allocating a sprite frame.
            auto quad = getQuad();
            auto origin = quad.tl.texCoords;
            program->setUniformLocationWith2f(program->getUniformLocationForName("u_imageOrigin"), origin.u, origin.v);
            program->setUniformLocationWith2f(program->getUniformLocationForName("u_imageU"),
                quad.tr.texCoords.u - origin.u, quad.tr.texCoords.v - origin.v);
            program->setUniformLocationWith2f(program->getUniformLocationForName("u_imageV"),
                quad.bl.texCoords.u - origin.u, quad.bl.texCoords.v - origin.v);
            glActiveTexture(GL_TEXTURE0);
        }
        CCSprite::draw();
    }
};

void paimon::icon_gradients::setGradientImage(CCSprite* sprite, CCTexture2D* image) {
    auto fields = static_cast<ImageState*>(sprite->getUserObject(imageStateKey));
    if (!image) {
        if (fields) sprite->setUserObject(imageStateKey, nullptr);
        return;
    }
    if (!fields) {
        fields = new ImageState();
        sprite->setUserObject(imageStateKey, fields);
        fields->release();
    }
    fields->image = image;
    fields->program = image ? sprite->getShaderProgram() : nullptr;
}
