#pragma once

#include "../services/MainMenuLayoutManager.hpp"

#include "../../../utils/PaimonDrawNode.hpp"

namespace paimon::menu_layout {

class MainMenuDrawShapeNode : public PaimonDrawNode {
public:
    static MainMenuDrawShapeNode* create(DrawShapeLayout const& layout);

    void applyLayout(DrawShapeLayout const& layout);
    DrawShapeLayout readLayout();

protected:
    DrawShapeLayout m_layout;
};

} // namespace paimon::menu_layout
