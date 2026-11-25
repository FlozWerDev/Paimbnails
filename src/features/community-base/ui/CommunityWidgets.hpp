#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>
#include <matjson.hpp>
#include <functional>
#include <string>

namespace paimon::community {

// framed image that fills itself in once the texture arrives. `source` is a
// public url, or a moderation media path when `moderation` is true.
cocos2d::CCNode* makeRemoteImage(std::string const& source, cocos2d::CCSize size, bool moderation = false);

// one-line text prompt (background name, rejection reason).
class TextPromptPopup : public PaimonPopup {
public:
    using Callback = std::function<void(std::string const& text)>;
    static TextPromptPopup* create(std::string const& title, std::string const& hint, size_t maxLength,
        bool required, Callback callback);
    ~TextPromptPopup() override;

protected:
    bool init(std::string const& title, std::string const& hint, size_t maxLength, bool required, Callback callback);

private:
    geode::TextInput* m_input = nullptr;
    bool m_required = true;
    Callback m_callback;
    void confirm();
};

// asks for an image and a name, then sends it to the moderation queue.
void suggestBackground();

// preview of any paimon base item: preset preview, background image, a doll
// painted with a shared gradient, or a doll wearing a shared custom icon.
// `shaderSlot` keeps gradient previews on separate shader programs so cards
// shown at the same time do not overwrite each other's uniforms.
cocos2d::CCNode* makeItemPreview(matjson::Value const& item, cocos2d::CCSize size, int shaderSlot, bool moderation = false);

// likes, dislikes and downloads, each with its own icon and number.
cocos2d::CCNode* makeCounters(int likes, int dislikes, int downloads, float scale = 0.32f);

// readable "pendiente / aprobado / rechazado".
char const* statusLabel(std::string const& status);
cocos2d::ccColor3B statusColor(std::string const& status);

} // namespace paimon::community
