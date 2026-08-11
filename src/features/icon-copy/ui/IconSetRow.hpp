#pragma once
// The card both icon lists are built from: artwork on the right, name and
// subtitle on the left, and whatever buttons the list hands over in between.

#include <Geode/Geode.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace paimon::iconcopy {

struct IconSet;

// A button in the row. A null face is dropped, so a missing sprite frame costs
// the row nothing but its button.
struct RowAction {
    cocos2d::CCNode* face = nullptr;
    std::function<void()> run;
};

constexpr float kRowHeight = 54.f;

cocos2d::CCNode* makeRowTextFace(char const* text, char const* buttonSprite);
cocos2d::CCNode* makeRowIconFace(char const* frameName);

// `index` only picks the card shade, so a list reads as banded rows.
cocos2d::CCNode* makeSetRow(IconSet const& set, std::string const& subtitle,
                            std::size_t index, float width,
                            std::vector<RowAction> const& actions);

// "2026-07-26 22:03", or "unknown date" for sets stored before we kept one.
std::string formatSetDate(std::int64_t epoch);

}  // namespace paimon::iconcopy
