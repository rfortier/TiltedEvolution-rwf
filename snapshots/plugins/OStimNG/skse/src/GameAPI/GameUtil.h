#pragma once

namespace GameAPI {
    namespace GameUtil {
        void sendModEvent(RE::TESForm* sender, std::string eventName, std::string strArg, float numArg);

        void forEachReferenceInRange(const RE::NiPoint3& center, float radius, std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR*)> callback);

        // bandaid function until CLib-NG is updated
        void ForEachReferenceInRange(RE::TESObjectREFR* a_origin, float a_radius, std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR* a_ref)> a_callback);
    }
}