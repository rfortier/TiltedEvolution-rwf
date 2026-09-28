#pragma once

#include "ActorProperty.h"

namespace ActorProperties {
	template<class T>
	struct ActorPropertyList {
    public:
		void add(ActorProperty<T> element) {
			properties.push_back(element);
		}

		void sort() {
            std::stable_sort(properties.begin(), properties.end(), [&](ActorProperty<T> a, ActorProperty<T> b) {
				return a.condition.getPriority() > b.condition.getPriority();
			});
		}

		T get(GameAPI::GameActor actor, T fallback) {
            for (ActorProperty<T> prop : properties) {
                if (prop.condition.fulfills(actor)) {
                    return prop.value;
                }
            }

            return fallback;
		}

		void clear() {
			properties.clear();
		}

	private:
        std::vector<ActorProperty<T>> properties;
	};
}