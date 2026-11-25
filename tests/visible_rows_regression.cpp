#include "../src/utils/VisibleRows.hpp"
#include <cassert>
#include <limits>

int main() {
    using paimon::ui::visibleRows;
    using paimon::ui::VisibleRowRange;
    // Ten thousand rows create only the viewport and its one-row margins.
    assert((visibleRows(10000, 50, 500000, 200, -499800) == VisibleRowRange{0, 5}));
    assert((visibleRows(10000, 50, 500000, 200, -499750) == VisibleRowRange{0, 6}));
    assert((visibleRows(10000, 50, 500000, 200, 0) == VisibleRowRange{9995, 10000}));
    assert((visibleRows(10000, 50, 500000, 200, 100) == VisibleRowRange{9995, 10000}));
    assert((visibleRows(10000, 50, 500000, 200, -500100) == VisibleRowRange{0, 5}));
    assert((visibleRows(1, 50, 200, 200, 0) == VisibleRowRange{0, 1}));
    assert((visibleRows(0, 50, 200, 200, 0) == VisibleRowRange{}));
    assert((visibleRows(1, 0, 200, 200, 0) == VisibleRowRange{}));
    assert((visibleRows(1, 50, 200, 0, 0) == VisibleRowRange{}));
    assert((visibleRows(1, 50, 200, 200, std::numeric_limits<float>::quiet_NaN()) == VisibleRowRange{}));
    for (int y = -499800; y <= 0; y += 137) {
        auto const range = visibleRows(10000, 50, 500000, 200, static_cast<float>(y));
        assert(range.begin <= range.end && range.end <= 10000);
        assert(range.end - range.begin <= 7);
    }
}
