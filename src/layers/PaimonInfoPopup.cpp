#include "PaimonInfoPopup.hpp"
#include "../features/emotes/EmoteRenderer.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include "../blur/BlurSystem.hpp"
#include <Geode/ui/MDTextArea.hpp>
#include <Geode/utils/random.hpp>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

std::optional<std::filesystem::path> pickRandomThumb() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;

    auto collect = [&](std::filesystem::path const& dir, bool allowRgb) {
        if (!std::filesystem::exists(dir, ec)) return;
        for (auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (ec || !e.is_regular_file()) continue;
            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(e.path().extension()));
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || (allowRgb && ext == ".rgb"))
                candidates.push_back(e.path());
        }
    };

    collect(Mod::get()->getSaveDir() / "thumbs", true);
    if (candidates.empty())
        collect(Mod::get()->getSaveDir() / "pet_gallery", false);
    if (candidates.empty()) return std::nullopt;
    return geode::utils::random::choice(candidates);
}

} // namespace

PaimonInfoPopup* PaimonInfoPopup::create(std::string const& title, std::string const& desc) {
    auto ret = new PaimonInfoPopup();
    if (ret && ret->init(title, desc)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool PaimonInfoPopup::init(std::string const& title, std::string const& desc) {
    if (!Popup::init(340.f, 240.f)) return false;

    m_infoTitle = title;
    m_infoDesc = desc;
    this->setTitle(title.c_str());

    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    auto descLabel = geode::MDTextArea::create(desc, {300.f, 160.f});
    if (descLabel) {
        descLabel->setPosition({cx, content.height / 2.f + 10.f});
        descLabel->setZOrder(10);
        m_mainLayer->addChild(descLabel);

        if (paimon::emotes::EmoteRenderer::hasEmoteSyntax(desc)) {
            if (auto emoteNode = paimon::emotes::EmoteRenderer::renderComment(
                    desc, 18.f, 300.f, "chatFont.fnt", 1.0f)) {
                emoteNode->setAnchorPoint({0.5f, 0.5f});
                emoteNode->setPosition({cx, content.height / 2.f + 10.f});
                emoteNode->setZOrder(11);
                descLabel->setVisible(false);
                m_mainLayer->addChild(emoteNode);
            }
        }
    }

    loadRandomThumbnailBg();
    paimon::markDynamicPopup(this);
    return true;
}

void PaimonInfoPopup::loadRandomThumbnailBg() {
    auto thumbPath = pickRandomThumb();
    if (!thumbPath) return;

    auto img = ImageLoadHelper::loadStaticImage(*thumbPath);
    if (!img.success || !img.texture) return;

    Ref<CCTexture2D> texGuard(img.texture);
    auto popupSize = m_size;
    CCSprite* bgSpr = nullptr;

    if (auto* blurred = BlurSystem::getInstance()->createBlurredSprite(img.texture, popupSize, 0.06f)) {
        blurred->setFlipY(true);
        auto texSize = blurred->getContentSize();
        blurred->setScale(std::max(popupSize.width / texSize.width, popupSize.height / texSize.height));
        blurred->setOpacity(140);
        blurred->setColor({180, 180, 200});
        bgSpr = blurred;
    } else if (auto* spr = CCSprite::createWithTexture(img.texture)) {
        spr->setScale(std::max(
            popupSize.width / spr->getContentSize().width,
            popupSize.height / spr->getContentSize().height
        ));
        spr->setOpacity(60);
        bgSpr = spr;
    }
    if (!bgSpr) return;

    CCSize clipped = {popupSize.width - 4.f, popupSize.height - 4.f};
    auto stencil = CCLayerColor::create({255, 255, 255, 255});
    stencil->setContentSize(clipped);
    stencil->setAnchorPoint({0.5f, 0.5f});
    stencil->ignoreAnchorPointForPosition(false);

    auto clip = CCClippingNode::create(stencil);
    clip->setAlphaThreshold(0.05f);
    clip->setAnchorPoint({0.5f, 0.5f});
    clip->ignoreAnchorPointForPosition(false);
    clip->setContentSize(clipped);

    bgSpr->setAnchorPoint({0.5f, 0.5f});
    bgSpr->setPosition(clipped / 2.f);
    clip->addChild(bgSpr);

    if (m_bgSprite) {
        clip->setPosition(m_bgSprite->getPosition());
    } else {
        clip->setPosition(m_mainLayer->getContentSize() / 2.f);
    }
    stencil->setPosition(clipped / 2.f);
    m_mainLayer->addChild(clip, 1);
}
