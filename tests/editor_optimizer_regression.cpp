#include "../src/features/editor-optimizer/StructureOptimizerCore.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace paimon::optimizer;

namespace {

bool near(float a, float b, float eps = 0.01f) {
    return std::abs(a - b) <= eps;
}

ObjectRecord makeBlock(
    std::size_t idx,
    float x,
    float y,
    int objectID = 1,
    StretchCapability cap = StretchCapability::Axis1D,
    bool isSolid = true,
    float rot = 0.f,
    std::string key = "default"
) {
    ObjectRecord rec;
    rec.index = idx;
    rec.objectID = objectID;
    rec.isSolid = isSolid;
    rec.isEligible = true;
    rec.stretch = cap;
    rec.x = x;
    rec.y = y;
    rec.baseWidth = 30.f;
    rec.baseHeight = 30.f;
    rec.scaleX = 1.f;
    rec.scaleY = 1.f;
    rec.rotation = rot;
    rec.compatibilityKey = std::move(key) + "|" + std::to_string(objectID) + "|" + std::to_string(static_cast<int>(rot));
    return rec;
}

void testFourInARow() {
    std::vector<ObjectRecord> objs;
    for (int i = 0; i < 4; ++i) {
        objs.push_back(makeBlock(i, 15.f + i * 30.f, 15.f));
    }
    auto plan = planStructureOptimization(objs);
    assert(plan.inputCount == 4);
    assert(plan.outputCount == 1);
    assert(plan.merges.size() == 1);
    assert(near(plan.merges[0].newX, 60.f));
    assert(near(plan.merges[0].newY, 15.f));
    assert(near(plan.merges[0].newScaleX, 4.f));
    assert(near(plan.merges[0].newScaleY, 1.f));
}

void testCollisionAndDecoMaxLengthCaps() {
    std::vector<ObjectRecord> solidLine;
    for (int i = 0; i < 10; ++i) {
        solidLine.push_back(makeBlock(i, 15.f + i * 30.f, 15.f, 1, StretchCapability::Axis1D, true));
    }
    OptimizerOptions opts;
    opts.maxCollisionScale = 4.f;
    opts.maxDecoScale = 8.f;

    auto solidPlan = planStructureOptimization(solidLine, opts);
    assert(solidPlan.outputCount == 3); // 4 + 4 + 2
    for (auto const& m : solidPlan.merges) {
        assert(m.newScaleX <= 4.01f);
        assert(m.newScaleY <= 1.01f);
    }

    std::vector<ObjectRecord> decoLine;
    for (int i = 0; i < 10; ++i) {
        decoLine.push_back(makeBlock(i, 15.f + i * 30.f, 15.f, 211, StretchCapability::Uniform2D, false));
    }
    auto decoPlan = planStructureOptimization(decoLine, opts);
    assert(decoPlan.outputCount == 2); // 8 + 2
    for (auto const& m : decoPlan.merges) {
        assert(m.newScaleX <= 8.01f);
    }
}

void testCornerBlocksPreserved() {
    // Structure: [Corner][Edge][Edge][Edge][Edge][Corner]
    // Plus two identical corner blocks next to each other must NEVER merge!
    std::vector<ObjectRecord> objs;
    objs.push_back(makeBlock(0, 15.f, 15.f, 2, StretchCapability::None, true));
    for (int i = 1; i <= 4; ++i) {
        objs.push_back(makeBlock(i, 15.f + i * 30.f, 15.f, 5, StretchCapability::HorizontalOnly, true));
    }
    objs.push_back(makeBlock(5, 165.f, 15.f, 2, StretchCapability::None, true));
    // Two identical corners adjacent to each other at another spot:
    objs.push_back(makeBlock(6, 15.f, 105.f, 2, StretchCapability::None, true));
    objs.push_back(makeBlock(7, 45.f, 105.f, 2, StretchCapability::None, true));

    auto plan = planStructureOptimization(objs);
    assert(plan.preservedCornerCount == 4);
    assert(plan.merges.size() == 1);
    assert(plan.merges[0].sourceIndices.size() == 4);
    assert(near(plan.merges[0].newX, 90.f));
    assert(near(plan.merges[0].newScaleX, 4.f));
    assert(plan.outputCount == 5); // 4 corners + 1 merged bar
}

void testTopologicalElbowCornerPreserved() {
    // L-shape of Axis1D blocks:
    // (0,3) (1,3) (2,3) (3,3) (4,3)
    // (0,2)
    // (0,1)
    // (0,0)
    std::vector<ObjectRecord> objs;
    std::size_t idx = 0;
    for (int x = 0; x < 5; ++x) {
        objs.push_back(makeBlock(idx++, 15.f + x * 30.f, 105.f, 1, StretchCapability::Axis1D, true));
    }
    for (int y = 0; y < 3; ++y) {
        objs.push_back(makeBlock(idx++, 15.f, 15.f + y * 30.f, 1, StretchCapability::Axis1D, true));
    }

    auto plan = planStructureOptimization(objs);
    assert(plan.preservedCornerCount == 1);
    assert(plan.preservedCorners[0] == 0); // (0,3) stays 1x1!
    assert(plan.outputCount == 3);        // 1 corner + 4x1 top + 1x3 left
}

void test2DFillMerge() {
    // 4x3 solid grid of borderless Uniform2D blocks (e.g. ID 211)
    std::vector<ObjectRecord> objs;
    std::size_t idx = 0;
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 4; ++x) {
            objs.push_back(makeBlock(idx++, 15.f + x * 30.f, 15.f + y * 30.f, 211, StretchCapability::Uniform2D, false));
        }
    }
    OptimizerOptions opts;
    opts.maxDecoScale = 8.f;
    opts.merge2DFills = true;

    auto plan = planStructureOptimization(objs, opts);
    assert(plan.inputCount == 12);
    assert(plan.outputCount == 1);
    assert(plan.merges.size() == 1);
    assert(near(plan.merges[0].newX, 60.f));
    assert(near(plan.merges[0].newY, 45.f));
    assert(near(plan.merges[0].newScaleX, 4.f));
    assert(near(plan.merges[0].newScaleY, 3.f));
}

