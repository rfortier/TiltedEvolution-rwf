#include "GameObject.h"

namespace GameAPI {
    int GameObject::countMarkers(GameObjectMarkerType type, float minZ, float maxZ) const {
        RE::BSTArray<RE::BSFurnitureMarker> markers = getMarkers();
        if (markers.empty()) {
            return 0;
        }

        int count = 0;
        for (RE::BSFurnitureMarker& marker : markers) {
            if (!isnan(minZ) && marker.offset.z < minZ) {
                continue;
            }
            if (!isnan(maxZ) && marker.offset.z > maxZ) {
                continue;
            }

            switch (type) {
            case GameObjectMarkerType::SIT:
                if (marker.animationType.all(RE::BSFurnitureMarker::AnimationType::kSit)) {
                    count++;
                }
                break;
            case GameObjectMarkerType::SLEEP:
                if (marker.animationType.all(RE::BSFurnitureMarker::AnimationType::kSleep)) {
                    count++;
                }
                break;
            }
        }

        return count;
    }

    GamePosition GameObject::getMarkerOffset() const {
        RE::BSTArray<RE::BSFurnitureMarker> markers = getMarkers();
        if (markers.empty()) {
            return {};
        }

        return {markers.front().offset.x, markers.front().offset.y, markers.front().offset.z, markers.front().heading};
    }

    GameOwnership GameObject::getOwner() const {
        auto xOwnership = form->extraList.GetByType<RE::ExtraOwnership>();
        if (xOwnership && xOwnership->owner) {
            return xOwnership->owner;
        } else {
            return {};
        }
    }


    RE::BSTArray<RE::BSFurnitureMarker> GameObject::getMarkers() const {
        RE::NiAVObject* root = form->Get3D();
        if (!root) {
            return {};
        }

        RE::NiExtraData* extra = root->GetExtraData("FRN");
        if (!extra) {
            return {};
        }

        RE::BSFurnitureMarkerNode* node = netimmerse_cast<RE::BSFurnitureMarkerNode*>(extra);
        if (!node) {
            return {};
        }

        return node->markers;
    }
}