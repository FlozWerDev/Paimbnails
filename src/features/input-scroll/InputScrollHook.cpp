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

// desktop wheel steps numeric fields; shares the ccmousedispatcher hook point
// with smooth-scroll.

#if defined(GEODE_IS_DESKTOP)

#include <Geode/modify/CCMouseDispatcher.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

// cceglview negates glfw yoffset: positive means physical wheel down.
int scrollDirection(float y) {
    if (y < 0.f) return +1;
    if (y > 0.f) return -1;
    return 0;
}

struct InputTarget {
    TextInput* geode = nullptr;
    CCTextInputNode* cocos = nullptr;
    explicit operator bool() const { return geode || cocos; }
    bool operator==(InputTarget const& o) const {
        return geode == o.geode && cocos == o.cocos;
    }
};

struct NumericProfile {
    bool numeric = false;
    bool signedInt = false;
    bool floating = false;
};

NumericProfile classifyInput(InputTarget const& target) {
    NumericProfile p;
    if (!target) return p;
    std::string allowed = target.geode
        ? target.geode->getFilter()
        : std::string(target.cocos->m_allowedChars.c_str());
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
        } else {
            return {};
        }
    }
    if (!hasDigit) return {};
    p.numeric = true;
    return p;
}

TextInput* findGeodeWrapper(CCNode* node) {
    for (CCNode* p = node ? node->getParent() : nullptr; p; p = p->getParent()) {
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

void collectCandidates(CCNode* node, CCPoint const& mouse,
                       InputTarget& topHit,
                       InputTarget& focused) {
    if (!node || !node->isVisible()) return;

    if (auto* geodeInput = typeinfo_cast<TextInput*>(node)) {
        if (geodeInput->isEnabled()) {
            if (geodeInput->isFocused()) focused = {geodeInput, nullptr};
            if (worldBox(geodeInput).containsPoint(mouse)) {
                topHit = {geodeInput, nullptr};
            }
        }
        return;
    }

    if (auto* input = typeinfo_cast<CCTextInputNode*>(node)) {
        if (!findGeodeWrapper(input)) {
            if (input->m_selected) focused = {nullptr, input};
            if (worldBox(input).containsPoint(mouse)) {
                topHit = {nullptr, input};
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

InputTarget pickTarget() {
    auto* dir = CCDirector::get();
    if (!dir) return {};
    auto* scene = dir->getRunningScene();
    if (!scene) return {};

    auto mouse = geode::cocos::getMousePos();
    InputTarget hit{};
    InputTarget focused{};
    collectCandidates(scene, mouse, hit, focused);

    if (paimon::settings::input_scroll::focusOnly()) {
        return (focused && hit == focused) ? focused : InputTarget{};
    }
    return hit ? hit : focused;
}

bool decimalModifierHeld() {
    auto* kb = CCKeyboardDispatcher::get();
    if (!kb) return false;
    std::string mod = paimon::settings::input_scroll::decimalModifier();
    if (mod == "ctrl")  return kb->getControlKeyPressed();
    if (mod == "alt")   return kb->getAltKeyPressed();
    return kb->getShiftKeyPressed();
}

std::string formatFloat(double value, int places) {
    if (std::abs(value) < std::pow(10.0, -(places + 3))) value = 0.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", places, value);
    std::string s = buf;
    if (s.size() >= 2 && s[0] == '-' && s[1] == '0') {
        bool onlyZeros = true;
        for (size_t i = 1; i < s.size(); ++i) {
            char c = s[i];
            if (c != '0' && c != '.') { onlyZeros = false; break; }
        }
        if (onlyZeros) s = s.substr(1);
    }
    return s;
}

std::string readTargetString(InputTarget const& target) {
    if (target.geode) return target.geode->getString();
    if (target.cocos) return std::string(target.cocos->getString().c_str());
    return {};
}

bool parseCurrentInt(InputTarget const& target, long long& out) {
    std::string s = readTargetString(target);
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

bool parseCurrentFloat(InputTarget const& target, double& out) {
    std::string s = readTargetString(target);
    if (s.empty()) { out = 0.0; return true; }
    for (auto& c : s) if (c == ',') c = '.';
    auto parsed = geode::utils::numFromString<double>(s);
    if (!parsed.isOk()) return false;
    out = parsed.unwrap();
    return true;
}

void commitString(InputTarget const& target, std::string const& newStr) {
    if (target.geode) {
        target.geode->setString(newStr, true);
        return;
    }
    if (!target.cocos) return;
    target.cocos->setString(newStr);
    target.cocos->refreshLabel();
    if (target.cocos->m_delegate) {
        target.cocos->m_delegate->textChanged(target.cocos);
    }
}

bool respectsMaxLen(InputTarget const& target, std::string const& newStr,
                    std::string& outFinal) {
    int maxLen = target.geode
        ? static_cast<int>(target.geode->getMaxCharCount())
        : target.cocos->m_maxLabelLength;
    if (maxLen <= 0 || static_cast<int>(newStr.size()) <= maxLen) {
        outFinal = newStr;
        return true;
    }
    if (!paimon::settings::input_scroll::wrap()) return false;
    // wrap only pure integers; decimals stay unchanged.
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

void writeInt(InputTarget const& target, long long value, NumericProfile const& profile) {
    if (!profile.signedInt && value < 0) value = 0;
    std::string s = std::to_string(value);
    std::string finalStr;
    if (!respectsMaxLen(target, s, finalStr)) return;
    commitString(target, finalStr);
}

void writeFloat(InputTarget const& target, double value, NumericProfile const& profile) {
    if (!profile.signedInt && value < 0.0) value = 0.0;
    int places = std::clamp(paimon::settings::input_scroll::decimalPlaces(), 1, 6);
    std::string s = formatFloat(value, places);
    std::string finalStr;
    if (!respectsMaxLen(target, s, finalStr)) return;
    commitString(target, finalStr);
}

bool tryHandleInputScroll(float y) {
    if (!paimon::settings::input_scroll::enabled()) return false;
    if (y == 0.f) return false;
    // ignore smooth-scroll replay ticks.
    if (paimon::smoothscroll::SmoothScrollController::get().isReplaying()) {
        return false;
    }

    int dir = scrollDirection(y);
    if (dir == 0) return false;

    auto target = pickTarget();
    if (!target) return false;

    auto profile = classifyInput(target);
    if (!profile.numeric) return false;

    if (profile.floating) {
        double base = 0.0;
        if (!parseCurrentFloat(target, base)) return false;
        double step = static_cast<double>(std::max(1, paimon::settings::input_scroll::intStep()));
        if (decimalModifierHeld()) {
            step = paimon::settings::input_scroll::decimalStep();
            if (step <= 0.0) step = 0.1;
        }
        writeFloat(target, base + dir * step, profile);
    } else {
        long long base = 0;
        if (!parseCurrentInt(target, base)) return false;
        // parsecurrentint saturates, so the step must too: adding past the limits is signed overflow.
        long long bump = static_cast<long long>(dir) * std::max(1, paimon::settings::input_scroll::intStep());
        long long next = base;
        if (bump > 0 && base > std::numeric_limits<long long>::max() - bump) {
            next = std::numeric_limits<long long>::max();
        } else if (bump < 0 && base < std::numeric_limits<long long>::min() - bump) {
            next = std::numeric_limits<long long>::min();
        } else {
            next = base + bump;
        }
        writeInt(target, next, profile);
    }
    return true;
}

}

class $modify(PaimonInputScrollDispatcher, CCMouseDispatcher) {
    static void onModify(auto& self) {
        // before volume/smooth scroll; owned ticks are consumed.
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

#endif // geode_is_desktop