void testRotatedVerticalLine() {
    // HorizontalOnly block rotated 90 degrees clockwise -> local X points DOWN (world -Y).
    std::vector<ObjectRecord> objs;
    for (int i = 0; i < 4; ++i) {
        objs.push_back(makeBlock(i, 45.f, 15.f + i * 30.f, 5, StretchCapability::HorizontalOnly, false, 90.f));
    }
    auto plan = planStructureOptimization(objs);
    assert(plan.outputCount == 1);
    assert(plan.merges.size() == 1);
    assert(near(plan.merges[0].newX, 45.f));
    assert(near(plan.merges[0].newY, 60.f));
    assert(near(plan.merges[0].newScaleX, 4.f)); // Stretches along local X!
    assert(near(plan.merges[0].newScaleY, 1.f));
}

void testIncompatibleAndOverlappingDoNotMerge() {
    std::vector<ObjectRecord> objs;
    objs.push_back(makeBlock(0, 15.f, 15.f, 1, StretchCapability::Axis1D, true, 0.f, "group1"));
    objs.push_back(makeBlock(1, 45.f, 15.f, 1, StretchCapability::Axis1D, true, 0.f, "group2"));
    // Duplicate at exact same position:
    objs.push_back(makeBlock(2, 15.f, 15.f, 1, StretchCapability::Axis1D, true, 0.f, "group1"));

    auto plan = planStructureOptimization(objs);
    assert(plan.merges.empty());
    assert(plan.outputCount == 3);
}

void testRewriteObjectTransform() {
    std::string const raw = "1,211,2,15,3,15,21,3,32,2,57,10.12,24,-1,25,4";
    float sx = 0.f, sy = 0.f;
    assert(extractSaveXY(raw, sx, sy));
    assert(near(sx, 15.f) && near(sy, 15.f));
    std::string const updated = rewriteObjectTransform(raw, 60.f, 45.5f, 4.f, 3.f);
    assert(updated.find(",32,") == std::string::npos);
    assert(updated.find("1,211,21,3,57,10.12,24,-1,25,4,2,60,3,45.5,128,4,129,3") != std::string::npos);
}

void assertPlanGeometryValid(
    std::vector<ObjectRecord> const& objs,
    OptimizationPlan const& plan
) {
    std::vector<bool> seen(objs.size(), false);
    std::size_t removed = 0;
    for (auto const& g : plan.merges) {
        assert(!g.sourceIndices.empty());
        bool keeperFound = false;
        for (std::size_t idx : g.sourceIndices) {
            assert(idx < objs.size());
            assert(!seen[idx]); // No double-deletes!
            seen[idx] = true;
            ++removed;
            if (idx == g.keeperIndex) keeperFound = true;
        }
        assert(keeperFound);
    }
    assert(removed == plan.mergedObjectCount);
    assert(plan.outputCount == plan.inputCount - plan.mergedObjectCount + plan.merges.size());
}

