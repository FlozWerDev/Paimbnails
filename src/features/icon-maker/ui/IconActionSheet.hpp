#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

#include <functional>
#include <string>
#include <vector>

namespace paimon::icon_maker {

class IconActionSheet : public PaimonPopup {
public:
    struct Action {
        std::string label;
        std::string desc;
        std::function<void()> run;
        bool destructive = false;
    };

    // the sheet closes before running the action, so callbacks are free to
    // open another popup or rebuild the scene behind it.
    static IconActionSheet* create(std::string title, std::vector<Action> actions);

protected:
    bool init(std::string title, std::vector<Action> actions);
};

}  // namespace paimon::icon_maker
