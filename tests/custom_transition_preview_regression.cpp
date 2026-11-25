#include "../src/features/transitions/services/TransitionPreview.hpp"
#include <cassert>
#include <cmath>

using namespace paimon::transitions;
bool close(float a, float b) { return std::abs(a - b) < .001f; }

int main() {
    TransitionPreview preview({1000.f, 500.f}, {300.f, 150.f}, true);
    TransitionVisual scene;
    scene.visible = true; scene.position = {500.f, 250.f}; scene.opacity = 0.f;
    auto a = preview.scene(scene, false), b = preview.scene(scene, true);
    assert(close(a.visual.position.x, 75.f) && close(b.visual.position.x, 225.f));
    assert(a.widthScale == .5f && b.widthScale == .5f);
    assert(a.visual.opacity > 0.f && b.visual.opacity > 0.f); // both guides remain readable
    TransitionVisual image;
    image.position = {500.f, 250.f}; image.visible = true; image.opacity = 255.f;
    assert(preview.object(image).opacity < 255.f); // a fullscreen image cannot obscure the guide boxes
    image.opacity = 0.f;
    assert(preview.object(image).opacity == 0.f); // guide mode must still show Alpha going invisible
    preview = TransitionPreview({1000.f, 500.f}, {300.f, 150.f}, false);
    assert(preview.scene(scene, false).visual.opacity == 0.f);
    assert(preview.scene(scene, true).widthScale == 1.f);
    assert(close(preview.scene(scene, true).visual.position.x, 150.f));
    image.opacity = 255.f;
    assert(preview.object(image).opacity == 255.f); // final playback preserves the actual composition
}
