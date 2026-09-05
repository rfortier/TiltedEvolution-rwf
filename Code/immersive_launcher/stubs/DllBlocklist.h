#pragma once

#include <string>

namespace stubs
{
// Returns true if the module name is found in the hard block list.
bool IsDllBlocked(std::wstring_view dllName);

// Returns true if the module is a Skyrim Souls RE related DLL.
bool IsSoulsRE(std::wstring_view dllName);

// Global flag indicating whether Skyrim Souls RE is active.
// inline: this header is included by several translation units in both the
// launcher and the client; an out-of-line definition here duplicated the
// symbol in every TU (LNK2005 once /FORCE:MULTIPLE went away). inline also
// gives all users the one shared flag, which is the intended semantics.
inline bool g_IsSoulsREActive{};
} // namespace stubs
