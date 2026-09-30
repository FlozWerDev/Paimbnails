#pragma once

#include <Geode/Geode.hpp>

namespace paimon::ui {

// detach ime/delegate first; late key events av in textinput::textchanged otherwise.
inline void detachGeodeTextInput(geode::TextInput* input) {
    if (!input) return;

    input->setCallbackEnabled(false);
    input->defocus();

    if (auto* node = input->getInputNode()) {
        if (node->m_textField) {
            node->m_textField->detachWithIME();
        }
        node->m_selected = false;
        node->onClickTrackNode(false);
        node->m_delegate = nullptr;
    }

    input->setDelegate(nullptr);
}

template <class Owner>
inline geode::Function<void(std::string const&)> safeTextInputCallback(
    geode::WeakRef<Owner> owner,
    void (Owner::*method)(std::string const&)
) {
    return [owner, method](std::string const& text) {
        auto ref = owner.lock();
        if (!ref) return;
        (static_cast<Owner*>(ref.data())->*method)(text);
    };
}

} // namespace paimon::ui