#include "../../core/Settings.hpp"
#include "../smooth-scroll/services/SmoothScrollController.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCTextInputNode.hpp>
#include <Geode/binding/TextInputDelegate.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/cocos/robtop/keyboard_dispatcher/CCKeyboardDispatcher.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

// Numeric text-input scroll wheel: hovering (or focusing) a numeric input and
// rolling the wheel bumps its value by 1 per tick. Holding the configured
// modifier (Shift by default; "Fn" isn't reachable from software on Windows)
// on a decimal input steps by a small fractional amount instead. The
// CCMouseDispatcher::dispatchScrollMSG binding is inline on macOS/iOS so this
// is Windows-only, like volume-scroll.

#if defined(GEODE_IS_WINDOWS)

#include <Geode/modify/CCMouseDispatcher.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

// GD/cocos convention on Windows: CCEGLView negates GLFW's yoffset, so
// dispatchScrollMSG receives y>0 when the physical wheel goes DOWN and y<0
// when it goes UP.
int scrollDirection(float y) {
    if (y < 0.f) return +1;
    if (y > 0.f) return -1;
    return 0;
}

struct NumericProfile {
    bool numeric = false;
    bool signedInt = false;
    bool floating = false;
};

NumericProfile classifyInput(CCTextInputNode* input) {
    NumericProfile p;
    if (!input) return p;
    std::string allowed(input->m_allowedChars.c_str());
    if (allowed.empty()) return p;

    bool hasDigit = false;
    for (char c : allowed) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            hasDigit = true;
        } else if (c == '-') {
            p.signedInt = true;
        } else if (c == '.' || c == ',') {
            p.floating = true;
        } else if (c == '+' || c == ' ') {
            // tolerated
        } else {
            return {};
        }
    }
    if (!hasDigit) return {};
    p.numeric = true;
    return p;
}

TextInput* findGeodeWrapper(CCTextInputNode* input) {
    for (CCNode* p = input ? input->getParent() : nullptr; p; p = p->getParent()) {
        if (auto* w = typeinfo_cast<TextInput*>(p)) return w;
    }
    return nullptr;
}

CCRect worldBox(CCNode* n) {
    auto bl = n->convertToWorldSpace({0.f, 0.f});
    auto tr = n->convertToWorldSpace(n->getContentSize());
    return {std::min(bl.x, tr.x), std::min(bl.y, tr.y),
            std::abs(tr.x - bl.x), std::abs(tr.y - bl.y)};
}

bool isNodeVisibleInTree(CCNode* n) {
    for (auto* c = n; c; c = c->getParent()) {
        if (!c->isVisible()) return false;
    }
    return true;
}

void collectCandidates(CCNode* node, CCPoint const& mouse,
                       CCTextInputNode*& topHit,
                       CCTextInputNode*& focused) {
    if (!node || !node->isVisible()) return;

    if (auto* input = typeinfo_cast<CCTextInputNode*>(node)) {
        if (isNodeVisibleInTree(input)) {
            if (input->m_selected) focused = input;
            CCRect box = worldBox(input);
            if (!box.containsPoint(mouse)) {
                if (auto* wrap = findGeodeWrapper(input)) {
                    box = worldBox(wrap);
                }
            }
            if (box.containsPoint(mouse)) {
                topHit = input;
            }
        }
    }

    if (auto* children = node->getChildren()) {
        for (unsigned i = 0; i < children->count(); ++i) {
            collectCandidates(static_cast<CCNode*>(children->objectAtIndex(i)),
                              mouse, topHit, focused);
        }
    }
}

CCTextInputNode* pickTarget() {
    auto* dir = CCDirector::get();
    if (!dir) return nullptr;
    auto* scene = dir->getRunningScene();
    if (!scene) return nullptr;

    auto mouse = geode::cocos::getMousePos();
    CCTextInputNode* hit = nullptr;
    CCTextInputNode* focused = nullptr;
    collectCandidates(scene, mouse, hit, focused);

    if (paimon::settings::input_scroll::focusOnly()) {
        return (focused && hit == focused) ? focused : nullptr;
    }
    return hit ? hit : focused;
}

bool decimalModifierHeld() {
    auto* kb = CCKeyboardDispatcher::get();
    if (!kb) return false;
    std::string mod = paimon::settings::input_scroll::decimalModifier();
    if (mod == "ctrl")  return kb->getControlKeyPressed();
    if (mod == "alt")   return kb->getAltKeyPressed();
    return kb->getShiftKeyPressed(); // default
}

// Format a double with a fixed number of decimal places, trimming any final
// negative zero that snprintf might emit for tiny magnitudes.
std::string formatFloat(double value, int places) {
    if (std::abs(value) < std::pow(10.0, -(places + 3))) value = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", places, value);
    std::string s = buf;
    if (s.size() >= 2 && s[0] == '-' && s[1] == '0') {
        // Strip "-0.000" style negatives.
        bool onlyZeros = true;
        for (size_t i = 1; i < s.size(); ++i) {
            char c = s[i];
            if (c != '0' && c != '.') { onlyZeros = false; break; }
        }
        if (onlyZeros) s = s.substr(1);
    }
    return s;
}

