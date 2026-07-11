// Improved editor pan/zoom: Ctrl+wheel zoom (to cursor), Shift+wheel horizontal,
// smooth multipliers, min/max clamp. Pinch-to-zoom on mobile.

#include "../EditorModule.hpp"
#include "../EditorHelpers.hpp"

#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/modify/EditorUI.hpp>
#include <Geode/utils/cocos.hpp>
#include <cmath>
#include <numbers>
#include <unordered_set>

using namespace geode::prelude;
using namespace cocos2d;
using namespace paimon::editor;

namespace {

bool scrollOn() { return moduleEnabled("editor-mod-scroll-zoom"); }

float scrollMult() {
    return static_cast<float>(moduleSetting<double>("editor-mod-scroll-multiplier", 12.0));
}
float zoomMult() {
    return static_cast<float>(moduleSetting<double>("editor-mod-zoom-multiplier", 12.0));
}
float zoomMin() {
    auto value = static_cast<float>(moduleSetting<double>("editor-mod-zoom-min", 0.1));
    return std::isfinite(value) && value > 0.f ? value : 0.1f;
}
float zoomMax() {
    auto value = static_cast<float>(moduleSetting<double>("editor-mod-zoom-max", 1.0e7));
    return std::isfinite(value) && value > 0.f ? value : 1.0e7f;
}
bool zoomToCursor() {
    return moduleSetting<bool>("editor-mod-scroll-zoom-to-cursor", true);
}
bool invertV() { return moduleSetting<bool>("editor-mod-scroll-invert-v", false); }
bool invertH() { return moduleSetting<bool>("editor-mod-scroll-invert-h", false); }
bool invertZ() { return moduleSetting<bool>("editor-mod-scroll-invert-z", false); }

} // namespace

class $modify(PaimonEditorScrollUI, EditorUI) {
    struct Fields {
#ifdef GEODE_IS_MOBILE
        std::unordered_set<Ref<CCTouch>> touches;
        float pinchStartDist = 0.f;
        float pinchStartScale = 1.f;
        CCPoint pinchMid{};
#endif
    };

