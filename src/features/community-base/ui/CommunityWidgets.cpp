#include "CommunityWidgets.hpp"
#include "../services/CommunityClient.hpp"
#include "../services/CommunityItems.hpp"
#include "../services/CommunityMediaCache.hpp"
#include "../../global-icon/GlobalIconGradients.hpp"
#include "../../global-icon/services/GlobalIconStorage.hpp"
#include "../../icon-gradients/GradientUtils.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/BoundedFileRead.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <memory>

#include "../../../framework/compat/MoreIconsAPI.hpp"

using namespace geode::prelude;

namespace paimon::community {

namespace {
constexpr size_t kMaxBackgroundBytes = 4 * 1024 * 1024;

// magic bytes decide the content type; the server checks them again.
char const* imageContentType(std::vector<uint8_t> const& data) {
    if (data.size() >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') return "image/png";
    if (data.size() >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) return "image/jpeg";
    if (data.size() >= 12 && std::string_view(reinterpret_cast<char const*>(data.data()), 4) == "RIFF"
        && std::string_view(reinterpret_cast<char const*>(data.data()) + 8, 4) == "WEBP") return "image/webp";
    return nullptr;
}
} // namespace

CCNode* makeRemoteImage(std::string const& source, CCSize size, bool moderation) {
    auto* holder = CCNode::create();
    holder->setContentSize(size);
    holder->addChild(paimon::ui::makeInset(size, 140, {10, 17, 30}));
    auto* placeholder = paimon::ui::makeLabel("...", size.width - 8.f, 0.4f, paimon::ui::palette::dim);
    placeholder->setPosition(size / 2.f);
    holder->addChild(placeholder);
    if (source.empty()) return holder;
    WeakRef<CCNode> weak = holder;
    Ref<CCLabelBMFont> label = placeholder;
    auto onTexture = [weak, label, size](CCTexture2D* texture) {
        auto node = weak.lock();
        if (!node) return;
        if (!texture) {
            label->setString(tx("Sin imagen", "No image"));
            return;
        }
        auto* sprite = CCSprite::createWithTexture(texture);
        if (!sprite) return;
        auto content = sprite->getContentSize();
        sprite->setScale(std::min((size.width - 4.f) / std::max(1.f, content.width), (size.height - 4.f) / std::max(1.f, content.height)));
        sprite->setPosition(size / 2.f);
        label->setVisible(false);
        node->addChild(sprite);
    };
    if (moderation) CommunityMediaCache::get().loadModeration(source, onTexture);
    else CommunityMediaCache::get().load(source, onTexture);
    return holder;
}

TextPromptPopup* TextPromptPopup::create(std::string const& title, std::string const& hint, size_t maxLength,
    bool required, Callback callback) {
    auto popup = std::make_unique<TextPromptPopup>();
    if (!popup->init(title, hint, maxLength, required, std::move(callback))) return nullptr;
    popup->autorelease();
    return popup.release();
}

TextPromptPopup::~TextPromptPopup() { paimon::ui::detachGeodeTextInput(m_input); }

bool TextPromptPopup::init(std::string const& title, std::string const& hint, size_t maxLength, bool required, Callback callback) {
    if (!PaimonPopup::init(320.f, 150.f)) return false;
    m_required = required;
    m_callback = std::move(callback);
    setTitle(title, "goldFont.fnt", 0.55f);
    paimon::markDynamicPopup(this);
    m_input = TextInput::create(260.f, hint, "chatFont.fnt");
    m_input->setCommonFilter(CommonFilter::TextEmojis);
    m_input->setMaxCharCount(static_cast<int>(maxLength));
    m_input->setPosition({160.f, 82.f});
    m_mainLayer->addChild(m_input);
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu);
    auto* ok = paimon::ui::makeButton(tx("Aceptar", "Confirm"), [this] { confirm(); }, paimon::ui::Btn::Green, 110.f, 0.5f, "bigFont.fnt");
    ok->setPosition({160.f, 30.f});
    menu->addChild(ok);
    return true;
}

void TextPromptPopup::confirm() {
    auto text = std::string(m_input->getString());
    auto first = text.find_first_not_of(" \t");
    text = first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \t") - first + 1);
    if (m_required && text.empty()) {
        PaimonNotify::show(tx("Escribe algo primero.", "Type something first."), NotificationIcon::Warning);
        return;
    }
    auto callback = m_callback;
    onClose(nullptr);
    if (callback) callback(text);
}

void suggestBackground() {
    if (!currentIdentity()) {
        PaimonNotify::show(tx("Inicia sesion en Geometry Dash.", "Log in to Geometry Dash."), NotificationIcon::Warning);
        return;
    }
    pt::pickImage([](geode::Result<std::optional<std::filesystem::path>> result) {
        if (!result) { PaimonNotify::show(result.unwrapErr().c_str(), NotificationIcon::Error); return; }
        if (!result.unwrap()) return;
        auto bytes = paimon::file::readBytes(*result.unwrap(), kMaxBackgroundBytes + 1);
        if (bytes.empty() || bytes.size() > kMaxBackgroundBytes) {
            PaimonNotify::show(tx("La imagen debe pesar menos de 4 MB.", "The image must be under 4 MB."), NotificationIcon::Error);
            return;
        }
        auto* type = imageContentType(bytes);
        if (!type) {
            PaimonNotify::show(tx("Usa una imagen PNG, JPG o WEBP.", "Use a PNG, JPG or WEBP image."), NotificationIcon::Error);
            return;
        }
        auto shared = std::make_shared<std::vector<uint8_t>>(std::move(bytes));
        std::string contentType = type;
        auto* prompt = TextPromptPopup::create(tx("Nombre del fondo", "Background name"),
            tx("Ej: Atardecer neon", "E.g. Neon sunset"), 40, true,
            [shared, contentType](std::string const& title) {
                PaimonNotify::show(tx("Enviando fondo...", "Sending background..."), NotificationIcon::Loading);
                CommunityClient::get().submitBackground(title, *shared, contentType,
                    [](bool ok, matjson::Value const& json, std::string const& error) {
                        if (!ok) { PaimonNotify::show(error.c_str(), NotificationIcon::Error); return; }
                        bool approved = json["status"].asString().unwrapOr("") == "approved";
                        PaimonNotify::show(approved
                            ? tx("Fondo publicado.", "Background published.")
                            : tx("Fondo enviado. Aparecera cuando un moderador lo apruebe.",
                                 "Background sent. It appears once a moderator approves it."),
                            NotificationIcon::Success);
                    });
            });
        if (prompt) showOnTop(prompt);
    });
}

namespace {

SimplePlayer* makeDoll(IconType type, float box, int frame = 1) {
    auto* doll = SimplePlayer::create(1);
    if (!doll) return nullptr;
    doll->updatePlayerFrame(std::max(1, frame), type);
    if (auto* gm = GameManager::get()) {
        doll->setColor(gm->colorForIdx(gm->getPlayerColor()));
        doll->setSecondColor(gm->colorForIdx(gm->getPlayerColor2()));
        doll->m_hasGlowOutline = gm->getPlayerGlow();
        if (doll->m_hasGlowOutline) doll->enableCustomGlowColor(gm->colorForIdx(gm->getPlayerGlowColor()));
        doll->updateColors();
    }
    doll->setScale(box / 32.f);
    return doll;
}

CCNode* centered(CCNode* holder, CCNode* child, CCSize size) {
    if (child) {
        child->setPosition(size / 2.f);
        holder->addChild(child, 2);
    }
    return holder;
}

} // namespace

CCNode* makeItemPreview(matjson::Value const& item, CCSize size, int shaderSlot, bool moderation) {
    auto type = item["type"].asString().unwrapOr("");
    if (type == "presets" || type == "backgrounds") {
        auto source = moderation ? item["mediaPath"].asString().unwrapOr("")
            : item[type == "presets" ? "previewUrl" : "imageUrl"].asString().unwrapOr("");
        return makeRemoteImage(source, size, moderation);
    }
    auto* holder = CCNode::create();
    holder->setContentSize(size);
    holder->addChild(paimon::ui::makeInset(size, 140, {10, 17, 30}));
    auto iconType = paimon::globalicon::iconTypeFromString(item["iconType"].asString().unwrapOr(""));
    if (!iconType) return holder;
    float const box = std::min(size.width, size.height) * 0.7f;
    // gradients are shown on the player's own icon for that gamemode.
    int const frame = type == "gradients" ? paimon::icon_gradients::GradientUtils::getIconID(*iconType, false) : 1;
    auto* doll = makeDoll(*iconType, box, frame);
    if (!doll) return holder;
    centered(holder, doll, size);
    Ref<SimplePlayer> dollRef = doll;
    if (type == "gradients") {
        auto meta = items::gradientMeta(item);
        auto kind = *iconType;
        paimon::globalicon::ensureGradientImages(meta, [dollRef, meta, kind, shaderSlot](paimon::globalicon::GradientPaths const& paths) {
            if (!dollRef->getParent()) return;
            auto localized = paimon::globalicon::localizeGradients(meta.gradients, paths);
            paimon::icon_gradients::GradientUtils::applyGradient(dollRef.data(),
                paimon::globalicon::sharedGradient(localized, kind), false, false, 5100 + shaderSlot);
        });
        return holder;
    }
    // icons: the texture comes from the shared item through more icons.
    if (!paimon::globalicon::GlobalIconStorage::available()) {
        doll->setOpacity(90);
        auto* note = paimon::ui::makeLabel(tx("Necesita More Icons", "Needs More Icons"), size.width - 6.f, 0.26f, paimon::ui::palette::dim);
        note->setPosition({size.width / 2.f, 8.f});
        holder->addChild(note, 3);
        return holder;
    }
    doll->setOpacity(100);
    auto kind = *iconType;
    items::previewIcon(item, [dollRef, kind](std::string const& name) {
        if (!dollRef->getParent() || name.empty()) return;
        if (auto* info = more_icons::getIcon(name, kind)) {
            more_icons::updateSimplePlayer(dollRef.data(), info);
            dollRef->setOpacity(255);
        }
    });
    return holder;
}

CCNode* makeCounters(int likes, int dislikes, int downloads, float scale) {
    auto* row = CCNode::create();
    struct Counter { char const* frame; int value; ccColor3B color; };
    Counter const counters[] = {
        {"GJ_likesIcon_001.png", likes, paimon::ui::palette::success},
        {"GJ_dislikesIcon_001.png", dislikes, paimon::ui::palette::danger},
        {"GJ_downloadsIcon_001.png", downloads, paimon::ui::palette::info},
    };
    float x = 0.f;
    float const iconSize = 34.f * scale;
    for (auto const& counter : counters) {
        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(counter.frame)) {
            icon->setScale(iconSize / std::max(1.f, icon->getContentHeight()));
            icon->setAnchorPoint({0.f, 0.5f});
            icon->setPosition({x, 0.f});
            row->addChild(icon);
            x += icon->getScaledContentWidth() + 3.f;
        }
        auto* label = CCLabelBMFont::create(std::to_string(counter.value).c_str(), "bigFont.fnt");
        label->setScale(scale * 0.9f);
        label->setColor(counter.color);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({x, 0.f});
        row->addChild(label);
        x += label->getScaledContentWidth() + 12.f;
    }
    row->setContentSize({std::max(0.f, x - 12.f), iconSize});
    return row;
}

char const* statusLabel(std::string const& status) {
    if (status == "approved") return tx("Aprobado", "Approved");
    if (status == "rejected") return tx("Rechazado", "Rejected");
    return tx("Pendiente", "Pending");
}

ccColor3B statusColor(std::string const& status) {
    if (status == "approved") return paimon::ui::palette::success;
    if (status == "rejected") return paimon::ui::palette::danger;
    return paimon::ui::palette::warning;
}

} // namespace paimon::community
