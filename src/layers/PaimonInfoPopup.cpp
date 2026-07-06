#include "PaimonInfoPopup.hpp"
#include "../features/emotes/EmoteRenderer.hpp"
#include "../utils/DynamicPopupRegistry.hpp"
#include "../utils/Shaders.hpp"
#include "../blur/BlurSystem.hpp"
#include "../utils/ImageLoadHelper.hpp"
#include <Geode/ui/MDTextArea.hpp>
#include <random>
#include <filesystem>

using namespace geode::prelude;
using namespace cocos2d;

static std::optional<std::filesystem::path> pickRandomThumb() {
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;

    auto cacheDir = Mod::get()->getSaveDir() / "thumbs";
    if (std::filesystem::exists(cacheDir, ec)) {
        for (auto& e : std::filesystem::directory_iterator(cacheDir, ec)) {
            if (ec) break;
            if (!e.is_regular_file()) continue;
            auto ext = geode::utils::string::toLower(
                geode::utils::string::pathToString(e.path().extension()));
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".rgb")
                candidates.push_back(e.path());
        }
    }

    if (candidates.empty()) {
        auto gallery = Mod::get()->getSaveDir() / "pet_gallery";
        if (std::filesystem::exists(gallery, ec)) {
            for (auto& e : std::filesystem::directory_iterator(gallery, ec)) {
                if (ec) break;
                if (!e.is_regular_file()) continue;
                auto ext = geode::utils::string::toLower(
                    geode::utils::string::pathToString(e.path().extension()));
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                    candidates.push_back(e.path());
            }
        }
    }

    if (candidates.empty()) return std::nullopt;

    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(rng)];
}

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
            if (auto emoteNode = paimon::emotes::EmoteRenderer::renderComment(desc, 18.f, 300.f, "chatFont.fnt", 1.0f)) {
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
    if (!thumbPath.has_value()) return;

    auto img = ImageLoadHelper::loadStaticImage(thumbPath.value());
    if (!img.success || !img.texture) return;

    // RAII guard: auto-releases the texture when scope exits
    Ref<CCTexture2D> texGuard(img.texture);

    auto popupSize = m_size;

    auto* blurredSpr = BlurSystem::getInstance()->createBlurredSprite(img.texture, popupSize, 0.06f);
    CCSprite* bgSpr = nullptr;

    if (blurredSpr) {
        blurredSpr->setFlipY(true);

        auto texSize = blurredSpr->getContentSize();
        float scX = popupSize.width / texSize.width;
        float scY = popupSize.height / texSize.height;
        blurredSpr->setScale(std::max(scX, scY));

        blurredSpr->setOpacity(140);
        blurredSpr->setColor({180, 180, 200});
        bgSpr = blurredSpr;
    } else {
        // fallback: plain sprite
        auto* spr = CCSprite::createWithTexture(img.texture);
        if (spr) {
            float scX = popupSize.width / spr->getContentSize().width;
            float scY = popupSize.height / spr->getContentSize().height;
            spr->setScale(std::max(scX, scY));
            spr->setOpacity(60);
            bgSpr = spr;
        }
    }

    if (bgSpr) {
        CCSize clippedSize = {popupSize.width - 4.f, popupSize.height - 4.f};
        
        auto stencil = CCLayerColor::create({255, 255, 255, 255});
        stencil->setContentSize(clippedSize);
        stencil->setAnchorPoint({0.5f, 0.5f});
        stencil->ignoreAnchorPointForPosition(false);

        auto clip = CCClippingNode::create(stencil);
        clip->setAlphaThreshold(0.05f);
        clip->setAnchorPoint({0.5f, 0.5f});
        clip->ignoreAnchorPointForPosition(false);
        clip->setContentSize(clippedSize);

        bgSpr->setAnchorPoint({0.5f, 0.5f});
        bgSpr->setPosition(clippedSize / 2.f);
        clip->addChild(bgSpr);

        if (m_bgSprite) {
            clip->setPosition(m_bgSprite->getPosition());
            stencil->setPosition(clippedSize / 2.f);
        } else {
            auto content = m_mainLayer->getContentSize();
            clip->setPosition(content / 2.f);
            stencil->setPosition(clippedSize / 2.f);
        }

        // zOrder 1: above bg (0), below text/buttons (10+, 100)
        m_mainLayer->addChild(clip, 1);
    }
}


