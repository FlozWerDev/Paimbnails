#include "../src/features/death-effects/services/DeathSfxFormat.hpp"

#include <cassert>

int main() {
    using namespace paimon::death_effects;

    // Openverse license ids become human labels with their version.
    assert(licenseLabel("by", "4.0") == "CC BY 4.0");
    assert(licenseLabel("by-nc-sa", "4.0") == "CC BY-NC-SA 4.0");
    assert(licenseLabel("cc0", "1.0") == "CC0 1.0");
    assert(licenseLabel("by", "") == "CC BY");
    assert(licenseLabel("", "4.0").empty());

    // Downloaded titles become safe, bounded file names.
    assert(sanitizeFileStem("Big Explosion!") == "Big-Explosion");
    assert(sanitizeFileStem("pop/pop\\pop") == "poppoppop");
    assert(sanitizeFileStem("  spaced  out ") == "spaced-out");
    assert(sanitizeFileStem("") == "openverse-sfx");
    assert(sanitizeFileStem("!!!") == "openverse-sfx");
    assert(sanitizeFileStem(std::string(100, 'a')).size() == 60);
}
