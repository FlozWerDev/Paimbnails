#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace paimon::profile_redesign {

struct ProfileLevelSummary {
    int id, likes;
    std::string encoded;
};

// collect every server page before sorting. User-level searches are recent-first.
class ProfileLevelCatalog {
public:
    bool appendResponse(std::string_view response, int userID) {
        if (m_complete || userID <= 0) return false;
        if (response == "-1") {
            if (m_nextPage != 0) return false;
            m_complete = true;
            return true;
        }
        auto const sections = split(response, "#");
        if (sections.size() < 4) return false;
        auto const pagination = split(sections[3], ":");
        int total = 0, offset = 0, count = 0;
        if (pagination.size() != 3 || !integer(pagination[0], total) || !integer(pagination[1], offset) ||
            !integer(pagination[2], count) || total < 0 || offset < 0 || count <= 0 ||
            static_cast<std::int64_t>(m_nextPage) * count != offset) return false;

        std::vector<ProfileLevelSummary> incoming;
        for (auto const row : split(sections[0], "|")) {
            if (row.empty()) continue;
            auto const fields = split(row, ":");
            if (fields.size() % 2 != 0) return false;
            int id = 0, creator = 0, likes = 0;
            bool hasLikes = false;
            for (std::size_t index = 0; index < fields.size(); index += 2) {
                auto const key = fields[index], value = fields[index + 1];
                if (key == "1" && !integer(value, id)) return false;
                if (key == "6" && !integer(value, creator)) return false;
                if (key == "14") {
                    if (!integer(value, likes)) return false;
                    hasLikes = true;
                }
            }
            if (id <= 0 || creator != userID || !hasLikes) return false;
            incoming.push_back({id, likes, std::string(row)});
        }
        if (incoming.empty() && total != 0) return false;
        if (!incoming.empty() && total == 0) return false;
        bool const more = static_cast<std::int64_t>(offset) + count < total;
        if (more && std::ranges::all_of(incoming, [this](auto const& level) { return m_ids.contains(level.id); })) return false;
        for (auto& level : incoming) {
            if (m_ids.insert(level.id).second) m_levels.push_back(std::move(level));
        }
        m_total = total;
        ++m_nextPage;
        m_complete = !more;
        if (m_complete) {
            std::ranges::sort(m_levels, [](auto const& left, auto const& right) {
                return left.likes != right.likes ? left.likes > right.likes : left.id > right.id;
            });
        }
        return true;
    }

    int nextPage() const { return m_nextPage; }
    int total() const { return m_total; }
    bool complete() const { return m_complete; }
    std::vector<ProfileLevelSummary> const& levels() const { return m_levels; }
    std::size_t pageCount(std::size_t size = 10) const {
        return size ? m_levels.size() / size + (m_levels.size() % size != 0) : 0;
    }
    std::span<ProfileLevelSummary const> page(std::size_t index, std::size_t size = 10) const {
        if (!size || index >= pageCount(size)) return {};
        auto const start = index * size;
        return std::span<ProfileLevelSummary const>(m_levels).subspan(start, std::min(size, m_levels.size() - start));
    }

private:
    static bool integer(std::string_view text, int& value) {
        if (text.empty()) return false;
        auto const result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }
    static std::vector<std::string_view> split(std::string_view text, std::string_view delimiter) {
        std::vector<std::string_view> parts;
        for (;;) {
            auto const end = text.find(delimiter);
            parts.push_back(text.substr(0, end));
            if (end == std::string_view::npos) return parts;
            text.remove_prefix(end + delimiter.size());
        }
    }
    std::vector<ProfileLevelSummary> m_levels;
    std::unordered_set<int> m_ids;
    int m_nextPage = 0, m_total = 0;
    bool m_complete = false;
};

} // namespace paimon::profile_redesign