void testMixedPreStretched2DPinwheel() {
    // 3x3 filled square composed of 5 pre-stretched Uniform2D pieces in a pinwheel:
    // (0..2, 2): 2x1 at (30, 75)
    // (2, 1..2): 1x2 at (75, 60)
    // (0, 0..1): 1x2 at (15, 30)
    // (1..2, 0): 2x1 at (60, 15)
    // (1, 1):    1x1 at (45, 45)
    std::vector<ObjectRecord> objs;
    auto add = [&](float x, float y, float sx, float sy) {
        auto r = makeBlock(objs.size(), x, y, 211, StretchCapability::Uniform2D, false);
        r.scaleX = sx;
        r.scaleY = sy;
        objs.push_back(std::move(r));
    };
    add(30.f, 75.f, 2.f, 1.f);
    add(75.f, 60.f, 1.f, 2.f);
    add(15.f, 30.f, 1.f, 2.f);
    add(60.f, 15.f, 2.f, 1.f);
    add(45.f, 45.f, 1.f, 1.f);

    auto plan = planStructureOptimization(objs);
    assertPlanGeometryValid(objs, plan);
    assert(plan.outputCount == 1);
    assert(plan.merges.size() == 1);
    assert(near(plan.merges[0].newX, 45.f));
    assert(near(plan.merges[0].newY, 45.f));
    assert(near(plan.merges[0].newScaleX, 3.f));
    assert(near(plan.merges[0].newScaleY, 3.f));
}

void test2DExactIrregularShapes() {
    // Shape with bottom-left/right tabs & top center:
    // . X X .
    // X X X X
    // X X X X
    // X . . X
    // Optimal = 3 rectangles: (0,0..2) 1x3, (1..2,1..3) 2x3, (3,0..2) 1x3.
    std::vector<ObjectRecord> objs;
    auto put = [&](int gx, int gy) {
        objs.push_back(makeBlock(objs.size(), 15.f + gx * 30.f, 15.f + gy * 30.f,
                                 211, StretchCapability::Uniform2D, false));
    };
    put(0, 0); put(3, 0);
    for (int x = 0; x < 4; ++x) { put(x, 1); put(x, 2); }
    put(1, 3); put(2, 3);

    OptimizerOptions opts;
    opts.maxDecoScale = 8.f;
    opts.merge2DFills = true;
    auto plan = planStructureOptimization(objs, opts);
    assertPlanGeometryValid(objs, plan);
    assert(plan.outputCount == 3);
}

void testMultiComponentIndependentAxisOrientation() {
    // Same compatibilityKey bucket contains TWO disconnected Axis1D rectangles:
    // Component 1: 3 wide x 5 tall (optimal = 3 vertical 1x5 strips)
    // Component 2: 5 wide x 3 tall far away (optimal = 3 horizontal 5x1 strips)
    // Total optimal = 6 objects (instead of 8 if forced to a single global axis!).
    std::vector<ObjectRecord> objs;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 3; ++x) {
            objs.push_back(makeBlock(objs.size(), 15.f + x * 30.f, 15.f + y * 30.f,
                                     1, StretchCapability::Axis1D, true));
        }
    }
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            objs.push_back(makeBlock(objs.size(), 615.f + x * 30.f, 15.f + y * 30.f,
                                     1, StretchCapability::Axis1D, true));
        }
    }
    OptimizerOptions opts;
    opts.maxCollisionScale = 8.f;
    opts.preserveCorners = false;
    auto plan = planStructureOptimization(objs, opts);
    assertPlanGeometryValid(objs, plan);
    assert(plan.outputCount == 6);
}

