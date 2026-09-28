#pragma once

#include "IDGenerator.h"
#include "MapUtil.h"

namespace Util {
	template<typename T>
	class IDMap {
    public:
        IDMap() : idGenerator{(0)} {}

        int put(T object) {
            int id = idGenerator.get();
            objects[id] = object;
            return id;
        }

        T* get(int id) {
            auto iter = objects.find(id);
            if (iter == objects.end()) {
                return nullptr;
            }
            return &iter->second;
        }

        void remove(int id) {
            if (objects.contains(id)) {
                objects.erase(id);
                idGenerator.free(id);
            }
        }

        void reset() {
            objects.clear();
            idGenerator.reset();
        }

	private:
        std::map<int, T> objects;
        IDGenerator idGenerator;
	};
}