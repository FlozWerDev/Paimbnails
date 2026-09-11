#include "DeathAnimation.hpp"
#include "../../../utils/GLSLLoader.hpp"
#include <algorithm>
#include <random>

using namespace geode::prelude;
namespace paimon::death_effects {
namespace {
constexpr auto kSelection = "death-animation-style";
constexpr auto kContainer = "death-animation-container";
CCGLProgram* program() {
    return paimon::shaders::loadShader("paimon-death-animation", "position.vert",
        "death_animation.fsh", nullptr, nullptr);
}
class AnimationSprite : public CCSprite {
    float m_elapsed = 0.f;
    int m_style = 0;
    ccColor3B m_tint{};
public:
    static AnimationSprite* create(int style, ccColor3B color) {
        auto shader = program();
        if (!shader) return nullptr;
        auto texture = new CCTexture2D();
        unsigned char white[] = {255,255,255,255};
        if (!texture->initWithData(white,kCCTexture2DPixelFormat_RGBA8888,1,1,{1,1})) {
            texture->release(); return nullptr;
        }
        auto node = new AnimationSprite();
        bool ok = node->initWithTexture(texture);
        texture->release();
        if (!ok) { delete node; return nullptr; }
        node->autorelease();
        node->m_style = style;
        node->m_tint = color;
        node->setShaderProgram(shader);
        node->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        node->setScale(180.f);
        node->scheduleUpdate();
        return node;
    }
    void update(float dt) override {
        m_elapsed += std::max(0.f, dt);
        if (m_elapsed >= 0.85f) removeFromParentAndCleanup(true);
    }
    void draw() override {
        auto shader = getShaderProgram();
        shader->use();
        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_progress"),m_elapsed/0.85f);
        shader->setUniformLocationWith1f(shader->getUniformLocationForName("u_style"),static_cast<float>(m_style));
        shader->setUniformLocationWith3f(shader->getUniformLocationForName("u_tint"),
            0.25f+0.75f*m_tint.r/255.f,0.25f+0.75f*m_tint.g/255.f,0.25f+0.75f*m_tint.b/255.f);
        CCSprite::draw();
    }
};
}
int selectedAnimation() {
    int value = Mod::get()->getSavedValue<int>(kSelection,-1);
    return value >= -1 && value <= 12 ? value : -1;
}
int resolveAnimation(int selection) {
    if (selection != 12) return selection;
    static std::mt19937 rng(std::random_device{}());
    static int previous = -1;
    int next = std::uniform_int_distribution<int>(0,previous < 0 ? 11 : 10)(rng);
    if (previous >= 0 && next >= previous) ++next;
    previous = next;
    return next;
}
void prewarmAnimation() { if (selectedAnimation() >= 0) (void)program(); }
bool spawnAnimation(CCNode* parent, CCPoint position, int style, ccColor3B color, float scale) {
    if (!parent || style < 0 || style >= 12) return false;
    auto effect = AnimationSprite::create(style,color);
    if (!effect) return false;
    auto container = parent->getChildByID(kContainer);
    if (!container) {
        container = CCNode::create();
        container->setID(kContainer);
        parent->addChild(container,1000);
    }
    // Dual mode and rapid deaths cannot grow the effect population unboundedly.
    if (container->getChildrenCount() >= 4) container->removeAllChildrenWithCleanup(true);
    effect->setPosition(position);
    effect->setScale(180.f*scale);
    container->addChild(effect);
    return true;
}
void clearAnimations(CCNode* parent) {
    if (parent) parent->removeChildByID(kContainer);
}
DeathAnimationPopup* DeathAnimationPopup::create() {
    auto ret = new DeathAnimationPopup();
    if (ret->init()) { ret->autorelease(); return ret; }
    delete ret; return nullptr;
}
bool DeathAnimationPopup::init() {
    if (!Popup::init(440.f,290.f)) return false;
    setTitle("Death Animations");
    setID("death-animation-popup"_spr);
    m_preview = CCNode::create();
    m_preview->setPosition({345,158});
    m_mainLayer->addChild(m_preview,5);
    m_previewHint = CCLabelBMFont::create("Uses your\nequipped effect", "bigFont.fnt");
    m_previewHint->setScale(0.3f);
    m_preview->addChild(m_previewHint);
    auto backdrop = CCLayerColor::create({12,18,32,220},148,164);
    backdrop->setPosition({270,76});
    m_mainLayer->addChild(backdrop);
    auto label = CCLabelBMFont::create("PREVIEW", "goldFont.fnt");
    label->setScale(0.45f); label->setPosition({344,228}); m_mainLayer->addChild(label);
    for (int i=0;i<14;++i) {
        auto name = i==0 ? "Original" : i==13 ? "Random" : kAnimationNames[i-1];
        auto sprite = ButtonSprite::create(name,"bigFont.fnt","GJ_button_04.png",0.65f);
        sprite->setScale(0.48f);
        sprite->setCascadeColorEnabled(true);
        auto item = CCMenuItemSpriteExtra::create(sprite,this,menu_selector(DeathAnimationPopup::onSelect));
        item->setTag(i-1);
        item->setPosition({80.f+(i%2)*120.f,228.f-(i/2)*27.f});
        m_buttonMenu->addChild(item); m_choices[i]=item;
    }
    auto sprite = ButtonSprite::create("Replay","bigFont.fnt","GJ_button_01.png",0.7f);
    sprite->setScale(0.5f);
    auto replay = CCMenuItemSpriteExtra::create(sprite,this,menu_selector(DeathAnimationPopup::onPreview));
    replay->setPosition({344,58}); m_buttonMenu->addChild(replay);
    m_status = CCLabelBMFont::create("","bigFont.fnt");
    m_status->setScale(0.35f); m_status->setPosition({220,24}); m_mainLayer->addChild(m_status);
    refresh(); onPreview(nullptr);
    return true;
}
void DeathAnimationPopup::refresh() {
    int selected = selectedAnimation();
    for (int i=0;i<14;++i) {
        auto sprite = typeinfo_cast<CCSprite*>(m_choices[i]->getNormalImage());
        if (sprite) sprite->setColor(i-1==selected ? ccColor3B{110,255,180} : ccColor3B{255,255,255});
    }
    auto name = selected < 0 ? "Original" : selected==12 ? "Random" : kAnimationNames[selected];
    m_status->setString(fmt::format("Selected: {}",name).c_str());
}
void DeathAnimationPopup::onSelect(CCObject* sender) {
    auto node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;
    Mod::get()->setSavedValue(kSelection,node->getTag());
    refresh(); onPreview(nullptr);
}
void DeathAnimationPopup::onPreview(CCObject*) {
    clearAnimations(m_preview);
    int style = resolveAnimation(selectedAnimation());
    m_previewHint->setVisible(style < 0);
    if (style < 0) return;
    if (!spawnAnimation(m_preview,{0,0},style,{100,220,255},0.78f)) {
        m_status->setString("Shader unavailable - original used");
    }
}
}
