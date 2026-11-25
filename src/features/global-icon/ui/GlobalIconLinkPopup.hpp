#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../GlobalIconTypes.hpp"
#include <functional>

namespace paimon::globalicon {

// asked when the global icon button is pressed: which parts of the icon other
// players can see (vanilla set, icon gradients, custom more icons textures).
class GlobalIconLinkPopup : public PaimonPopup {
public:
    // link=false means "stop sharing".
    using Callback = std::function<void(bool link, LinkOptions options)>;
    static GlobalIconLinkPopup* create(LinkOptions current, bool enabled, Callback callback);

protected:
    bool init(LinkOptions current, bool enabled, Callback callback);

private:
    LinkOptions m_options;
    Callback m_callback;
    void finish(bool link);
};

} // namespace paimon::globalicon
