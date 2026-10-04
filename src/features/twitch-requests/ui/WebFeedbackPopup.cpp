#include "WebFeedbackPopup.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/Base64.hpp"
#include "../../../utils/ImageConverter.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/binding/ButtonSprite.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr ccColor4F kInk[] = {
    {1.f, .16f, .16f, 1.f}, {1.f, .86f, .15f, 1.f}, {.12f, .92f, 1.f, 1.f}
};
constexpr uint8_t kRgb[][3] = {
    {255, 41, 41}, {255, 219, 38}, {31, 235, 255}
};

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

void addButton(CCMenu* menu, char const* label, CCPoint position,
    std::function<void()> action, float scale = .62f,
    paimon::ui::Btn skin = paimon::ui::Btn::Cyan) {
    auto* sprite = paimon::ui::makeButtonSprite(label, skin, 0.f, scale, "bigFont.fnt");
    if (!sprite) return;
    auto* item = CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) { action(); });
    item->setPosition(position);
    menu->addChild(item);
}

void writeDisk(std::vector<uint8_t>& rgba, int w, int h, int x, int y,
    int radius, int color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        int py = y + dy;
        if (py < 0 || py >= h) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            int px = x + dx;
            if (px < 0 || px >= w || dx * dx + dy * dy > radius * radius) continue;
            size_t at = (static_cast<size_t>(py) * w + px) * 4;
            rgba[at] = kRgb[color][0];
            rgba[at + 1] = kRgb[color][1];
            rgba[at + 2] = kRgb[color][2];
            rgba[at + 3] = 255;
        }
    }
}

void writeLine(std::vector<uint8_t>& rgba, int w, int h,
    CCPoint a, CCPoint b, int color) {
    float x0 = a.x * (w - 1), y0 = (1.f - a.y) * (h - 1);
    float x1 = b.x * (w - 1), y1 = (1.f - b.y) * (h - 1);
    int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(x1 - x0, y1 - y0))));
    int radius = std::max(2, w / 250);
    for (int i = 0; i <= steps; ++i) {
        float t = static_cast<float>(i) / steps;
        writeDisk(rgba, w, h,
            static_cast<int>(std::round(x0 + (x1 - x0) * t)),
            static_cast<int>(std::round(y0 + (y1 - y0) * t)), radius, color);
    }
}

} // namespace

WebFeedbackPopup* WebFeedbackPopup::create(LevelRequest request,
    CCTexture2D* texture, std::shared_ptr<uint8_t> rgba, int width, int height) {
    auto* popup = new WebFeedbackPopup();
    if (popup->init(std::move(request), texture, std::move(rgba), width, height)) {
        popup->autorelease();
        return popup;
    }
    delete popup;
    return nullptr;
}

