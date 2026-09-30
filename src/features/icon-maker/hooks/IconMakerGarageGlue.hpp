#pragma once

class GJGarageLayer;

namespace paimon::icon_maker::garage {

// no own garage button; entry is the paimon icons popup bottom strip.

// re-applies exact colors on the garage's main preview after gd re-tints it.
void onPlayerColorChanged(GJGarageLayer* layer);

}  // namespace paimon::icon_maker::garage
