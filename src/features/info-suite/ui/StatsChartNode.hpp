#pragma once

// stats chart rendered with separate grid and bar nodes. colors are premultiplied
// for ccdrawnode's (gl_one, gl_one_minus_src_alpha) blending.

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::info {

struct ChartOptions {
    cocos2d::ccColor3B color{120, 190, 255};
    bool heat = false;       // color bars by height.
    bool stretch = true;     // use the full width instead of right-pinning bars.
    bool average = false;    // draw the mean line.
    bool showScale = false;  // show the maximum above the plot.
    int highlight = -1;      // bar index, or -1 for the tallest.
    std::vector<float> tints;
    float marker = -1.f;                    // vertical reference, as width fraction.
    std::vector<std::string> axisLabels;    // labels along the bottom.
    std::string axisNote;                   // right-aligned bottom hint.
    std::string emptyText;                  // text shown when all values are zero.
};

class StatsChartNode : public cocos2d::CCNode {
public:
    // zero-valued data still draws the grid and emptytext.
    static StatsChartNode* create(std::vector<float> const& values,
                                  cocos2d::CCSize const& size, ChartOptions const& options);

protected:
    // shared column geometry for the grid and bar passes.
    struct Geometry {
        int count = 0;
        float slot = 0.f;    // bar plus gap.
        float barW = 0.f;
        float startX = 0.f;
    };

    bool init(std::vector<float> const& values, cocos2d::CCSize const& size,
              ChartOptions const& options);

    void drawGrid(cocos2d::CCDrawNode* draw, cocos2d::CCRect const& plot,
                  Geometry const& geo, bool ghost);
    // the bar node is parked on the baseline; heights start at y = 0.
    void drawBars(cocos2d::CCDrawNode* draw, std::vector<float> const& values,
                  ChartOptions const& options, cocos2d::CCRect const& plot,
                  Geometry const& geo, float peak);
    void drawAxis(cocos2d::CCDrawNode* draw, ChartOptions const& options,
                  cocos2d::CCRect const& plot, float laneY);
};

}
