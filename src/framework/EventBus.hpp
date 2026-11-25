#pragma once

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <memory>
#include <cstdint>
#include <atomic>

namespace paimon {

using SubscriptionHandle = uint64_t;

class ISubscriberList {
public:
    virtual ~ISubscriberList() = default;
    virtual void removeSubscriber(SubscriptionHandle handle) = 0;
};

template <typename Event>
class SubscriberList final : public ISubscriberList {
public:
    struct Entry {
        SubscriptionHandle handle;
        std::function<void(Event const&)> callback;
    };

    SubscriptionHandle add(std::function<void(Event const&)> cb, SubscriptionHandle h) {
        std::lock_guard lock(m_entriesMutex);
        m_entries.push_back({h, std::move(cb)});
        return h;
    }

    void removeSubscriber(SubscriptionHandle handle) override {
        // destroyed after unlocking: the callback can hold the last reference
        // to a node whose destructor unsubscribes again.
        std::function<void(Event const&)> removed;
        {
            std::lock_guard lock(m_entriesMutex);
            auto it = std::find_if(m_entries.begin(), m_entries.end(),
                [handle](Entry const& e) { return e.handle == handle; });
            if (it == m_entries.end()) return;
            removed.swap(it->callback);
            m_entries.erase(it);
        }
    }

    void dispatch(Event const& event) const {
        // handlers may unsubscribe mid-dispatch.
        auto copy = snapshot();
        for (auto const& entry : copy) {
            entry.callback(event);
        }
    }

    size_t subscriberCount() const {
        std::lock_guard lock(m_entriesMutex);
        return m_entries.size();
    }

private:
    std::vector<Entry> snapshot() const {
        std::lock_guard lock(m_entriesMutex);
        return m_entries;
    }

    mutable std::mutex m_entriesMutex;
    std::vector<Entry> m_entries;
};

// typed pub/sub; every operation takes the lock.
class EventBus {
public:
    static EventBus& get() {
        static EventBus instance;
        return instance;
    }

    template <typename Event>
    SubscriptionHandle subscribe(std::function<void(Event const&)> callback) {
        std::lock_guard lock(m_mutex);
        if (m_shuttingDown.load(std::memory_order_acquire)) return 0;
        auto& list = getOrCreate<Event>();
        SubscriptionHandle h = ++m_nextGlobalHandle;
        list.add(std::move(callback), h);
        m_handleToType.insert_or_assign(h, std::type_index(typeid(Event)));
        return h;
    }

    void unsubscribe(SubscriptionHandle handle) {
        std::shared_ptr<ISubscriberList> list;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_handleToType.find(handle);
            if (it == m_handleToType.end()) return;
            auto listIt = m_subscribers.find(it->second);
            m_handleToType.erase(it);
            if (listIt == m_subscribers.end()) return;
            list = listIt->second;
        }
        // outside m_mutex: dropping the callback can run a node destructor
        // that calls unsubscribe() again.
        list->removeSubscriber(handle);
    }

    void beginShutdown() {
        // weakref<ccnode> captured in lambdas die in atexit; clear them here instead.
        // the lambdas are destroyed after unlocking: one can release the last
        // reference to a node (e.g. a menulayer kept only by the weakref pool)
        // whose fields destructor calls unsubscribe(). locking m_mutex again
        // on the same thread throws, and inside a destructor that terminates.
        decltype(m_subscribers) subscribers;
        decltype(m_handleToType) handles;
        {
            std::lock_guard lock(m_mutex);
            m_shuttingDown.store(true, std::memory_order_release);
            subscribers.swap(m_subscribers);
            handles.swap(m_handleToType);
        }
    }

    template <typename Event>
    void publish(Event const& event) {
        if (m_shuttingDown.load(std::memory_order_acquire)) return;
        std::shared_ptr<ISubscriberList> kept;
        SubscriberList<Event>* list = nullptr;
        {
            std::lock_guard lock(m_mutex);
            if (m_shuttingDown.load(std::memory_order_acquire)) return;
            auto it = m_subscribers.find(std::type_index(typeid(Event)));
            if (it == m_subscribers.end()) return;
            kept = it->second;
            list = static_cast<SubscriberList<Event>*>(kept.get());
        }
        list->dispatch(event);
    }

    template <typename Event>
    size_t subscriberCount() {
        std::lock_guard lock(m_mutex);
        auto it = m_subscribers.find(std::type_index(typeid(Event)));
        if (it == m_subscribers.end()) return 0;
        return static_cast<SubscriberList<Event>*>(it->second.get())->subscriberCount();
    }

private:
    EventBus() = default;
    ~EventBus() = default;
    EventBus(EventBus const&) = delete;
    EventBus& operator=(EventBus const&) = delete;

    template <typename Event>
    SubscriberList<Event>& getOrCreate() {
        auto key = std::type_index(typeid(Event));
        auto it = m_subscribers.find(key);
        if (it != m_subscribers.end()) {
            return *static_cast<SubscriberList<Event>*>(it->second.get());
        }
        auto list = std::make_shared<SubscriberList<Event>>();
        auto& ref = *list;
        m_subscribers.emplace(key, std::move(list));
        return ref;
    }

    std::mutex m_mutex;
    std::atomic<bool> m_shuttingDown{false};
    SubscriptionHandle m_nextGlobalHandle = 0;
    std::unordered_map<std::type_index, std::shared_ptr<ISubscriberList>> m_subscribers;
    std::unordered_map<SubscriptionHandle, std::type_index> m_handleToType;
};

} // namespace paimon
