#pragma once
//
// SelfTest.hpp - Offline correctness check for the engine pipeline.
//
// The test builds a tiny synthetic sprite in memory (no file IO):
//
//   .....OOOO.....
//   .....OOOO.....
//   ..GGGGOOOOGGGG..
//   ..GGGGOOOOGGGG..
//   ..GGGGBBBBGGGG..
//   ..GGGGBBBBGGGG..
//   .....BBBB.....
//   .....BBBB.....
//
// where O = outline (#222222), G = green (#3FCC2F, "Color 1"), B = black
// fill (#0F0F0F, "Color 2"). It then runs:
//
//   1. ColorClustering::compute → expect 3 clusters (outline / green / black).
//   2. ClusterClassifier::classify → expect roles {Outline, Color1, Color2}.
//   3. MaskBuilder::build → 3 non-empty masks.
//   4. LuminanceTinter::apply with C1=red, C2=blue, glow=white.
//   5. Verify: green pixels became reddish, black pixels became bluish,
//      outline pixels stayed dark.
//
// Run via `paimon::texture_studio::engineSelfTest()` from anywhere — it
// writes results to `log::info` and returns true on full success. The test
// is opt-in (we don't auto-run it on mod load) because it does ~10ms of
// work and there's no value to running it on user machines.
//

namespace paimon::texture_studio {

// Returns true if every assertion passes. Always logs detailed results.
bool engineSelfTest();

}  // namespace paimon::texture_studio
