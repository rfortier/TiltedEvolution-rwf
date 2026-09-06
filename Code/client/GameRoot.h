#pragma once

#include <filesystem>

// Game root as determined during client init. Set by RunTiltedInit in both
// launch flows: the SkyrimTogether.exe launcher passes the user-selected
// install dir, the SKSE bootstrap passes the running exe's directory.
const std::filesystem::path& GetGameRoot();
