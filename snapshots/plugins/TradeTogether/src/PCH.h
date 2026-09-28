#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Localization.h"
#include "SafeMessageBox.h"

// Route legacy user-facing calls through TradeTogether's English localization
// layer while keeping the trade logic itself unchanged.
#define DebugNotification LocalizedDebugNotification
#define CreateMessage SafeCreateMessage

// CommonLibSSE-NG generates __TradeTogetherPlugin.cpp with "..."sv.
// Keep the literal visible to that generated translation unit on MSVC.
using namespace std::string_view_literals;
