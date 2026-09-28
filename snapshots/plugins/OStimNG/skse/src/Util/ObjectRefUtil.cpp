#include "ObjectRefUtil.h"

#include "MathUtil.h"
#include "StringUtil.h"

namespace ObjectRefUtil {
    int ObjectRefUtil::getMotionType(RE::TESObjectREFR* object) {
        std::int32_t motionType = -1;

        if (!object) {
            return -1;
        }

        const auto root = object->Get3D();
        if (!root) {
            return -1;
        }

        const auto cell = object->GetParentCell();
        const auto world = cell ? cell->GetbhkWorld() : nullptr;

        if (world) {
            RE::BSReadLockGuard locker(world->worldLock);

            RE::BSVisit::TraverseScenegraphCollision(
                root, [&](const RE::bhkNiCollisionObject* a_col) -> RE::BSVisit::BSVisitControl {
                    const auto body = a_col->body.get();
                    const auto hkpRigidBody = body ? static_cast<RE::hkpRigidBody*>(body->referencedObject.get()) : nullptr;
                    if (hkpRigidBody) {
                        motionType = hkpRigidBody->motion.type.underlying();
                    }
                    return motionType != -1 ? RE::BSVisit::BSVisitControl::kStop : RE::BSVisit::BSVisitControl::kContinue;
                });
        }

        return motionType;
    }

    bool ObjectRefUtil::isInBBLS(RE::TESObjectREFR* object) {
        if (!object) {
            return false;
        }

        const auto root = object->Get3D();
        if (!root) {
            return false;
        }

        const auto cell = object->GetParentCell();
        std::string id = cell->GetFormEditorID();
        StringUtil::toLower(&id);
        return id.find("luxurysuite") != std::string::npos;
    }

    void ObjectRefUtil::addItem(RE::TESObjectREFR* object, RE::TESForm* itemToAdd, int count, bool silent) {
        AddItem(nullptr, 0, object, itemToAdd, count, silent);
    }

    void ObjectRefUtil::addItem(RE::TESObjectREFR* object, RE::TESForm* itemToAdd) {
        addItem(object, itemToAdd, 1, true);
    }

    void ObjectRefUtil::removeItem(RE::TESObjectREFR* object, RE::TESForm* itemToRemove, int count, bool silent, RE::TESObjectREFR* otherContainer) {
        RemoveItem(nullptr, 0, object, itemToRemove, count, silent, otherContainer);
    }

    void ObjectRefUtil::removeItem(RE::TESObjectREFR* object, RE::TESForm* itemToRemove) {
        removeItem(object, itemToRemove, 999, true, nullptr);
    }

    void ObjectRefUtil::stopTranslation(RE::TESObjectREFR* object) {
        StopTranslation(nullptr, 0, object);
    }

    void ObjectRefUtil::translateTo(RE::TESObjectREFR* object, float x, float y, float z, float angleX, float angleY, float angleZ, float speed, float maxRotationSpeed) {
        TranslateTo(nullptr, 0, object, x, y, z, angleX, angleY, angleZ, speed, maxRotationSpeed);
    }

    void ObjectRefUtil::translateToRadians(RE::TESObjectREFR* object, float x, float y, float z, float angleX, float angleY, float angleZ, float speed, float maxRotationSpeed) {
        TranslateTo(nullptr, 0, object, x, y, z, MathUtil::toDegrees(angleX), MathUtil::toDegrees(angleY), MathUtil::toDegrees(angleZ), speed, maxRotationSpeed);
    }
}