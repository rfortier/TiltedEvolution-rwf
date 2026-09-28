#pragma once

#include "Role.h"

namespace Graph {
	template<class T>
    struct RoleMap {
    public:
        T actor;
        T target;
        T performer;

        T* get(Role role) {
            switch (role) {
                case Role::ACTOR:
                    return &actor;
                case Role::TARGET:
                    return &target;
                case Role::PERFORMER:
                    return &performer;
                default:
                    return nullptr;
            }
        }

        void forEach(std::function<void(T&)> lambda) {
            lambda(actor);
            lambda(target);
            lambda(performer);
        }

        void forEach(std::function<void(Role, T&)> lambda) {
            lambda(Role::ACTOR, actor);
            lambda(Role::TARGET, target);
            lambda(Role::PERFORMER, performer);
        }
	};

    class RoleMapAPI {
    public:
        inline static RoleMap<std::string> KEYS{"actor", "target", "performer"};
        inline static RoleMap<std::string> TERMS{"Actor", "Target", "Performer"};
    };
}