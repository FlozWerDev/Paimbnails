#pragma once

// Public Group View API (inspired by Tinker improved_group_view).
// Modules / future external code can request a rebuild of the group list.

namespace paimon::editor::group_view {

// Post GroupViewUpdateEvent — listeners (GroupViewImproved) rebuild.
void updateGroupView();

} // namespace paimon::editor::group_view
