#pragma once

namespace paimon::factory_reset {

// Show destructive confirmation and run the reset if the user accepts.
void requestWithConfirmation();

// Wipe saved settings and caches, restore defaults.
void execute();

} // namespace paimon::factory_reset