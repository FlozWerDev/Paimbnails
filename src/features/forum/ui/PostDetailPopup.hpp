#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/function.hpp>
#include "../services/ForumApi.hpp"

class PostDetailPopup : public geode::Popup {
protected:
    paimon::forum::Post m_post;
    geode::CopyableFunction<void()> m_onChanged; // notify hub on like/reply/delete

    geode::ScrollLayer* m_scroll = nullptr;
    geode::TextInput*   m_replyInput = nullptr;
    cocos2d::CCLabelBMFont* m_cooldownLabel = nullptr;
    std::string         m_replyTo; // parentReplyId si se quiere crear hilo

    bool init(paimon::forum::Post const& post,
              geode::CopyableFunction<void()> onChanged);

    void rebuild();          // refresca todo el contenido (header + replies)
    cocos2d::CCNode* makeAuthorRow(paimon::forum::Author const& author, int64_t when, float w);
    cocos2d::CCNode* makeReplyCard(paimon::forum::Reply const& r, float w);

    void onLikePost(cocos2d::CCObject*);
    void onReportPost(cocos2d::CCObject*);
    void onDeletePost(cocos2d::CCObject*);
    void onSubmitReply(cocos2d::CCObject*);
    void onLikeReplyById(std::string id);
    void onReportReplyById(std::string id);
    void onDeleteReplyById(std::string id);
    void onReplyToReply(std::string id);
    void updateCooldownLabel();

public:
    static PostDetailPopup* create(paimon::forum::Post const& post,
                                   geode::CopyableFunction<void()> onChanged);
    virtual void update(float dt) override;
};