bool parseCurrentInt(CCTextInputNode* input, long long& out) {
    std::string s(input->getString().c_str());
    if (s.empty()) { out = 0; return true; }

    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i == s.size()) { out = 0; return true; }

    bool neg = false;
    if (s[i] == '-') { neg = true; ++i; }
    else if (s[i] == '+') { ++i; }

    long long v = 0;
    bool any = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        int d = s[i] - '0';
        if (v > (std::numeric_limits<long long>::max() - d) / 10) {
            v = std::numeric_limits<long long>::max();
            any = true;
            break;
        }
        v = v * 10 + d;
        any = true;
        ++i;
    }
    if (!any) return false;
    out = neg ? -v : v;
    return true;
}

bool parseCurrentFloat(CCTextInputNode* input, double& out) {
    std::string s(input->getString().c_str());
    if (s.empty()) { out = 0.0; return true; }
    // Accept "," as decimal separator too.
    for (auto& c : s) if (c == ',') c = '.';
    try {
        size_t idx = 0;
        double v = std::stod(s, &idx);
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

void commitString(CCTextInputNode* input, std::string const& newStr) {
    if (auto* wrap = findGeodeWrapper(input)) {
        wrap->setString(newStr, true);
        return;
    }
    input->setString(newStr);
    input->refreshLabel();
    if (input->m_delegate) {
        input->m_delegate->textChanged(input);
    }
}

bool respectsMaxLen(CCTextInputNode* input, std::string const& newStr,
                    std::string& outFinal) {
    int maxLen = input->m_maxLabelLength;
    if (maxLen <= 0 || static_cast<int>(newStr.size()) <= maxLen) {
        outFinal = newStr;
        return true;
    }
    if (!paimon::settings::input_scroll::wrap()) return false;

    // Wrap only makes sense for pure integers; if the string has a dot, give up.
    if (newStr.find('.') != std::string::npos) return false;

    bool neg = !newStr.empty() && newStr[0] == '-';
    std::string digits = neg ? newStr.substr(1) : newStr;
    int allowedDigits = neg ? std::max(1, maxLen - 1) : maxLen;
    if (static_cast<int>(digits.size()) <= allowedDigits) {
        outFinal = newStr;
        return true;
    }
    digits = digits.substr(digits.size() - static_cast<size_t>(allowedDigits));
    outFinal = neg ? ("-" + digits) : digits;
    return static_cast<int>(outFinal.size()) <= maxLen;
}

void writeInt(CCTextInputNode* input, long long value, NumericProfile const& profile) {
    if (!profile.signedInt && value < 0) value = 0;
    std::string s = std::to_string(value);
    std::string finalStr;
    if (!respectsMaxLen(input, s, finalStr)) return;
    commitString(input, finalStr);
}

void writeFloat(CCTextInputNode* input, double value, NumericProfile const& profile) {
    if (!profile.signedInt && value < 0.0) value = 0.0;
    int places = std::clamp(paimon::settings::input_scroll::decimalPlaces(), 1, 6);
    std::string s = formatFloat(value, places);
    std::string finalStr;
    if (!respectsMaxLen(input, s, finalStr)) return;
    commitString(input, finalStr);
}

bool tryHandleInputScroll(float y) {
    if (!paimon::settings::input_scroll::enabled()) return false;
    if (y == 0.f) return false;

    // Never fire on smooth-scroll's own replay ticks.
    if (paimon::smoothscroll::SmoothScrollController::get().isReplaying()) {
        return false;
    }

    int dir = scrollDirection(y);
    if (dir == 0) return false;

    auto* target = pickTarget();
    if (!target) return false;

    auto profile = classifyInput(target);
    if (!profile.numeric) return false;

    bool decimalMode = profile.floating && decimalModifierHeld();

    if (decimalMode) {
        double base = 0.0;
        if (!parseCurrentFloat(target, base)) return false;
        double step = paimon::settings::input_scroll::decimalStep();
        if (step <= 0.0) step = 0.1;
        writeFloat(target, base + dir * step, profile);
    } else {
        long long base = 0;
        if (!parseCurrentInt(target, base)) return false;
        int step = std::max(1, paimon::settings::input_scroll::intStep());
        writeInt(target, base + static_cast<long long>(dir) * step, profile);
    }
    return true;
}

} // namespace

class $modify(PaimonInputScrollDispatcher, CCMouseDispatcher) {
    static void onModify(auto& self) {
        // VeryEarly so we run before smooth-scroll (Early) and volume-scroll
        // (Early). Consuming here prevents smoothing from replaying the tick
        // and prevents the volume hook from firing on scrolls we own.
        (void)self.setHookPriorityPre("cocos2d::CCMouseDispatcher::dispatchScrollMSG",
                                       geode::Priority::VeryEarly);
    }

    bool dispatchScrollMSG(float y, float x) {
        if (tryHandleInputScroll(y)) {
            return true;
        }
        return CCMouseDispatcher::dispatchScrollMSG(y, x);
    }
};

#endif // GEODE_IS_WINDOWS
