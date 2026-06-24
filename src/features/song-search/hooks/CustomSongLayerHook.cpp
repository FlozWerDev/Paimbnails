
#include <Geode/Geode.hpp>
#include <Geode/modify/CustomSongLayer.hpp>

#include "../services/NewgroundsSongSearch.hpp"

using namespace geode::prelude;

namespace {
    inline bool songSearchEnabled() {
        return Mod::get()->getSettingValue<bool>("song-search-enable");
    }
}

class $modify(PaimonSongSearchLayer, CustomSongLayer) {
    struct Fields {
        bool m_searching = false;
    };

    $override
    bool init(CustomSongDelegate* delegate) {
        if (!CustomSongLayer::init(delegate)) return false;
        if (!songSearchEnabled()) return true;

        if (this->m_songIDInput) {
            this->m_songIDInput->setAllowedChars(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-.");
            this->m_songIDInput->setMaxLabelLength(200);
        }
        return true;
    }

    $override
    void onSearch(CCObject* sender) {
        if (!songSearchEnabled() || !this->m_songIDInput) {
            CustomSongLayer::onSearch(sender);
            return;
        }

        std::string query = this->m_songIDInput->getString();

        // Numeric (or empty) input -> normal song-id lookup.
        if (query.empty() || paimon::songsearch::isNumericID(query)) {
            CustomSongLayer::onSearch(sender);
            return;
        }
        if (m_fields->m_searching) return;
        m_fields->m_searching = true;

        WeakRef<PaimonSongSearchLayer> weakSelf = this;
        paimon::songsearch::resolveByName(
            query,
            [weakSelf](paimon::songsearch::SearchResult result) {
                auto self = weakSelf.lock();
                if (!self) return;
                self->m_fields->m_searching = false;

                using paimon::songsearch::SearchStatus;
                switch (result.status) {
                    case SearchStatus::Found:
                        if (self->m_songIDInput) {
                            self->m_songIDInput->setString(result.songID.c_str());
                        }
                        self->CustomSongLayer::onSearch(nullptr);
                        break;
                    case SearchStatus::NoResults:
                        FLAlertLayer::create(
                            "No Results",
                            "No songs on Newgrounds matched your search.",
                            "OK")->show();
                        break;
                    case SearchStatus::NetworkError:
                        FLAlertLayer::create(
                            "Connection Error",
                            "Couldn't reach Newgrounds. It may be blocking the "
                            "request, or your connection is down.",
                            "OK")->show();
                        break;
                }
            }
        );
    }
};