    $override
    void scrollWheel(float y, float x) {
        if (!scrollOn()) {
            return EditorUI::scrollWheel(y, x);
        }
        if (moduleSetting<bool>("editor-mod-scroll-delegate-vanilla", false)) {
            return EditorUI::scrollWheel(y, x);
        }
        if (!m_editorLayer || m_editorLayer->m_playbackMode == PlaybackMode::Playing) {
            return EditorUI::scrollWheel(y, x);
        }

        auto* objLayer = m_editorLayer->m_objectLayer;
        if (!objLayer) {
            return EditorUI::scrollWheel(y, x);
        }

        float dy = invertV() ? -y : y;
        float dx = invertH() ? -x : x;
        float zm = invertZ() ? -y : y;

        auto prevScale = objLayer->getScale();
        if (!std::isfinite(prevScale) || prevScale <= 0.f) {
            prevScale = clampZoom(1.f, zoomMin(), zoomMax());
            this->updateZoom(prevScale);
        }
        auto swipeStart = objLayer->convertToNodeSpace(m_swipeStart) * prevScale;

        auto* keys = CCKeyboardDispatcher::get();
        bool ctrl = keys && keys->getControlKeyPressed();
        bool shift = keys && keys->getShiftKeyPressed();

        if (ctrl) {
            auto zoom = objLayer->getScale();
            float factor = zm * 0.01f * (zoomMult() / 12.f);
            zoom = static_cast<float>(std::pow(
                std::numbers::e,
                std::log(std::max(zoom, 0.001f)) - factor
            ));
            zoom = clampZoom(zoom, zoomMin(), zoomMax());

            if (zoomToCursor()) {
                auto mousePos = getMousePos();
                auto prevPos = objLayer->convertToNodeSpace(mousePos);
                this->updateZoom(zoom);
                auto newPos = objLayer->convertToWorldSpace(prevPos);
                objLayer->setPosition(objLayer->getPosition() + mousePos - newPos);
            } else {
                this->updateZoom(zoom);
            }
        } else {
            float mult = scrollMult() / 6.f; // ~2.f at default 12
            if (!std::isfinite(mult)) mult = 2.f;
            if (shift) {
                objLayer->setPositionX(objLayer->getPositionX() - dy * mult);
            } else {
#ifdef GEODE_IS_MACOS
                // trackpad often reports horizontal via y
                objLayer->setPositionX(objLayer->getPositionX() + dy * mult);
                if (std::abs(dx) > 0.01f) {
                    objLayer->setPositionY(objLayer->getPositionY() + dx * mult);
                }
#else
                objLayer->setPositionY(objLayer->getPositionY() + dy * mult);
                if (std::abs(dx) > 0.01f) {
                    objLayer->setPositionX(objLayer->getPositionX() + dx * mult);
                }
#endif
            }
            // Update UI state without re-applying vanilla pan
            EditorUI::scrollWheel(0.f, 0.f);
        }

        // Keep swipe rect aligned after pan/zoom
        auto newSwipeStart = objLayer->convertToNodeSpace(m_swipeStart) * prevScale;
        auto const currentScale = objLayer->getScale();
        if (std::isfinite(currentScale) && currentScale > 0.f) {
            auto rel = (swipeStart - newSwipeStart) * (currentScale / prevScale);
            if (std::isfinite(rel.x) && std::isfinite(rel.y)) {
                m_swipeStart = m_swipeStart + rel;
            }
        }
    }

#ifdef GEODE_IS_MOBILE
    $override
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) {
        if (scrollOn() && moduleSetting<bool>("editor-mod-pinch-to-zoom", true) && touch) {
            m_fields->touches.insert(touch);
            if (m_fields->touches.size() == 2 && m_editorLayer && m_editorLayer->m_objectLayer) {
                auto it = m_fields->touches.begin();
                auto* a = it->data();
                ++it;
                auto* b = it->data();
                if (a && b) {
                    auto pa = a->getLocation();
                    auto pb = b->getLocation();
                    m_fields->pinchStartDist = pa.getDistance(pb);
                    m_fields->pinchStartScale = m_editorLayer->m_objectLayer->getScale();
                    m_fields->pinchMid = (pa + pb) / 2.f;
                }
            }
        }
        return EditorUI::ccTouchBegan(touch, event);
    }

    $override
    void ccTouchMoved(CCTouch* touch, CCEvent* event) {
        if (scrollOn() && moduleSetting<bool>("editor-mod-pinch-to-zoom", true)
            && m_fields->touches.size() >= 2
            && m_editorLayer && m_editorLayer->m_objectLayer
            && m_editorLayer->m_playbackMode != PlaybackMode::Playing) {
            auto it = m_fields->touches.begin();
            auto* a = it->data();
            ++it;
            auto* b = it->data();
            if (a && b && m_fields->pinchStartDist > 1.f) {
                auto pa = a->getLocation();
                auto pb = b->getLocation();
                float dist = pa.getDistance(pb);
                float zoom = m_fields->pinchStartScale * (dist / m_fields->pinchStartDist);
                zoom = clampZoom(zoom, zoomMin(), zoomMax());
                if (std::isfinite(zoom)) {
                    auto* layer = m_editorLayer->m_objectLayer;
                    auto mid = (pa + pb) / 2.f;
                    auto prevPos = layer->convertToNodeSpace(m_fields->pinchMid);
                    this->updateZoom(zoom);
                    auto newPos = layer->convertToWorldSpace(prevPos);
                    layer->setPosition(layer->getPosition() + mid - newPos);
                    m_fields->pinchMid = mid;
                }
            }
        }
        EditorUI::ccTouchMoved(touch, event);
    }

    $override
    void ccTouchEnded(CCTouch* touch, CCEvent* event) {
        if (touch) m_fields->touches.erase(touch);
        EditorUI::ccTouchEnded(touch, event);
    }

    $override
    void ccTouchCancelled(CCTouch* touch, CCEvent* event) {
        if (touch) m_fields->touches.erase(touch);
        EditorUI::ccTouchCancelled(touch, event);
    }
#endif
};
