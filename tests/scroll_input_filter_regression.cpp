#include "../src/features/smooth-scroll/services/ScrollInputFilter.hpp"

#include <cassert>
#include <cmath>

using namespace paimon::smoothscroll;

namespace {

double runTravel(double notches, double dt, int maxFrames) {
    ScrollInputFilter filter;
    filter.queue(notches, 0.0);
    double delivered = 0.0;
    for (int i = 0; i < maxFrames && filter.active(); ++i) {
        delivered += filter.step(dt, 12.0).y;
    }
    return delivered;
}

}

int main() {
    // one notch travels exactly one notch.
    assert(std::abs(runTravel(1.0, 1.0 / 60.0, 600) - 1.0) < 1e-3);
    assert(std::abs(runTravel(-2.5, 1.0 / 60.0, 600) + 2.5) < 1e-3);

    // the delivered travel does not depend on the frame time.
    double const slow = runTravel(1.0, 1.0 / 30.0, 600);
    double const fast = runTravel(1.0, 1.0 / 240.0, 600);
    assert(std::abs(slow - fast) < 1e-3);

    // the travel never overshoots the queued notches.
    {
        ScrollInputFilter filter;
        filter.queue(3.0, 0.0);
        double delivered = 0.0;
        for (int i = 0; i < 600 && filter.active(); ++i) {
            delivered += filter.step(1.0 / 60.0, 12.0).y;
            assert(delivered <= 3.0 + 1e-6);
        }
        assert(std::abs(delivered - 3.0) < 1e-3);
    }

    // the motion ramps up: the first frame only moves a small slice of the
    // travel, so no zone starts with a jump.
    {
        ScrollInputFilter filter;
        filter.queue(1.0, 0.0);
        ScrollVector const frame = filter.step(1.0 / 60.0, 12.0);
        assert(frame.y > 0.0);
        assert(frame.y < 0.1);
    }

    // a reversal reacts at once instead of repaying the old momentum first.
    {
        ScrollInputFilter filter;
        filter.queue(1.0, 0.0);
        filter.step(1.0 / 60.0, 12.0);
        filter.queue(-1.0, 0.0);
        double delivered = 0.0;
        for (int i = 0; i < 600 && filter.active(); ++i) {
            delivered += filter.step(1.0 / 60.0, 12.0).y;
        }
        assert(delivered < 0.0);
    }

    // the filter settles once the travel is delivered.
    {
        ScrollInputFilter filter;
        filter.queue(1.0, 0.0);
        int frames = 0;
        while (filter.active() && frames < 600) {
            filter.step(1.0 / 60.0, 12.0);
            ++frames;
        }
        assert(!filter.active());
        assert(frames < 600);
    }

    // the axes are independent.
    {
        ScrollInputFilter filter;
        filter.queue(0.0, 2.0);
        ScrollVector const frame = filter.step(1.0 / 60.0, 12.0);
        assert(frame.y == 0.0);
        assert(frame.x > 0.0);
    }

    // garbage input is dropped instead of poisoning the momentum.
    {
        ScrollInputFilter filter;
        filter.queue(std::nan(""), 0.0);
        assert(!filter.active());
        filter.reset();
        assert(!filter.active());
    }
}
