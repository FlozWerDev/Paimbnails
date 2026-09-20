#pragma once

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace paimon::texture_studio {

class NewProjectPopup : public geode::Popup {
public:
    using SlotCreatedCallback = std::function<void(std::string const& slotId)>;

    static NewProjectPopup* create(SlotCreatedCallback cb);

protected:
    bool init(SlotCreatedCallback cb);

    void onCreateClicked(cocos2d::CCObject*);

private:
    SlotCreatedCallback m_onCreated;

    geode::TextInput* m_nameInput = nullptr;
};

}  // namespace paimon::texture_studio
