#pragma once
#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include "../services/ForumApi.hpp"

struct PostDetailCallbacks {
    geode::CopyableFunction<void()> onChanged;
    geode::CopyableFunction<void(std::string const&)> onFilterTag;
    geode::CopyableFunction<void(int, std::string const&)> onFilterAuthor;
};

class PostDetailPopup : public PaimonPopup {
protected:
    paimon::forum::Post m_post;
    PostDetailCallbacks m_callbacks;

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput*   m_replyInput = nullptr;
    cocos2d::CCLabelBMFont* m_cooldownLabel = nullptr;
    cocos2d::CCLabelBMFont* m_replyCharLabel = nullptr;
    cocos2d::CCLabelBMFont* m_authorStatusLabel = nullptr;

    std::string m_replyTo;
    std::string m_replyToAuthor;
    std::string m_authorStatusText;
    cocos2d::ccColor3B m_authorStatusColor{170, 175, 190};

    int m_replySort = 0; // 0 = Oldest, 1 = Newest, 2 = Top Liked
    bool m_submittingReply = false;
    float m_scrollTarget = 0.f;
    bool m_scrollTargetSet = false;

    bool init(paimon::forum::Post const& post, PostDetailCallbacks callbacks);

    void rebuild();
    void fetchAuthorStatus();
    void refreshFromServer();
    cocos2d::CCNode* makeAuthorRow(paimon::forum::Author const& author, int64_t when, float w);
    cocos2d::CCNode* makeReplyCard(paimon::forum::Reply const& r, int depth, std::string const& parentAuthor, float w, int index);

    void onLikePost(cocos2d::CCObject*);
    void onBookmarkPost(cocos2d::CCObject*);
    void onCopyPost(cocos2d::CCObject*);
    void onReportPost(cocos2d::CCObject*);
    void onDeletePost(cocos2d::CCObject*);
    void onSubmitReply(cocos2d::CCObject*);
    void onLikeReplyById(std::string id);
    void onReportReplyById(std::string id);
    void onDeleteReplyById(std::string id);
    void onReplyToReply(std::string id, std::string authorName);
    void updateCooldownLabel();
    void updateReplyCounter();
    void submitReport(std::string const& targetId, bool isReply);

public:
    static PostDetailPopup* create(paimon::forum::Post const& post,
                                   geode::CopyableFunction<void()> onChanged);
    static PostDetailPopup* create(paimon::forum::Post const& post,
                                   PostDetailCallbacks callbacks);
    void onExit() override;
    bool scrollWheel(float x, float y) override;
    void update(float dt) override;
};
