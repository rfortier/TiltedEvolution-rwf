#pragma once

#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <queue>
#include <random>

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <REL/Relocation.h>

#include <ShlObj_core.h>
#include <Windows.h>
#include <Psapi.h>
#undef cdecl // Workaround for Clang 14 CMake configure error.

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include "pugixml.hpp"
#include "nlohmann/json.hpp"

using namespace std::literals;
using namespace REL::literals;

namespace logger = SKSE::log;
namespace fs = std::filesystem;

using json = nlohmann::json;

namespace util {
    using SKSE::stl::report_and_fail;
}

namespace stld {
    using namespace SKSE::stl;

    template <class T>
    void asm_replace(std::uintptr_t a_from) {
        asm_replace(a_from, T::size, reinterpret_cast<std::uintptr_t>(T::func));
    }

    template <class T>
    void write_thunk_call(std::uintptr_t a_src) {
        auto& trampoline = SKSE::GetTrampoline();

        T::func = trampoline.write_call<5>(a_src, T::thunk);
    }

    template <class F, size_t offset, class T>
    void write_vfunc() {
        REL::Relocation<std::uintptr_t> vtbl{F::VTABLE[offset]};
        T::func = vtbl.write_vfunc(T::idx, T::thunk);
    }

    template <class F, class T>
    void write_vfunc() {
        write_vfunc<F, 0, T>();
    }

    inline std::string as_string(std::string_view a_view) { return {a_view.data(), a_view.size()}; }
}  // namespace stl
