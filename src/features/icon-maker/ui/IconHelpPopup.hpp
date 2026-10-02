#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>

namespace paimon::icon_maker {

class IconHelpPopup : public PaimonPopup {
public:
    enum class Topic { Basics = 0, Canvas = 1, Paint = 2, Export = 3 };

    static IconHelpPopup* create(Topic topic = Topic::Basics);

protected:
    bool init(Topic topic);
    void showTopic(Topic topic);

    cocos2d::CCNode* m_body = nullptr;
};

}  // namespace paimon::icon_maker