bool WebFeedbackPopup::init(LevelRequest request, CCTexture2D* texture,
    std::shared_ptr<uint8_t> rgba, int width, int height) {
    if (!texture || !rgba || width <= 0 || height <= 0 || !PaimonPopup::init(510.f, 310.f)) return false;
    m_request = std::move(request);
    m_rgba = std::move(rgba);
    m_width = width;
    m_height = height;
    setTitle("Feedback del nivel");
    addCorners();
    addInfoButton("Feedback del nivel",
        "Marca la captura y manda tu veredicto a quien lo pidio desde la web.\n\n"
        "<cy>Pluma</c> y <cj>Circulo</c> dibujan sobre la imagen; <co>Color</c> cambia de "
        "tinta y <cy>Deshacer</c> quita la ultima marca. Los emotes se anaden al comentario.\n\n"
        "<cj>Feedback</c> solo guarda nota y porcentaje; <cg>Aceptar</c> y <cr>Rechazar</c> "
        "cierran el pedido. El rechazo necesita un motivo.");

    namespace ui = paimon::ui;

    if (auto* frame = ui::makeInset({308.f, 177.f}, 120)) {
        frame->setPosition({10.f, 88.f});
        m_mainLayer->addChild(frame, 1);
    }

    float drawW = std::min(300.f, 169.f * width / height);
    float drawH = drawW * height / width;
    m_image = CCNode::create();
    m_image->setContentSize({drawW, drawH});
    m_image->setPosition({14.f + (300.f - drawW) / 2.f, 92.f + (169.f - drawH) / 2.f});
    m_mainLayer->addChild(m_image, 2);
    auto* preview = CCSprite::createWithTexture(texture);
    if (!preview) return false;
    preview->setAnchorPoint({0.f, 0.f});
    preview->setScaleX(drawW / preview->getContentSize().width);
    preview->setScaleY(drawH / preview->getContentSize().height);
    m_image->addChild(preview);
    m_marksNode = CCDrawNode::create();
    m_image->addChild(m_marksNode, 2);

    if (auto* fields = ui::makeInset({182.f, 177.f}, 120)) {
        fields->setPosition({320.f, 88.f});
        m_mainLayer->addChild(fields, 1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    m_mainLayer->addChild(menu, 5);
    addButton(menu, "Pluma", {51.f, 73.f}, [this] { m_tool = Tool::Pen; });
    addButton(menu, "Circulo", {132.f, 73.f}, [this] { m_tool = Tool::Circle; });
    addButton(menu, "Color", {217.f, 73.f}, [this] { m_color = (m_color + 1) % 3; });
    addButton(menu, "Deshacer", {300.f, 73.f}, [this] {
        if (!m_marks.empty()) m_marks.pop_back();
        redraw();
    });
    addButton(menu, ":)", {343.f, 91.f}, [this] { appendEmote(" 🙂"); }, .48f, ui::Btn::Pink);
    addButton(menu, "<3", {402.f, 91.f}, [this] { appendEmote(" ❤️"); }, .48f, ui::Btn::Pink);
    addButton(menu, "!", {461.f, 91.f}, [this] { appendEmote(" 🔥"); }, .48f, ui::Btn::Pink);

    auto addLabel = [this](char const* text, float y) {
        auto* label = paimon::ui::makeLabel(text, 180.f, .35f);
        label->setAnchorPoint({0.f, .5f});
        label->setPosition({335.f, y});
        m_mainLayer->addChild(label, 3);
    };
    addLabel("Feedback", 252.f);
    m_note = TextInput::create(170.f, "Comentario...", "chatFont.fnt");
    m_note->setMaxCharCount(255);
    m_note->setPosition({412.f, 226.f});
    m_mainLayer->addChild(m_note, 3);
    addLabel("Motivo", 201.f);
    m_reason = TextInput::create(170.f, "Opcional", "chatFont.fnt");
    m_reason->setMaxCharCount(120);
    m_reason->setPosition({412.f, 175.f});
    m_mainLayer->addChild(m_reason, 3);
    addLabel("Porcentaje", 151.f);
    m_percent = TextInput::create(170.f, "0-100", "chatFont.fnt");
    m_percent->setFilter("0123456789");
    m_percent->setMaxCharCount(3);
    m_percent->setString(std::to_string(m_request.percent));
    m_percent->setPosition({412.f, 126.f});
    m_mainLayer->addChild(m_percent, 3);

    addButton(menu, "Feedback", {80.f, 30.f}, [this] { send("feedback"); }, .72f, ui::Btn::Cyan);
    addButton(menu, "Aceptar", {250.f, 30.f}, [this] { send("accepted"); }, .72f, ui::Btn::Green);
    addButton(menu, "Rechazar", {425.f, 30.f}, [this] { send("rejected"); }, .72f, ui::Btn::Red);
    return true;
}

void WebFeedbackPopup::appendEmote(char const* emote) {
    if (!m_note) return;
    std::string text(m_note->getString());
    text += emote;
    m_note->setString(text);
}

bool WebFeedbackPopup::pointOnImage(CCPoint world, CCPoint& normalized) const {
    if (!m_image) return false;
    auto point = m_image->convertToNodeSpace(world);
    auto size = m_image->getContentSize();
    if (point.x < 0 || point.y < 0 || point.x > size.width || point.y > size.height) return false;
    normalized = ccp(point.x / size.width, point.y / size.height);
    return true;
}

void WebFeedbackPopup::registerWithTouchDispatcher() {
    auto* dispatcher = CCDirector::get()->getTouchDispatcher();
    dispatcher->addTargetedDelegate(this, dispatcher->getTargetPrio() - 1, true);
}

bool WebFeedbackPopup::ccTouchBegan(CCTouch* touch, CCEvent*) {
    CCPoint point;
    if (!isVisible() || m_sending || !pointOnImage(touch->getLocation(), point)) return false;
    m_marks.push_back({m_tool, m_color, {point, point}});
    m_drawing = true;
    redraw();
    return true;
}

void WebFeedbackPopup::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (!m_drawing || m_marks.empty()) return;
    auto local = m_image->convertToNodeSpace(touch->getLocation());
    auto size = m_image->getContentSize();
    CCPoint point{std::clamp(local.x / size.width, 0.f, 1.f),
        std::clamp(local.y / size.height, 0.f, 1.f)};
    auto& mark = m_marks.back();
    if (mark.tool == Tool::Circle) mark.points.back() = point;
    else mark.points.push_back(point);
    redraw();
}

void WebFeedbackPopup::ccTouchEnded(CCTouch*, CCEvent*) { m_drawing = false; }
void WebFeedbackPopup::ccTouchCancelled(CCTouch*, CCEvent*) { m_drawing = false; }

void WebFeedbackPopup::redraw() {
    if (!m_marksNode || !m_image) return;
    m_marksNode->clear();
    auto size = m_image->getContentSize();
    for (auto const& mark : m_marks) {
        auto toView = [size](CCPoint p) { return ccp(p.x * size.width, p.y * size.height); };
        if (mark.tool == Tool::Pen) {
            for (size_t i = 1; i < mark.points.size(); ++i)
                m_marksNode->drawSegment(toView(mark.points[i - 1]), toView(mark.points[i]), 1.4f, kInk[mark.color]);
        } else if (mark.points.size() >= 2) {
            auto a = mark.points.front(), b = mark.points.back();
            CCPoint center{(a.x + b.x) * .5f, (a.y + b.y) * .5f};
            float rx = std::abs(a.x - b.x) * .5f, ry = std::abs(a.y - b.y) * .5f;
            for (int i = 0; i < 72; ++i) {
                float t0 = static_cast<float>(i) * 2.f * 3.14159265f / 72.f;
                float t1 = static_cast<float>(i + 1) * 2.f * 3.14159265f / 72.f;
                m_marksNode->drawSegment(toView({center.x + rx * std::cos(t0), center.y + ry * std::sin(t0)}),
                    toView({center.x + rx * std::cos(t1), center.y + ry * std::sin(t1)}), 1.4f, kInk[mark.color]);
            }
        }
    }
}

void WebFeedbackPopup::send(std::string decision) {
    if (m_sending) return;
    std::string note = m_note ? std::string(m_note->getString()) : std::string{};
    std::string reason = m_reason ? std::string(m_reason->getString()) : std::string{};
    if (decision == "rejected" && reason.empty()) {
        PaimonNotify::create("Escribe el motivo del rechazo", NotificationIcon::Warning)->show();
        return;
    }
    int percent = 0;
    std::string value = m_percent ? std::string(m_percent->getString()) : std::string{};
    if (!value.empty()) {
        auto [ptr, error] = std::from_chars(value.data(), value.data() + value.size(), percent);
        if (error != std::errc{} || ptr != value.data() + value.size() || percent > 100) {
            PaimonNotify::create("El porcentaje debe estar entre 0 y 100", NotificationIcon::Warning)->show();
            return;
        }
    }
    m_sending = true;
    PaimonNotify::create("Preparando feedback...", NotificationIcon::Info)->show();
    auto marks = m_marks;
    auto rgba = m_rgba;
    int srcW = m_width, srcH = m_height;
    LevelRequest request = m_request;
    geode::WeakRef<WebFeedbackPopup> weak = this;
    paimon::ThreadTracker::get().spawn([marks = std::move(marks), rgba, srcW, srcH,
        request, decision = std::move(decision), percent, note = std::move(note),
        reason = std::move(reason), weak]() mutable {
        geode::utils::thread::setName("PaimonWebFeedback");
        float scale = std::min(1.f, std::sqrt(220000.f / (static_cast<float>(srcW) * srcH)));
        int w = std::max(1, static_cast<int>(srcW * scale));
        int h = std::max(1, static_cast<int>(srcH * scale));
        std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            int srcY = std::min(srcH - 1, y * srcH / h);
            for (int x = 0; x < w; ++x) {
                int srcX = std::min(srcW - 1, x * srcW / w);
                size_t src = (static_cast<size_t>(srcY) * srcW + srcX) * 4;
                size_t dst = (static_cast<size_t>(y) * w + x) * 4;
                std::copy_n(rgba.get() + src, 4, pixels.data() + dst);
            }
        }
        for (auto const& mark : marks) {
            if (mark.points.size() < 2) continue;
            if (mark.tool == Tool::Pen) {
                for (size_t i = 1; i < mark.points.size(); ++i)
                    writeLine(pixels, w, h, mark.points[i - 1], mark.points[i], mark.color);
            } else {
                auto a = mark.points.front(), b = mark.points.back();
                CCPoint center{(a.x + b.x) * .5f, (a.y + b.y) * .5f};
                float rx = std::abs(a.x - b.x) * .5f, ry = std::abs(a.y - b.y) * .5f;
                CCPoint previous{center.x + rx, center.y};
                for (int i = 1; i <= 72; ++i) {
                    float t = static_cast<float>(i) * 2.f * 3.14159265f / 72.f;
                    CCPoint next{center.x + rx * std::cos(t), center.y + ry * std::sin(t)};
                    writeLine(pixels, w, h, previous, next, mark.color);
                    previous = next;
                }
            }
        }
        std::vector<uint8_t> png;
        bool encoded = ImageConverter::rgbaToPngBuffer(pixels.data(), w, h, png)
            && png.size() <= 1048576;
        std::string image = encoded ? paimon::base64Encode(png) : "";
        Loader::get()->queueInMainThread([weak, request, decision = std::move(decision), percent,
            note = std::move(note), reason = std::move(reason), image = std::move(image), encoded]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            auto popup = weak.lock();
            if (!popup) return;
            if (!encoded) {
                popup->m_sending = false;
                PaimonNotify::create("No se pudo preparar la captura", NotificationIcon::Error)->show();
                return;
            }
            bool started = TwitchRequestManager::get().sendWebFeedback(request, decision, percent,
                std::move(note), std::move(reason), std::move(image),
                [weak, request, decision, percent](bool success, std::string error) {
                    auto popup = weak.lock();
                    if (!success) {
                        if (popup) popup->m_sending = false;
                        PaimonNotify::create(error.c_str(), NotificationIcon::Error)->show();
                        return;
                    }
                    auto& manager = TwitchRequestManager::get();
                    auto requests = manager.requests();
                    for (size_t i = 0; i < requests.size(); ++i) {
                        if (requests[i].webRequestID != request.webRequestID) continue;
                        if (decision == "feedback") manager.setPercent(i, percent);
                        else manager.markPlayed(i, percent);
                        break;
                    }
                    PaimonNotify::create("Feedback enviado", NotificationIcon::Success)->show();
                    if (popup) popup->onClose(nullptr);
                });
            if (!started) {
                popup->m_sending = false;
                PaimonNotify::create("Conecta tu pagina de requests", NotificationIcon::Error)->show();
            }
        });
    });
}

} // namespace paimon::twitch
