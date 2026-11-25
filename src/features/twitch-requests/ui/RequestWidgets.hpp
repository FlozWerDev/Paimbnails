#pragma once

#include <Geode/Geode.hpp>

#include <functional>
#include <string>
#include <vector>

// controls for the requests screen built on the stock GD button textures
// (GJ_button_01..06), so they look like the rest of the game.
namespace paimon::twitch::widgets {

struct Tone {
    char const* texture;
};

namespace tone {
inline constexpr Tone green{"GJ_button_01.png"};
inline constexpr Tone cyan{"GJ_button_02.png"};
inline constexpr Tone violet{"GJ_button_03.png"};
inline constexpr Tone slate{"GJ_button_04.png"};
inline constexpr Tone blue{"GJ_button_05.png"};
inline constexpr Tone red{"GJ_button_06.png"};
}

struct ButtonSpec {
    std::string label;
    char const* icon = nullptr;
    float iconRotation = 0.f;
    // keycap drawn at the right end, e.g. the shortcut that triggers the button.
    std::string badge;
    // final on-screen size; the stock button is 30 tall at scale 1.
    float width = 80.f;
    float height = 22.f;
    // relative label size, 0.4 is the default.
    float fontScale = 0.4f;
    Tone tone = tone::slate;
};

class ButtonFace final : public cocos2d::CCNodeRGBA {
public:
    static ButtonFace* create(ButtonSpec const& spec);

    void setLabel(std::string const& text);
    void setIcon(char const* frame, float rotation = 0.f);
    void setTone(Tone tone);
    void setMuted(bool muted);
    // small status light before the label; unlit lights fade to grey.
    void setDot(cocos2d::ccColor3B color, bool lit);

private:
    using CCNodeRGBA::init;
    bool init(ButtonSpec const& spec);
    void relayout();

    float m_width = 0.f;
    float m_height = 0.f;
    float m_pad = 7.f;
    float m_labelScale = 0.5f;
    Tone m_tone{};
    ButtonSprite* m_bg = nullptr;
    cocos2d::CCSprite* m_icon = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;
    cocos2d::CCNode* m_chip = nullptr;
    cocos2d::CCNode* m_dot = nullptr;
    cocos2d::ccColor4B m_dotPaint{0, 0, 0, 0};
    float m_chipWidth = 0.f;
};

CCMenuItemSpriteExtra* makeButton(ButtonSpec const& spec, std::function<void()> action);
void setLabel(CCMenuItemSpriteExtra* button, std::string const& text);
void setIcon(CCMenuItemSpriteExtra* button, char const* frame, float rotation = 0.f);
void setTone(CCMenuItemSpriteExtra* button, Tone tone);
void setMuted(CCMenuItemSpriteExtra* button, bool muted);
// quick scale pop to signal a state change.
void bounce(cocos2d::CCNode* node);

// a row of stock buttons where exactly one is lit.
class SegmentBar final : public cocos2d::CCNode {
public:
    struct Item {
        std::string label;
        bool dot = false;
    };

    static SegmentBar* create(std::vector<Item> items, cocos2d::CCSize size,
        int touchPriority, std::function<void(int)> onSelect, Tone selectedTone = tone::cyan);

    void select(int index, bool animate = true);
    int selected() const { return m_selected; }
    void setLabel(int index, std::string const& text);
    void setDot(int index, cocos2d::ccColor3B color, bool lit);

private:
    using CCNode::init;
    bool init(std::vector<Item> items, cocos2d::CCSize size, int touchPriority,
        std::function<void(int)> onSelect, Tone selectedTone);

    int m_selected = -1;
    Tone m_selectedTone = tone::cyan;
    std::function<void(int)> m_onSelect;
    std::vector<CCMenuItemSpriteExtra*> m_buttons;
};

}
