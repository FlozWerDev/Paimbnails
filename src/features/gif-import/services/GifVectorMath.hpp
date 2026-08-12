#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace paimon::gifimport {

constexpr float kPi = 3.14159265358979323846f;

struct Point {
    float x = 0.f;
    float y = 0.f;
};

float pointDistance(Point const& first, Point const& second);
float lineDistance(Point const& point, Point const& first, Point const& last);
std::vector<Point> simplify(std::vector<Point> const& points, float tolerance);

std::array<int, 4> bounds(std::vector<int> const& positions, int width);

struct Skeleton {
    int width = 0;
    int height = 0;
    int offsetX = 0;
    int offsetY = 0;
    std::vector<std::uint8_t> cells;
};

Skeleton thin(std::vector<int> const& positions, int sourceWidth);
std::vector<std::vector<int>> skeletonPaths(Skeleton const& skeleton);

std::vector<std::vector<int>> connectedComponents(
    std::vector<int> const& positions,
    int width,
    int height
);

} // namespace paimon::gifimport
