#include "Util.h"

#define WIN32_LEAN_AND_MEAN

#define NOGDICAPMASKS
#define NOVIRTUALKEYCODES
//#define NOWINMESSAGES
#define NOWINSTYLES
#define NOSYSMETRICS
#define NOMENUS
#define NOICONS
#define NOKEYSTATES
#define NOSYSCOMMANDS
#define NORASTEROPS
#define NOSHOWWINDOW
#define OEMRESOURCE
#define NOATOM
#define NOCLIPBOARD
#define NOCOLOR
//#define NOCTLMGR
#define NODRAWTEXT
#define NOGDI
#define NOKERNEL
//#define NOUSER
#define NONLS
//#define NOMB
#define NOMEMMGR
#define NOMETAFILE
#define NOMINMAX
//#define NOMSG
#define NOOPENFILE
#define NOSCROLL
#define NOSERVICE
#define NOSOUND
#define NOTEXTMETRIC
#define NOWH
#define NOWINOFFSETS
#define NOCOMM
#define NOKANJI
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX

#include <shlobj_core.h>

namespace stl {
    std::vector<std::string> string_split(const std::string& text, char sep) {
        std::vector<std::string> tokens;
        size_t start = 0, end = 0;

        while ((end = text.find(sep, start)) != std::string::npos) {
            tokens.push_back(text.substr(start, end - start));

            start = end + 1;
        }

        if (text.substr(start) != "") tokens.push_back(text.substr(start));

        return tokens;
    }
}  // namespace stl

namespace util {
    std::optional<fs::path> documents_path() {
        wchar_t* buffer{nullptr};
        const auto result = ::SHGetKnownFolderPath(::FOLDERID_Documents, 0, nullptr, std::addressof(buffer));
        std::unique_ptr<wchar_t[], decltype(&::CoTaskMemFree)> knownPath(buffer, ::CoTaskMemFree);

        if (!knownPath || result != S_OK) {
            logger::error("failed to get known folder path"sv);
            return std::nullopt;
        }

        fs::path path = knownPath.get();
        REL::Version::value_type patch = REL::Module::get().version().patch();
        if (patch == 659 || patch == 1179) {
            path /= "My Games/Skyrim Special Edition GOG"sv;
        } else {
            path /= "My Games/Skyrim Special Edition"sv;
        }

        return path;
    }

    std::optional<fs::path> ostim_path() {
        auto path = documents_path();
        if (!path) return std::nullopt;

        *path /= "OStim/"sv;

        return path;
    }

    std::optional<fs::path> settings_path() {
        auto path = ostim_path();
        if (!path) {
            return std::nullopt;
        }

        *path /= "mcmsettings.json"sv;
        return path;
    }

    std::optional<fs::path> settings_path_legacy() {
        auto path = documents_path();
        if (!path) return std::nullopt;

        *path /= "JCUser/OstimMCMSettings.json"sv;

        return path;
    }

    std::optional<fs::path> alignment_path() {
        auto path = ostim_path();
        if (!path) {
            return std::nullopt;
        }

        *path /= "alignment.json"sv;
        return path;
    }

    std::optional<fs::path> ui_settings_path() {
        auto path = ostim_path();
        if (!path) {
            return std::nullopt;
        }
        *path /= "ui_settings.json"sv;
        return path;
    }

    std::optional<fs::path> config_path() {
        auto path = ostim_path();
        if (!path) {
            return std::nullopt;
        }
        *path /= "config.json"sv;
        return path;
    }

    bool within_radius(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_obj, float a_radius) {
        if (a_radius <= 0.0f) return false;

        float tempx = std::abs(a_origin.x - a_obj.x);
        float tempy = std::abs(a_origin.y - a_obj.y);
        float tempz = std::abs(a_origin.z - a_obj.z);

        if (tempx + tempy + tempz < a_radius) return true;       // very small distances
        if (tempx + tempy + tempz > a_radius / 2) return false;  // very large distances

        tempx = tempx * tempx;
        tempy = tempy * tempy;
        tempz = tempz * tempz;

        float tempd = a_radius * a_radius;
        if (tempx + tempy + tempz < tempd) return true;  // near but within distance

        return false;
    }

    // Derived from: https://github.com/powerof3/PapyrusExtenderSSE
    void iterate_attached_cells(const RE::NiPoint3& a_origin, float a_radius,
                                std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR*)> a_fn) {
        auto TES = RE::TES::GetSingleton();
        if (!TES) return;

        auto cell = TES->interiorCell;
        if (cell) {
            cell->ForEachReferenceInRange(a_origin, a_radius, [&](RE::TESObjectREFR* ref) { return a_fn(ref); });
            return;
        }

        auto gridCellArray = TES->gridCells;
        if (!gridCellArray) return;

        auto gridLength = gridCellArray->length;
        if (gridLength > 0) {
            std::uint32_t x = 0;
            std::uint32_t y = 0;
            float yPlus = a_origin.y + a_radius;
            float yMinus = a_origin.y - a_radius;
            float xPlus = a_origin.x + a_radius;
            float xMinus = a_origin.x - a_radius;

            for (x = 0, y = 0; (x < gridLength && y < gridLength); x++, y++) {
                cell = gridCellArray->GetCell(x, y);
                if (cell && cell->IsAttached()) {
                    auto cellCoords = cell->GetCoordinates();
                    if (!cellCoords) continue;

                    float worldX = cellCoords->worldX;
                    float worldY = cellCoords->worldY;

                    if (worldX < xPlus && (worldX + 4096.0) > xMinus && worldY < yPlus && (worldY + 4096.0) > yMinus) {
                        cell->ForEachReferenceInRange(a_origin, a_radius,
                                                      [&](RE::TESObjectREFR* a_cellRef) { return a_fn(a_cellRef); });
                    }
                }
            }
        }
    }

    uint32_t gamepadMaskToKeyCode(uint32_t mask) {
        switch (mask) {
            case 0x1:
                return 266;
            case 0x2:
                return 267;
            case 0x4:
                return 268;
            case 0x8:
                return 269;
            case 0x10:
                return 270;
            case 0x20:
                return 271;
            case 0x40:
                return 272;
            case 0x80:
                return 273;
            case 0x100:
                return 274;
            case 0x200:
                return 275;
            case 0x1000:
                return 276;
            case 0x2000:
                return 277;
            case 0x4000:
                return 278;
            case 0x8000:
                return 279;
            case 0x9:
                return 280;
            case 0xA:
                return 281;
            default:
                return 282;  // Invalid
        }
    }
}  // namespace util