void testAxis1DPinwheelAndLShape() {
    // 6x6 Axis1D grid with maxCollisionScale = 4:
    // Naive U-first or V-first produces 12 objects; pinwheel cover achieves 10 objects!
    std::vector<ObjectRecord> grid6x6;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            grid6x6.push_back(makeBlock(grid6x6.size(), 15.f + x * 30.f, 15.f + y * 30.f,
                                        1, StretchCapability::Axis1D, true));
        }
    }
    OptimizerOptions opts;
    opts.maxCollisionScale = 4.f;
    opts.preserveCorners = false;
    auto plan6 = planStructureOptimization(grid6x6, opts);
    assertPlanGeometryValid(grid6x6, plan6);
    assert(plan6.outputCount <= 10);

    // 2x6 vertical pillar joined with 5x2 horizontal foot (overlap at 2x2) = 20 cells:
    // Naive U-first = 6, naive V-first = 5, mixed H+V optimal = 4!
    std::vector<ObjectRecord> lBlock;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (x < 2 || y < 2) {
                lBlock.push_back(makeBlock(lBlock.size(), 15.f + x * 30.f, 15.f + y * 30.f,
                                           1, StretchCapability::Axis1D, true));
            }
        }
    }
    opts.maxCollisionScale = 8.f;
    auto planL = planStructureOptimization(lBlock, opts);
    assertPlanGeometryValid(lBlock, planL);
    assert(planL.outputCount == 4);
}

void testMixedQuarterTurnKeeperFrame() {
    // Two Uniform2D blocks in the same compatibility bucket where the first (keeper) is rotated 90 deg
    // and stretched 2x1 in its local frame (which is 1x2 in world), and the second is 0 deg 1x2 in world.
    // Combined world span is 2x2 (60x60), so keeper (at 90 deg) must receive scaleX=2, scaleY=2.
    std::vector<ObjectRecord> objs;
    ObjectRecord a = makeBlock(0, 15.f, 30.f, 211, StretchCapability::Uniform2D, false, 90.f, "same");
    a.scaleX = 2.f;
    a.scaleY = 1.f;
    a.compatibilityKey = "same|211";
    ObjectRecord b = makeBlock(1, 45.f, 30.f, 211, StretchCapability::Uniform2D, false, 0.f, "same");
    b.scaleX = 1.f;
    b.scaleY = 2.f;
    b.compatibilityKey = "same|211";
    objs.push_back(a);
    objs.push_back(b);

    auto plan = planStructureOptimization(objs);
    assertPlanGeometryValid(objs, plan);
    assert(plan.outputCount == 1);
    assert(plan.merges.size() == 1);
    assert(near(plan.merges[0].newX, 30.f));
    assert(near(plan.merges[0].newY, 30.f));
    assert(near(plan.merges[0].newScaleX, 2.f));
    assert(near(plan.merges[0].newScaleY, 2.f));

    // Also test non-square merge (1x3 world) when keeper is rotated 90 deg -> keeper's local X is world -Y!
    std::vector<ObjectRecord> vStrip;
    for (int i = 0; i < 3; ++i) {
        ObjectRecord r = makeBlock(i, 15.f, 15.f + i * 30.f, 211, StretchCapability::Uniform2D, false,
                                   i == 0 ? 90.f : 0.f, "same");
        r.compatibilityKey = "same|211";
        vStrip.push_back(r);
    }
    auto stripPlan = planStructureOptimization(vStrip);
    assertPlanGeometryValid(vStrip, stripPlan);
    assert(stripPlan.outputCount == 1);
    assert(stripPlan.merges[0].keeperIndex == 0);
    assert(near(stripPlan.merges[0].newX, 15.f));
    assert(near(stripPlan.merges[0].newY, 45.f));
    assert(near(stripPlan.merges[0].newScaleX, 3.f)); // local X of 90-deg keeper = world Y (90px)
    assert(near(stripPlan.merges[0].newScaleY, 1.f));
}

void testJitteredBandsAndLargeScale() {
    // Three blocks whose v coordinates step by 0.20 (0.0, 0.20, 0.40) within posTolerance = 0.25:
    // Previously caused non-transitive comparator UB in std::sort.
    std::vector<ObjectRecord> jitter;
    jitter.push_back(makeBlock(0, 15.f, 15.0f, 1, StretchCapability::Axis1D, true));
    jitter.push_back(makeBlock(1, 45.f, 15.20f, 1, StretchCapability::Axis1D, true));
    jitter.push_back(makeBlock(2, 75.f, 15.40f, 1, StretchCapability::Axis1D, true));
    auto jPlan = planStructureOptimization(jitter);
    assertPlanGeometryValid(jitter, jPlan);
    assert(jPlan.outputCount <= 2);

    // 2000-block grid (50x40) performance & validity check
    std::vector<ObjectRecord> big;
    big.reserve(2000);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 50; ++x) {
            big.push_back(makeBlock(big.size(), 15.f + x * 30.f, 15.f + y * 30.f,
                                    211, StretchCapability::Uniform2D, false));
        }
    }
    OptimizerOptions opts;
    opts.maxDecoScale = 16.f;
    opts.maxPixelSpan = 480.f;
    auto bigPlan = planStructureOptimization(big, opts);
    assertPlanGeometryValid(big, bigPlan);
    // 50 = 16*3 + 2 (4 pieces), 40 = 16*2 + 8 (3 pieces) => optimal = 4 * 3 = 12 objects!
    assert(bigPlan.outputCount == 12);
}

