#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <mutex>

enum class PendingCategory { Verify, Update, Report, ProfileBackground, ProfileImg };
enum class PendingStatus { Open, Accepted, Rejected };

struct Suggestion {
    std::string filename;
    std::string submittedBy;
    int64_t timestamp = 0;
    int accountID = 0;
};

struct ReportEntry {
    std::string reporter;
    int reporterAccountID = 0;
    std::string note;
    int64_t timestamp = 0;
};

struct PendingItem {
    int levelID = 0;
    PendingCategory category = PendingCategory::Verify;
    int64_t timestamp = 0;
    std::string submittedBy;
    std::string note;
    std::string claimedBy;
    PendingStatus status = PendingStatus::Open;
    bool isCreator = false;
    
    std::vector<Suggestion> suggestions;

    std::string type;
    std::string reportedUsername;
    std::vector<ReportEntry> reports;
};

class PendingQueue {
public:
    static PendingQueue& get();

    void addOrBump(int levelID, PendingCategory cat, std::string submittedBy = {}, std::string note = {}, bool isCreator = false);

    void removeForLevel(int levelID);

    void reject(int levelID, PendingCategory cat, std::string reason = {});

    void accept(int levelID, PendingCategory cat);

    std::vector<PendingItem> list(PendingCategory cat) const;

    void load();
    void save();

    std::string toJson() const;

    void syncNow();
    
    static char const* catToStr(PendingCategory c);

    static bool isLevelCreator(GJGameLevel* level, std::string const& username);

private:
    PendingQueue() = default;
    std::filesystem::path jsonPath() const;
    static PendingCategory strToCat(std::string const& s);
    static char const* statusToStr(PendingStatus s);
    static PendingStatus strToStatus(std::string const& s);
    static std::string escape(std::string const& s);

    bool m_loaded = false;
    std::once_flag m_loadFlag;
    mutable std::vector<PendingItem> m_items;
};

