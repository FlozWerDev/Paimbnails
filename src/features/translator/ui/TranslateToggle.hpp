#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <functional>
#include <string>

namespace paimon::translate {

// toggle that swaps a node's text between the original and a translation.
// anchored bottom-left at the position it is given.
class TranslateToggle : public cocos2d::CCNode {
public:
    static TranslateToggle* create(std::string original,
        std::function<void(std::string const&)> apply);

    void translate(bool silent);
    void restore();

protected:
    bool setup(std::string original, std::function<void(std::string const&)> apply);

private:
    void onPressed();
    void updateVisual();
    void request(bool silent);

    std::function<void(std::string const&)> m_apply;
    CCMenuItemSpriteExtra* m_button = nullptr;
    std::string m_original;
    std::string m_translated;
    std::string m_target;
    bool m_showing = false;
    bool m_busy = false;
    unsigned int m_request = 0;
};

}
