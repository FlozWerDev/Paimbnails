#pragma once

// Stats chart rendered with separate grid and bar nodes. Colors are premultiplied
// for CCDrawNode's (GL_ONE, GL_ONE_MINUS_SRC_ALPHA) blending.

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace paimon::info {

struct ChartOptions {
    cocos2d::ccColor3B color{120, 190, 255};
    bool heat = false;       // Color bars by height.
    bool stretch = true;     // Use the full width instead of right-pinning bars.
    bool average = false;    // Draw the mean line.
    bool showScale = false;  // Show the maximum above the plot.
    int highlight = -1;      // Bar index, or -1 for the tallest.
    std::vector<float> tints;
    float marker = -1.f;                    // Vertical reference, as width fraction.
    std::vector<std::string> axisLabels;    // Labels along the bottom.
    std::string axisNote;                   // Right-aligned bottom hint.
    std::string emptyText;                  // Text shown when all values are zero.
};

class StatsChartNode : public cocos2d::CCNode {
public:
    // Zero-valued data still draws the grid and emptyText.
    static StatsChartNode* create(std::vector<float> const& values,
                                  cocos2d::CCSize const& size, ChartOptions const& options);

protected:
    // Shared column geometry for the grid and bar passes.
    struct Geometry {
        int count = 0;
        float slot = 0.f;    // Bar plus gap.
        float barW = 0.f;
        float startX = 0.f;
    };

    bool init(std::vector<float> const& values, cocos2d::CCSize const& size,
              ChartOptions const& options);

    void drawGrid(cocos2d::CCDrawNode* draw, cocos2d::CCRect const& plot,
                  Geometry const& geo, bool ghost);
    // The bar node is parked on the baseline; heights start at y = 0.
    void drawBars(cocos2d::CCDrawNode* draw, std::vector<float> const& values,
                  ChartOptions const& options, cocos2d::CCRect const& plot,
                  Geometry const& geo, float peak);
    void drawAxis(cocos2d::CCDrawNode* draw, ChartOptions const& options,
                  cocos2d::CCRect const& plot, float laneY);
};

}
