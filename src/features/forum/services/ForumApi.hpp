#pragma once
#include <Geode/Geode.hpp>
#include <string>
#include <vector>
#include <cstdint>

// Forum client for Paimbnails: REST models + endpoints, with a local cache
// (Mod savedValues) fallback when offline.
//
//   GET    /api/forum/posts                (filters: ?sort&tags&q&page&limit)
//   GET    /api/forum/posts/:postId
//   POST   /api/forum/posts
//   DELETE /api/forum/posts/:postId
//   POST   /api/forum/posts/:postId/like
//   POST   /api/forum/posts/:postId/report
//   GET    /api/forum/posts/:postId/replies
//   POST   /api/forum/posts/:postId/replies        (parentReplyId optional for threads)
//   POST   /api/forum/replies/:replyId/like
//   POST   /api/forum/replies/:replyId/report
//   DELETE /api/forum/replies/:replyId
//   GET    /api/forum/tags                          (predefined + popular)

namespace paimon::forum {

    // Post/reply author; filled from GD user info on create, or from server JSON.
    struct Author {
        int accountID = 0;
        std::string username;
        int iconID = 1;
        int iconType = 0;     // IconType (0=cube)
        int color1 = 0;
        int color2 = 3;
        bool glowEnabled = false;

        matjson::Value toJson() const;
        static Author fromJson(matjson::Value const& v);
        static Author currentUser(); // from GameManager + GJAccountManager
    };

    struct Reply {
        std::string id;
        std::string postId;
        std::string parentReplyId; // empty = direct reply; otherwise a thread
        Author author;
        std::string content;
        int64_t createdAt = 0;     // epoch seconds
        int likes = 0;
        bool likedByMe = false;
        int reportCount = 0;

        matjson::Value toJson() const;
        static Reply fromJson(matjson::Value const& v);
    };

    struct Post {
        std::string id;
        Author author;
        std::string title;
        std::string description;
        std::vector<std::string> tags;
        int64_t createdAt = 0;     // epoch seconds (UTC)
        int64_t updatedAt = 0;
        int likes = 0;
        bool likedByMe = false;
        int replyCount = 0;
        int reportCount = 0;
        bool pinned = false;
        bool locked = false;
        std::vector<Reply> replies; // only filled in getPost

        matjson::Value toJson() const;
        static Post fromJson(matjson::Value const& v);
    };

    enum class SortMode {
        Recent = 0,
        TopRated = 1,
        MostLiked = 2,
    };

    struct ListFilter {
        SortMode sort = SortMode::Recent;
        std::vector<std::string> tags; // OR-match if not empty
        std::string query;
        int page = 1;
        int limit = 20;
    };

    struct CreatePostRequest {
        std::string title;
        std::string description;
        std::vector<std::string> tags;
    };

    struct CreateReplyRequest {
        std::string postId;
        std::string parentReplyId; // empty = direct reply
        std::string content;
    };

    struct UserStatus {
        int accountID = 0;
        bool online = false;
        int64_t lastSeen = 0; // epoch seconds

        matjson::Value toJson() const;
        static UserStatus fromJson(matjson::Value const& v);
    };

    struct ProfileView {
        int viewerAccountID = 0;
        std::string viewerUsername;
        int64_t viewedAt = 0; // epoch seconds

        matjson::Value toJson() const;
        static ProfileView fromJson(matjson::Value const& v);
    };

    // Generic result of API operations.
    template<typename T>
    struct Result {
        bool ok = false;
        std::string error;
        T data{};
    };

    using ListCallback   = geode::CopyableFunction<void(Result<std::vector<Post>>)>;
    using PostCallback   = geode::CopyableFunction<void(Result<Post>)>;
    using ReplyCallback  = geode::CopyableFunction<void(Result<Reply>)>;
    using BoolCallback   = geode::CopyableFunction<void(Result<bool>)>;
    using TagsCallback   = geode::CopyableFunction<void(Result<std::vector<std::string>>)>;
    using UserStatusCallback = geode::CopyableFunction<void(Result<UserStatus>)>;
    using ProfileViewsCallback = geode::CopyableFunction<void(Result<std::vector<ProfileView>>)>;

    class ForumApi {
    public:
        static ForumApi& get();

        // Endpoints
        void listPosts(ListFilter const& filter, ListCallback cb);
        void getPost(std::string const& postId, PostCallback cb);
        void createPost(CreatePostRequest const& req, PostCallback cb);
        void deletePost(std::string const& postId, BoolCallback cb);
        void togglePostLike(std::string const& postId, BoolCallback cb);
        void reportPost(std::string const& postId, std::string const& reason, BoolCallback cb);

        void createReply(CreateReplyRequest const& req, ReplyCallback cb);
        void deleteReply(std::string const& replyId, BoolCallback cb);
        void toggleReplyLike(std::string const& replyId, BoolCallback cb);
        void reportReply(std::string const& replyId, std::string const& reason, BoolCallback cb);

        void listTags(TagsCallback cb);

        // User status (online/offline)
        void getUserStatus(int accountID, UserStatusCallback cb);
        void sendHeartbeat(BoolCallback cb);

        // Profile views (who viewed your profile)
        void recordProfileView(int accountID, BoolCallback cb);
        void getProfileViews(int accountID, ProfileViewsCallback cb);

        // Local cache (offline fallback), persisted in Mod savedValues.
        void loadCache();
        void saveCache();
        std::vector<Post> const& cachedPosts() const { return m_cache; }
        bool hasServer() const;

        // Cooldown tracking (client-side rate limit awareness)
        int64_t getPostCooldownRemaining() const;
        int64_t getReplyCooldownRemaining() const;
        void markPostCooldown(int64_t seconds);
        void markReplyCooldown(int64_t seconds);

    private:
        ForumApi();
        std::vector<Post> m_cache;
        bool m_loaded = false;

        // cooldowns (epoch seconds when the cooldown expires)
        int64_t m_postCooldownUntil = 0;
        int64_t m_replyCooldownUntil = 0;

        // helpers
        Post& upsertCache(Post const& p);
        Post* findCached(std::string const& id);
        std::string makeLocalId() const;
        static int64_t nowEpoch();
        static std::string sortToString(SortMode s);
        static std::string urlEncode(std::string const& s);
    };

    // Helpers UI
    std::string formatRelativeTime(int64_t epoch);
    std::string formatAbsoluteTime(int64_t epoch);

} // namespace paimon::forum
