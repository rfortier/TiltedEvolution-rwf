#pragma once

namespace EventUtil {
    template <typename... Ts>
    void invokeListeners(std::vector<std::function<void(Ts...)>>& listeners, Ts... args) {
        for (std::function<void(Ts...)>& listener : listeners) {
            listener(args...);
        }
    }
}