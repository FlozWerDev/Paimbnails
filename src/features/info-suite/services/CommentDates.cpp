#include "CommentDates.hpp"
#include "InfoStore.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>

using namespace geode::prelude;

namespace paimon::info {

namespace {

constexpr int64_t kMinute = 60;
constexpr int64_t kHour = 60 * kMinute;
constexpr int64_t kDay = 24 * kHour;
constexpr int64_t kWeek = 7 * kDay;
constexpr int64_t kMonth = 30 * kDay;   // GD's own approximation
constexpr int64_t kYear = 365 * kDay;

int64_t nowSeconds() {
    return static_cast<int64_t>(std::time(nullptr));
}

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

int64_t parseRelativeAge(std::string const& text) {
    if (text.empty()) return 0;

    auto lowered = lower(text);

    // Leading number, then a unit word. Anything else (an absolute date from a
    // private server, for instance) is not ours to guess at.
    size_t i = 0;
    while (i < lowered.size() && std::isspace(static_cast<unsigned char>(lowered[i]))) i++;

    size_t start = i;
    while (i < lowered.size() && std::isdigit(static_cast<unsigned char>(lowered[i]))) i++;
    if (i == start) return 0;

    auto amount = geode::utils::numFromString<int64_t>(lowered.substr(start, i - start));
    if (!amount.isOk() || amount.unwrap() <= 0) return 0;
    int64_t value = amount.unwrap();

    auto unit = lowered.find_first_not_of(' ', i);
    if (unit == std::string::npos) return 0;
    auto rest = lowered.substr(unit);

    if (rest.rfind("second", 0) == 0) return value;
    if (rest.rfind("minute", 0) == 0) return value * kMinute;
    if (rest.rfind("hour", 0) == 0)   return value * kHour;
    if (rest.rfind("day", 0) == 0)    return value * kDay;
    if (rest.rfind("week", 0) == 0)   return value * kWeek;
    if (rest.rfind("month", 0) == 0)  return value * kMonth;
    if (rest.rfind("year", 0) == 0)   return value * kYear;
    return 0;
}

void noteComment(int64_t commentID, std::string const& relativeAge) {
    if (commentID <= 0) return;

    int64_t age = parseRelativeAge(relativeAge);
    if (age <= 0) return;

    // A "3 months" comment is only accurate to the month, but between two such
    // anchors the ids in the middle land far closer than the game's own text.
    InfoStore::get().addCommentSample(commentID, nowSeconds() - age);
}

std::string estimateCommentDate(int64_t commentID) {
    int64_t epoch = InfoStore::get().estimateCommentTime(commentID);
    if (epoch <= 0) return {};

    auto time = static_cast<std::time_t>(epoch);
    std::tm parts{};
#ifdef _WIN32
    if (localtime_s(&parts, &time) != 0) return {};
#else
    if (!localtime_r(&time, &parts)) return {};
#endif

    return fmt::format("{:02}/{:02}/{}", parts.tm_mday, parts.tm_mon + 1, parts.tm_year + 1900);
}

} // namespace paimon::info
