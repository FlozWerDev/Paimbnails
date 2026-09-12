#include "IdBadge.hpp"
#include "../InfoModule.hpp"
#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace paimon::info {

namespace {

bool g_shiftHeld = false;

// Badges alive right now. Entries whose node lost its parent are dropped on the
// next sweep, so cells recycled by the table view do not pile up.
std::vector<Ref<CCLabelBMFont>>& badges() {
    static std::vector<Ref<CCLabelBMFont>> list;
    return list;
}

bool revealMode() {
    return moduleSetting<bool>("info-ids-shift-reveal", false);
}

void applyVisibility() {
    bool visible = idBadgesVisible();
    auto& list = badges();
    list.erase(std::remove_if(list.begin(), list.end(),
        [](Ref<CCLabelBMFont> const& badge) {
            return !badge || !badge->getParent();
        }), list.end());

    for (auto& badge : list) {
        badge->setVisible(visible);
    }
}

} // namespace

ccColor3B idBadgeColor() {
    return moduleSetting<ccColor3B>("info-ids-color", ccColor3B{255, 255, 255});
}

GLubyte idBadgeOpacity() {
    auto value = static_cast<int>(moduleSetting<int64_t>("info-ids-opacity", 190));
    return static_cast<GLubyte>(std::clamp(value, 0, 255));
}

bool idBadgesVisible() {
    if (!revealMode()) return true;
    return g_shiftHeld;
}

void setShiftHeld(bool held) {
    if (g_shiftHeld == held) return;
    g_shiftHeld = held;
    if (revealMode()) applyVisibility();
}

CCLabelBMFont* makeIdBadge(std::string const& text, float scale) {
    if (text.empty()) return nullptr;

    auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    if (!label) return nullptr;

    label->setScale(scale);
    label->setColor(idBadgeColor());
    label->setOpacity(idBadgeOpacity());
    label->setVisible(idBadgesVisible());

    badges().push_back(label);
    return label;
}

void applyAdaptiveIdBadgeContrast(CCLabelBMFont* label) {
    // Difference blending needs premultiplied glyph RGB to preserve transparent pixels.
    if (!label || !label->isOpacityModifyRGB()) return;

    label->setColor({255, 255, 255});
    label->setOpacity(255);
    label->setBlendFunc({GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR});
}

} // namespace paimon::info

// Use Geode's portable input event instead of modifying CCKeyboardDispatcher.
// The generated dispatcher modify header has no constructor/destructor address
// on iOS, so merely including it makes the arm64 build fail.
$execute {
    KeyboardInputEvent().listen(+[](KeyboardInputData& data) {
        switch (data.key) {
            case KEY_Shift:
            case KEY_LeftShift:
            case KEY_RightShift:
                paimon::info::setShiftHeld(
                    data.action != KeyboardInputData::Action::Release
                );
                break;
            default:
                break;
        }
        return false;
    }).leak();
}
