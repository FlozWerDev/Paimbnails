#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/async.hpp>
#include "../../core/RuntimeLifecycle.hpp"
#include <arc/future/Future.hpp>
#include <arc/sync/oneshot.hpp>
#include <memory>
#include <utility>

namespace paimon::async_api {

template <typename T, typename StartFn>
    requires std::invocable<StartFn, geode::CopyableFunction<void(T)> const&>
arc::Future<T> awaitCallback(StartFn&& start) {
    auto [sender, receiver] = arc::oneshot::channel<T>();
    auto heldSender = std::make_shared<arc::oneshot::Sender<T>>(std::move(sender));
    auto heldStart = std::make_shared<std::decay_t<StartFn>>(std::forward<StartFn>(start));

    geode::CopyableFunction<void(T)> completion = [heldSender](T value) {
        geode::Loader::get()->queueInMainThread([heldSender, value = std::move(value)]() mutable {
            if (paimon::isRuntimeShuttingDown()) return;
            (void)heldSender->send(std::move(value));
        });
    };

    co_await geode::async::waitForMainThread<void>([heldStart, completion]() {
        (*heldStart)(completion);
    });

    auto received = co_await receiver.recv();
    if (received.isOk()) {
        co_return std::move(received).unwrap();
    }
    co_return T{};
}

} // namespace paimon::async_api 8) 