#pragma once

#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <typeindex>
#include <typeinfo>
#include <memory>
#include <cstdint>

namespace paimon {

// Handle opaco para desuscripcion.
using SubscriptionHandle = uint64_t;

// Interfaz base para contenedores de suscriptores tipados.
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
        std::lock_guard lock(m_entriesMutex);
        std::erase_if(m_entries, [handle](Entry const& e) { return e.handle == handle; });
    }

    void dispatch(Event const& event) const {
        // Copia para que un handler pueda desuscribirse durante dispatch
        // sin competir con el almacenamiento interno.
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

// EventBus — sistema tipado de pub/sub.
// Thread-safe: todas las operaciones adquieren lock.
// Uso:
//   auto handle = EventBus::get().subscribe<ThumbnailLoadedEvent>([](auto& e) { ... });
//   EventBus::get().publish(ThumbnailLoadedEvent{123, "network", false});
//   EventBus::get().unsubscribe(handle);
class EventBus {
public:
    static EventBus& get() {
        static EventBus instance;
        return instance;
    }

    template <typename Event>
    SubscriptionHandle subscribe(std::function<void(Event const&)> callback) {
        std::lock_guard lock(m_mutex);
        auto& list = getOrCreate<Event>();
        SubscriptionHandle h = ++m_nextGlobalHandle;
        list.add(std::move(callback), h);
        m_handleToType.insert_or_assign(h, std::type_index(typeid(Event)));
        return h;
    }

    void unsubscribe(SubscriptionHandle handle) {
        std::lock_guard lock(m_mutex);
        auto it = m_handleToType.find(handle);
        if (it == m_handleToType.end()) return;
        auto listIt = m_subscribers.find(it->second);
        if (listIt != m_subscribers.end()) {
            listIt->second->removeSubscriber(handle);
        }
        m_handleToType.erase(it);
    }

    template <typename Event>
    void publish(Event const& event) {
        std::shared_ptr<ISubscriberList> kept;
        SubscriberList<Event>* list = nullptr;
        {
            std::lock_guard lock(m_mutex);
            auto it = m_subscribers.find(std::type_index(typeid(Event)));
            if (it == m_subscribers.end()) return;
            // Copiar shared_ptr para mantener vivo el subscriber list
            // después de soltar el lock (evita use-after-free si otro hilo
            // borra la entrada del mapa entre unlock y dispatch).
            kept = it->second;
            list = static_cast<SubscriberList<Event>*>(kept.get());
        }
        // Dispatch fuera del lock para evitar deadlock si un handler publica.
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
    SubscriptionHandle m_nextGlobalHandle = 0;
    std::unordered_map<std::type_index, std::shared_ptr<ISubscriberList>> m_subscribers;
    std::unordered_map<SubscriptionHandle, std::type_index> m_handleToType;
};

} // namespace paimon
