#include "GroupViewAPI.hpp"
#include "Events.hpp"

namespace paimon::editor::group_view {

void updateGroupView() {
    GroupViewUpdateEvent().send();
}

} // namespace paimon::editor::group_view
