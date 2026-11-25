#pragma once

#include "../../../ui/PaimonPopup.hpp"

#include <Geode/ui/TextInput.hpp>

namespace paimon::twitch {

class RequestQueueToolsPopup : public PaimonPopup {
public:
    static RequestQueueToolsPopup* create();
    ~RequestQueueToolsPopup() override;

protected:
    bool init() override;

private:
    void addManualRequest();
    void copyPendingIDs();
    void confirmRemoveReviewed();
    void confirmClearQueue();
    bool queueStillSelected() const;
    void showStatus(std::string const& text, bool success);

    std::string m_queue;
    geode::TextInput* m_requestInput = nullptr;
    geode::TextInput* m_requesterInput = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
};

} // namespace paimon::twitch
