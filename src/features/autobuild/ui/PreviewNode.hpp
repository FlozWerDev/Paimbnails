#pragma once

// A small map of what a region or a template contains, drawn as coloured cells
// so structure, hazards and backdrop are told apart at a glance.

#include <Geode/Geode.hpp>

#include <vector>

#include "../AutobuildTypes.hpp"
#include "../services/LevelAnalysis.hpp"

namespace paimon::autobuild {

class PreviewNode : public cocos2d::CCNode {
public:
    static PreviewNode* create(cocos2d::CCSize size);

    void showRegion(LevelData const& data, Region const& region);
    void showTemplate(Template const& tpl);
    void showPiece(Piece const& piece);

private:
    struct Dot {
        float x = 0.f;
        float y = 0.f;
        float size = 1.f;
        ObjectKind kind = ObjectKind::Unknown;
        bool behind = false;
    };

    bool init(cocos2d::CCSize size);
    void draw(std::vector<Dot> const& dots);

    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCLabelBMFont* m_empty = nullptr;
};

} // namespace paimon::autobuild
