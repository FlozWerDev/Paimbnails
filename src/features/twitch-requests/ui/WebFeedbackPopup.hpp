#pragma once

#include "../TwitchRequestManager.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/TextInput.hpp>

#include <memory>
#include <vector>

namespace paimon::twitch {

class WebFeedbackPopup final : public geode::Popup {
public:
    static WebFeedbackPopup* create(LevelRequest request, cocos2d::CCTexture2D* texture,
        std::shared_ptr<uint8_t> rgba, int width, int height);

protected:
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void registerWithTouchDispatcher() override;

private:
    enum class Tool { Pen, Circle };
    struct Mark {
        Tool tool = Tool::Pen;
        int color = 0;
        std::vector<cocos2d::CCPoint> points;
    };

    bool init(LevelRequest request, cocos2d::CCTexture2D* texture,
        std::shared_ptr<uint8_t> rgba, int width, int height);
    void redraw();
    void send(std::string decision);
    void appendEmote(char const* emote);
    bool pointOnImage(cocos2d::CCPoint world, cocos2d::CCPoint& normalized) const;

    LevelRequest m_request;
    std::shared_ptr<uint8_t> m_rgba;
    int m_width = 0;
    int m_height = 0;
    cocos2d::CCNode* m_image = nullptr;
    cocos2d::CCDrawNode* m_marksNode = nullptr;
    geode::TextInput* m_note = nullptr;
    geode::TextInput* m_reason = nullptr;
    geode::TextInput* m_percent = nullptr;
    std::vector<Mark> m_marks;
    Tool m_tool = Tool::Pen;
    int m_color = 0;
    bool m_drawing = false;
    bool m_sending = false;
};

} // namespace paimon::twitch
