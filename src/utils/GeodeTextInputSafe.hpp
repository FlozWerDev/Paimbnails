#pragma once

#include <Geode/Geode.hpp>

namespace paimon::ui {

// detach ime/callbacks first; late key events can fire after exit otherwise.
inline void detachGeodeTextInput(geode::TextInput* input) {
    if (!input) return;

    input->setCallback(nullptr);
    input->setEnterCallback(nullptr);
    input->setFocusCallback(nullptr);
    input->setEnabled(false);
    input->unfocus();
    input->detachWithIME();
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