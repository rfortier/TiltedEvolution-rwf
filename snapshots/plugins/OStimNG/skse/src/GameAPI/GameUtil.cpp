#include "GameUtil.h"

namespace GameAPI {
    namespace GameUtil {
        void sendModEvent(RE::TESForm* sender, std::string eventName, std::string strArg, float numArg) {
            SKSE::ModCallbackEvent modEvent{eventName, strArg, numArg, sender};

            auto modCallbackSource = SKSE::GetModCallbackEventSource();
            modCallbackSource->SendEvent(&modEvent);
        }

        void forEachReferenceInRange(const RE::NiPoint3& center, float radius, std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR*)> callback) {
            auto TES = RE::TES::GetSingleton();
            if (!TES) return;

            auto cell = TES->interiorCell;
            if (cell) {
                cell->ForEachReferenceInRange(center, radius, [&](RE::TESObjectREFR* ref) { return callback(ref); });
                return;
            }

            auto gridCellArray = TES->gridCells;
            if (!gridCellArray) return;

            auto gridLength = gridCellArray->length;
            if (gridLength > 0) {
                std::uint32_t x = 0;
                std::uint32_t y = 0;
                float yPlus = center.y + radius;
                float yMinus = center.y - radius;
                float xPlus = center.x + radius;
                float xMinus = center.x - radius;

                for (x = 0, y = 0; (x < gridLength && y < gridLength); x++, y++) {
                    cell = gridCellArray->GetCell(x, y);
                    if (cell && cell->IsAttached()) {
                        auto cellCoords = cell->GetCoordinates();
                        if (!cellCoords) continue;

                        float worldX = cellCoords->worldX;
                        float worldY = cellCoords->worldY;

                        if (worldX < xPlus && (worldX + 4096.0) > xMinus && worldY < yPlus &&
                            (worldY + 4096.0) > yMinus) {
                            cell->ForEachReferenceInRange(center, radius, [&](RE::TESObjectREFR* a_cellRef) { return callback(a_cellRef); });
                        }
                    }
                }
            }
        }

        void ForEachReferenceInRange(RE::TESObjectREFR* a_origin, float a_radius, std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR* a_ref)> a_callback) {
            RE::TES* TES = RE::TES::GetSingleton();
            if (!TES) {
                return;
            }

            if (a_origin && a_radius > 0.0f) {
                const auto originPos = a_origin->GetPosition();

                if (TES->interiorCell) {
                    TES->interiorCell->ForEachReferenceInRange(originPos, a_radius, [&](RE::TESObjectREFR* a_ref) { return a_callback(a_ref); });
                } else {
                    if (const auto gridLength = TES->gridCells ? TES->gridCells->length : 0; gridLength > 0) {
                        const float yPlus = originPos.y + a_radius;
                        const float yMinus = originPos.y - a_radius;
                        const float xPlus = originPos.x + a_radius;
                        const float xMinus = originPos.x - a_radius;

                        std::uint32_t x = 0;
                        do {
                            std::uint32_t y = 0;
                            do {
                                if (const auto cell = TES->gridCells->GetCell(x, y); cell && cell->IsAttached()) {
                                    if (const auto cellCoords = cell->GetCoordinates(); cellCoords) {
                                        const RE::NiPoint2 worldPos{cellCoords->worldX, cellCoords->worldY};
                                        if (worldPos.x < xPlus && (worldPos.x + 4096.0f) > xMinus &&
                                            worldPos.y < yPlus && (worldPos.y + 4096.0f) > yMinus) {
                                            cell->ForEachReferenceInRange(
                                                originPos, a_radius,
                                                [&](RE::TESObjectREFR* a_ref) { return a_callback(a_ref); });
                                        }
                                    }
                                }
                                ++y;
                            } while (y < gridLength);
                            ++x;
                        } while (x < gridLength);
                    }
                }

                // something else in there still doesn't work
                // don't get it, don't care, we don't actually need that part anyways
                /*
                RE::TESWorldSpace* worldSpace = &TES->worldSpace;
                if (const auto skyCell = worldSpace ? worldSpace->GetSkyCell() : nullptr; skyCell) {
                    skyCell->ForEachReferenceInRange(originPos, a_radius, [&](RE::TESObjectREFR& a_ref) { return a_callback(a_ref); });
                }
                */
            } else {
                // also causes CTD + we don't need this for now
                //TES->ForEachReference([&](RE::TESObjectREFR& a_ref) { return a_callback(a_ref); });
            }
        }
    }
}