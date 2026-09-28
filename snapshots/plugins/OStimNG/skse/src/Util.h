#pragma once

namespace stl {
    std::vector<std::string> string_split(const std::string& text, char sep);
}

namespace util {
    std::optional<fs::path> documents_path();
    std::optional<fs::path> ostim_path();
    std::optional<fs::path> settings_path();
    std::optional<fs::path> settings_path_legacy();
    std::optional<fs::path> alignment_path();
    std::optional<fs::path> ui_settings_path();
    std::optional<fs::path> config_path();

    bool within_radius(const RE::NiPoint3& a_origin, const RE::NiPoint3& a_obj, float a_radius);

    void iterate_attached_cells(const RE::NiPoint3& a_origin, float a_radius,
                                std::function<RE::BSContainer::ForEachResult(RE::TESObjectREFR*)> a_fn);

    uint32_t gamepadMaskToKeyCode(uint32_t mask);
}  // namespace util