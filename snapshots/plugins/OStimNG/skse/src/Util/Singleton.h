#pragma once

namespace Util {
    template <class T>
    class Singleton {
    public:
        Singleton() = default;
        Singleton(const Singleton&) = delete;
        Singleton(Singleton&&) = delete;
        Singleton& operator=(const Singleton&) = delete;
        Singleton& operator=(Singleton&&) = delete;
        ~Singleton() = default;

        static T* getSingleton() noexcept {
            static T singleton;
            return std::addressof(singleton);
        }
    };
}