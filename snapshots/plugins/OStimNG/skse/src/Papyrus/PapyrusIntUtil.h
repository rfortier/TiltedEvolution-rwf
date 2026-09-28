#pragma once

namespace PapyrusIntUtil {
    using VM = RE::BSScript::IVirtualMachine;

    std::vector<int> CreateArray(RE::StaticFunctionTag*, int size, int filler) {
        return std::vector<int>(size, filler);
    }

    bool Bind(VM* a_vm) {
        const auto obj = "OIntUtil"sv;

        BIND(CreateArray);

        return true;
    }
}