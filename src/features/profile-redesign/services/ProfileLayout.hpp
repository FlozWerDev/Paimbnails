#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace paimon::profile_redesign {

struct ProfileRect {
    float x, y, width, height;
    float centerX() const { return x + width * .5f; }
    float centerY() const { return y + height * .5f; }
    float top() const { return y + height; }
};

struct ProfileLayout {
    ProfileRect header, icons, activity, posts, footer, accountRail, socialRail;
};

inline ProfileLayout profileLayout(float centerX, float centerY, float width, float height) {
    if (!std::isfinite(width) || width < 360.f) width = 440.f;
    if (!std::isfinite(height) || height < 280.f) height = 290.f;
    float const left = centerX - width * .5f;
    float const bottom = centerY - height * .5f;
    float const top = bottom + height;
    float const contentLeft = left + 52.f;
    float const contentWidth = width - 104.f;

    ProfileLayout layout;
    layout.header = {contentLeft, top - 64.f, contentWidth, 56.f};
    layout.icons = {contentLeft, layout.header.y - 40.f, contentWidth, 34.f};
    layout.footer = {contentLeft, bottom + 8.f, contentWidth, 30.f};
    float const activityBottom = layout.footer.top() + 6.f;
    layout.activity = {contentLeft, activityBottom, contentWidth, layout.icons.y - 6.f - activityBottom};
    layout.posts = {contentLeft + 4.f, activityBottom + 4.f, contentWidth - 8.f,
        layout.activity.height - 30.f};
    layout.accountRail = {left + 8.f, bottom + 54.f, 34.f, height - 104.f};
    layout.socialRail = {left + width - 42.f, bottom + 54.f, 34.f, height - 104.f};
    return layout;
}

inline std::array<ProfileRect, 4> profileActivityTabs(float width, float height) {
    constexpr float sideSpace = 8.f;
    constexpr float gap = 6.f;
    float const buttonWidth = std::min(100.f, (width - 2.f * sideSpace - 3.f * gap) / 4.f);
    float const left = (width - (4.f * buttonWidth + 3.f * gap)) * .5f;
    std::array<ProfileRect, 4> tabs;
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        tabs[index] = {left + static_cast<float>(index) * (buttonWidth + gap), height - 24.f, buttonWidth, 22.f};
    }
    return tabs;
}

} // namespace paimon::profile_redesign