void testUniform2DWith2DDisabledUses1DSolver() {
    // Same L-shape as Axis1D, but with Uniform2D blocks and merge2DFills = false:
    // Should still achieve optimal 1D mixed H+V cover (4 objects, not 5 or 6).
    std::vector<ObjectRecord> lBlock;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (x < 2 || y < 2) {
                lBlock.push_back(makeBlock(lBlock.size(), 15.f + x * 30.f, 15.f + y * 30.f,
                                           211, StretchCapability::Uniform2D, false));
            }
        }
    }
    OptimizerOptions opts;
    opts.maxDecoScale = 8.f;
    opts.merge2DFills = false;
    opts.preserveCorners = false;
    auto plan = planStructureOptimization(lBlock, opts);
    assertPlanGeometryValid(lBlock, plan);
    assert(plan.outputCount == 4);
    for (auto const& m : plan.merges) {
        assert(near(m.newScaleX, 1.f) || near(m.newScaleY, 1.f));
    }
}

void testPreStretchedAndKeeperFlipSelection() {
    // Pre-stretched 2x1 + 1x1 + 1x1 -> 4x1, preferring unflipped 0-deg keeper.
    std::vector<ObjectRecord> objs;
    ObjectRecord a = makeBlock(0, 30.f, 15.f, 1, StretchCapability::Axis1D, true, 0.f, "k");
    a.scaleX = 2.f;
    a.flipX = true;
    objs.push_back(a);
    ObjectRecord b = makeBlock(1, 75.f, 15.f, 1, StretchCapability::Axis1D, true, 0.f, "k");
    objs.push_back(b);
    ObjectRecord c = makeBlock(2, 105.f, 15.f, 1, StretchCapability::Axis1D, true, 0.f, "k");
    objs.push_back(c);

    auto plan = planStructureOptimization(objs);
    assertPlanGeometryValid(objs, plan);
    assert(plan.outputCount == 1);
    assert(plan.merges.size() == 1);
    assert(plan.merges[0].keeperIndex == 1);
    assert(near(plan.merges[0].newX, 60.f));
    assert(near(plan.merges[0].newScaleX, 4.f));
    assert(near(plan.merges[0].newScaleY, 1.f));
}

void testLargerExactStripCover44Cells() {
    // 44-cell U-shape: 2x8 left, 2x8 right, 6x2 bottom = 16 + 16 + 12 = 44 cells.
    // With maxCollisionScale = 10, optimal = 6 strips (2+2+2).
    std::vector<ObjectRecord> uShape;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 10; ++x) {
            if (x < 2 || x >= 8 || y < 2) {
                uShape.push_back(makeBlock(uShape.size(), 15.f + x * 30.f, 15.f + y * 30.f,
                                           1, StretchCapability::Axis1D, true));
            }
        }
    }
    assert(uShape.size() == 44);
    OptimizerOptions opts;
    opts.maxCollisionScale = 10.f;
    opts.preserveCorners = false;
    auto plan = planStructureOptimization(uShape, opts);
    assertPlanGeometryValid(uShape, plan);
    assert(plan.outputCount == 6);
}

} // namespace

int main() {
    testFourInARow();
    testCollisionAndDecoMaxLengthCaps();
    testCornerBlocksPreserved();
    testTopologicalElbowCornerPreserved();
    test2DFillMerge();
    testRotatedVerticalLine();
    testIncompatibleAndOverlappingDoNotMerge();
    testRewriteObjectTransform();
    testMixedPreStretched2DPinwheel();
    test2DExactIrregularShapes();
    testMultiComponentIndependentAxisOrientation();
    testAxis1DPinwheelAndLShape();
    testMixedQuarterTurnKeeperFrame();
    testJitteredBandsAndLargeScale();
    testUniform2DWith2DDisabledUses1DSolver();
    testPreStretchedAndKeeperFlipSelection();
    testLargerExactStripCover44Cells();

    std::cout << "All Structure Optimizer regression checks passed.\n";
    return 0;
}
