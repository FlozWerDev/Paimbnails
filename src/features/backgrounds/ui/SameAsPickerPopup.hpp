#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/function.hpp>

class SameAsPickerPopup : public geode::Popup {
protected:
    std::string m_selectedLayerKey;
    geode::CopyableFunction<void(std::string const&)> m_onPick;

    bool init(std::string const& currentKey, geode::CopyableFunction<void(std::string const&)> onPick);

public:
    static SameAsPickerPopup* create(std::string const& currentKey, geode::CopyableFunction<void(std::string const&)> onPick);
};
