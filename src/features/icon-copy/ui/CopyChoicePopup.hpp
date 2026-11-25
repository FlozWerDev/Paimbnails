#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include "../../global-icon/GlobalIconTypes.hpp"
#include <functional>

namespace paimon::iconcopy {

// asked before copying another player's icons when their global icon also
// shares gradients or custom textures: which parts to take.
class CopyChoicePopup : public PaimonPopup {
public:
    using Callback = std::function<void(paimon::globalicon::LinkOptions options)>;
    static CopyChoicePopup* create(paimon::globalicon::LinkOptions available, char const* action, Callback callback);

protected:
    bool init(paimon::globalicon::LinkOptions available, char const* action, Callback callback);

private:
    paimon::globalicon::LinkOptions m_options;
    Callback m_callback;
};

} // namespace paimon::iconcopy
