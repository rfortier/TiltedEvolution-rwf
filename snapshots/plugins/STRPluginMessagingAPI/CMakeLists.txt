cmake_minimum_required(VERSION 3.21)

project(
    STRPluginMessagingAPI
    VERSION 0.9.3
    LANGUAGES CXX
)

option(
    STRPM_ENABLE_UDP_BACKEND
    "Build the legacy standalone UDP backend"
    OFF
)

option(
    STRPM_BUILD_STR180_BRIDGE_PROBE
    "Build the Skyrim Together Reborn 1.8.0 chat bridge"
    ON
)

add_library(STRPluginMessagingAPIClient STATIC
    src/STRPluginMessagingAPI.cpp
)

target_compile_features(STRPluginMessagingAPIClient PUBLIC cxx_std_20)

target_include_directories(STRPluginMessagingAPIClient
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_target_properties(STRPluginMessagingAPIClient
    PROPERTIES
        OUTPUT_NAME STRPluginMessagingAPIClient
)

add_library(STRPluginMessagingAPIRuntime SHARED
    src/STRPluginMessagingAPIRuntime.cpp
    src/STRPluginMessagingAPISKSEMetadata.cpp
)

target_compile_features(STRPluginMessagingAPIRuntime PUBLIC cxx_std_20)

target_include_directories(STRPluginMessagingAPIRuntime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(STRPM_ENABLE_UDP_BACKEND)
    target_compile_definitions(STRPluginMessagingAPIRuntime
        PRIVATE
            STRPM_ENABLE_UDP_BACKEND=1
    )
else()
    target_compile_definitions(STRPluginMessagingAPIRuntime
        PRIVATE
            STRPM_ENABLE_UDP_BACKEND=0
    )
endif()

target_link_libraries(STRPluginMessagingAPIRuntime
    PRIVATE
        Ws2_32
        Bcrypt
)

set_target_properties(STRPluginMessagingAPIRuntime
    PROPERTIES
        OUTPUT_NAME STRPluginMessagingAPI
)

if(STRPM_BUILD_STR180_BRIDGE_PROBE)
    set(STRPM_BRIDGE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/STRPluginMessagingBridge.cpp")

    # STR 1.8.0 unpacks/remaps private runtime regions while Skyrim is starting.
    # Never scan those mappings in-place. For MSVC builds, generate a resolver
    # variant that snapshots memory through ReadProcessMemory in bounded chunks
    # and parses only local bytes.
    if(MSVC)
        file(READ "${STRPM_BRIDGE_SOURCE}" STRPM_BRIDGE_SOURCE_TEXT)

        # The official v1.8.0 OverlayClient source logs the chat anchor with a
        # wide literal (L"..."). v0.4.8 incorrectly searched the narrow ASCII
        # bytes, which can never match the public runtime. Generate the probe with
        # the exact UTF-16 anchor and exclude the wchar_t terminator correctly.
        set(STRPM_NARROW_CHAT_ANCHOR [=[    constexpr char kSendChatAnchor[] = "Send chat message of type {}: '{}' ";]=])
        set(STRPM_WIDE_CHAT_ANCHOR [=[    constexpr wchar_t kSendChatAnchor[] = L"Send chat message of type {}: '{}' ";]=])
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_NARROW_CHAT_ANCHOR}" STRPM_CHAT_ANCHOR_DECL_POS)
        if(STRPM_CHAT_ANCHOR_DECL_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate narrow STR chat anchor declaration")
        endif()
        string(REPLACE
            "${STRPM_NARROW_CHAT_ANCHOR}"
            "${STRPM_WIDE_CHAT_ANCHOR}"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        set(STRPM_NARROW_NEEDLE_SIZE "        constexpr std::size_t needleSize = sizeof(kSendChatAnchor) - 1;")
        set(STRPM_WIDE_NEEDLE_SIZE "        constexpr std::size_t needleSize = sizeof(kSendChatAnchor) - sizeof(kSendChatAnchor[0]);")
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_NARROW_NEEDLE_SIZE}" STRPM_NEEDLE_SIZE_POS)
        if(STRPM_NEEDLE_SIZE_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate STR chat anchor needle size")
        endif()
        string(REPLACE
            "${STRPM_NARROW_NEEDLE_SIZE}"
            "${STRPM_WIDE_NEEDLE_SIZE}"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        set(STRPM_FIND_ANCHOR_MARKER "    void FindAnchorCopies()")
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_FIND_ANCHOR_MARKER}" STRPM_FIND_ANCHOR_MARKER_POS)
        if(STRPM_FIND_ANCHOR_MARKER_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate FindAnchorCopies in STRPluginMessagingBridge.cpp")
        endif()

        set(STRPM_SAFE_SNAPSHOT_HELPERS [=[    bool SnapshotProcessMemory(
        std::uintptr_t address,
        std::size_t size,
        std::vector<std::uint8_t>& snapshot) noexcept
    {
        constexpr std::size_t kMaxResolverSnapshotBytes = 2u * 1024u * 1024u;
        snapshot.clear();
        if (address == 0 || size == 0 || size > kMaxResolverSnapshotBytes)
            return false;

        try
        {
            snapshot.resize(size);
        }
        catch (...)
        {
            snapshot.clear();
            return false;
        }

        SIZE_T bytesRead = 0;
        const BOOL ok = ReadProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<const void*>(address),
            snapshot.data(),
            size,
            &bytesRead);
        if (!ok || bytesRead != size)
        {
            snapshot.clear();
            return false;
        }
        return true;
    }

    int CompareSnapshotBytes(
        const void* left,
        const void* right,
        std::size_t count) noexcept
    {
        if (count == 0)
            return 0;
        if (!left || !right)
            return 1;

        const auto* lhs = static_cast<const std::uint8_t*>(left);
        const auto* rhs = static_cast<const std::uint8_t*>(right);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (lhs[i] != rhs[i])
                return lhs[i] < rhs[i] ? -1 : 1;
        }
        return 0;
    }

    void FindAnchorCopies()]=])

        string(REPLACE
            "${STRPM_FIND_ANCHOR_MARKER}"
            "${STRPM_SAFE_SNAPSHOT_HELPERS}"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        # FindAnchorCopies: scan readable spans in 1 MiB chunks with enough
        # overlap to catch the UTF-16 anchor crossing a chunk boundary.
        set(STRPM_ANCHOR_DIRECT_READ "            const auto* bytes = reinterpret_cast<const std::uint8_t*>(span.base);\n            for (std::size_t i = 0; i + needleSize <= span.size; ++i)\n            {\n                if (std::memcmp(bytes + i, needle, needleSize) == 0)\n                    g_anchorAddresses.push_back(span.base + i);\n            }")
        set(STRPM_ANCHOR_SNAPSHOT_READ [=[            constexpr std::size_t kChunkBytes = 1u * 1024u * 1024u;
            constexpr std::size_t kOverlapBytes = needleSize - 1;
            for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
            {
                const auto remaining = span.size - offset;
                const auto payloadBytes = std::min(kChunkBytes, remaining);
                const auto readBytes = std::min(remaining, payloadBytes + kOverlapBytes);

                std::vector<std::uint8_t> snapshot;
                if (!SnapshotProcessMemory(span.base + offset, readBytes, snapshot))
                    continue;

                for (std::size_t i = 0;
                     i < payloadBytes && i + needleSize <= snapshot.size();
                     ++i)
                {
                    if (CompareSnapshotBytes(snapshot.data() + i, needle, needleSize) == 0)
                        g_anchorAddresses.push_back(span.base + offset + i);
                }
            }]=])
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_ANCHOR_DIRECT_READ}" STRPM_ANCHOR_DIRECT_READ_POS)
        if(STRPM_ANCHOR_DIRECT_READ_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate anchor direct-read scan")
        endif()
        string(REPLACE "${STRPM_ANCHOR_DIRECT_READ}" "${STRPM_ANCHOR_SNAPSHOT_READ}" STRPM_BRIDGE_SOURCE_TEXT "${STRPM_BRIDGE_SOURCE_TEXT}")

        # FindRipRelativeXrefs: parse executable spans from 1 MiB local chunks,
        # overlapping by six bytes for the seven-byte RIP-relative instruction.
        set(STRPM_XREF_DIRECT_READ "            const auto* code = reinterpret_cast<const std::uint8_t*>(span.base);\n            for (std::size_t i = 0; i + 7 <= span.size; ++i)\n            {\n                if ((code[i] & 0xF8) != 0x48 || code[i + 1] != 0x8D)\n                    continue;\n\n                const std::uint8_t modrm = code[i + 2];\n                if ((modrm & 0xC7) != 0x05)\n                    continue;\n\n                std::int32_t displacement = 0;\n                std::memcpy(&displacement, code + i + 3, sizeof(displacement));\n                const auto instruction = span.base + i;\n                const auto target = instruction + 7 + static_cast<std::intptr_t>(displacement);\n                if (std::find(g_anchorAddresses.begin(), g_anchorAddresses.end(), target) != g_anchorAddresses.end())\n                    g_xrefs.push_back(instruction);\n            }")
        set(STRPM_XREF_SNAPSHOT_READ [=[            constexpr std::size_t kChunkBytes = 1u * 1024u * 1024u;
            constexpr std::size_t kInstructionBytes = 7;
            constexpr std::size_t kOverlapBytes = kInstructionBytes - 1;
            for (std::size_t offset = 0; offset < span.size; offset += kChunkBytes)
            {
                const auto remaining = span.size - offset;
                const auto payloadBytes = std::min(kChunkBytes, remaining);
                const auto readBytes = std::min(remaining, payloadBytes + kOverlapBytes);

                std::vector<std::uint8_t> snapshot;
                if (!SnapshotProcessMemory(span.base + offset, readBytes, snapshot))
                    continue;

                const auto* code = snapshot.data();
                for (std::size_t i = 0;
                     i < payloadBytes && i + kInstructionBytes <= snapshot.size();
                     ++i)
                {
                    if ((code[i] & 0xF8) != 0x48 || code[i + 1] != 0x8D)
                        continue;

                    const std::uint8_t modrm = code[i + 2];
                    if ((modrm & 0xC7) != 0x05)
                        continue;

                    std::int32_t displacement = 0;
                    std::memcpy(&displacement, code + i + 3, sizeof(displacement));
                    const auto instruction = span.base + offset + i;
                    const auto target = instruction + 7 + static_cast<std::intptr_t>(displacement);
                    if (std::find(g_anchorAddresses.begin(), g_anchorAddresses.end(), target) != g_anchorAddresses.end())
                        g_xrefs.push_back(instruction);
                }
            }]=])
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_XREF_DIRECT_READ}" STRPM_XREF_DIRECT_READ_POS)
        if(STRPM_XREF_DIRECT_READ_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate xref direct-read scan")
        endif()
        string(REPLACE "${STRPM_XREF_DIRECT_READ}" "${STRPM_XREF_SNAPSHOT_READ}" STRPM_BRIDGE_SOURCE_TEXT "${STRPM_BRIDGE_SOURCE_TEXT}")

        # EnumerateDirectCalls: resolved functions are normally small. Snapshot
        # them once and parse only local bytes.
        set(STRPM_CALL_DIRECT_READ "        const auto* code = reinterpret_cast<const std::uint8_t*>(bounds.begin);\n        const auto size = bounds.end - bounds.begin;")
        set(STRPM_CALL_SNAPSHOT_READ "        const auto size = bounds.end - bounds.begin;\n        std::vector<std::uint8_t> snapshot;\n        if (!SnapshotProcessMemory(bounds.begin, size, snapshot))\n            return result;\n        const auto* code = snapshot.data();")
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_CALL_DIRECT_READ}" STRPM_CALL_DIRECT_READ_POS)
        if(STRPM_CALL_DIRECT_READ_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate direct-call memory scan")
        endif()
        string(REPLACE "${STRPM_CALL_DIRECT_READ}" "${STRPM_CALL_SNAPSHOT_READ}" STRPM_BRIDGE_SOURCE_TEXT "${STRPM_BRIDGE_SOURCE_TEXT}")

        # FunctionContainsImmediate32 already limits functions to 0x4000 bytes.
        set(STRPM_IMMEDIATE_DIRECT_READ "        const auto* bytes = reinterpret_cast<const std::uint8_t*>(bounds.begin);\n        std::uint8_t needle[sizeof(value)]{};")
        set(STRPM_IMMEDIATE_SNAPSHOT_READ "        std::vector<std::uint8_t> snapshot;\n        if (!SnapshotProcessMemory(bounds.begin, size, snapshot))\n            return false;\n        const auto* bytes = snapshot.data();\n        std::uint8_t needle[sizeof(value)]{};")
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_IMMEDIATE_DIRECT_READ}" STRPM_IMMEDIATE_DIRECT_READ_POS)
        if(STRPM_IMMEDIATE_DIRECT_READ_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate immediate-value memory scan")
        endif()
        string(REPLACE "${STRPM_IMMEDIATE_DIRECT_READ}" "${STRPM_IMMEDIATE_SNAPSHOT_READ}" STRPM_BRIDGE_SOURCE_TEXT "${STRPM_BRIDGE_SOURCE_TEXT}")

        # Remaining memcmp calls now operate exclusively on local snapshots.
        string(REPLACE "std::memcmp" "CompareSnapshotBytes" STRPM_BRIDGE_SOURCE_TEXT "${STRPM_BRIDGE_SOURCE_TEXT}")

        # Add explicit resolver-pass diagnostics. The first log is emitted before
        # any process-memory scan so a silent/stuck pass is immediately visible.
        set(STRPM_RESOLVER_CALL "if (ResolveProcessChatMessage())")
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_RESOLVER_CALL}" STRPM_RESOLVER_CALL_POS)
        if(STRPM_RESOLVER_CALL_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate resolver call in STRPluginMessagingBridge.cpp")
        endif()
        string(REPLACE
            "${STRPM_RESOLVER_CALL}"
            "Log(\"send resolver pass starting\");\n                const auto resolverStartedAt = GetTickCount64();\n                if (ResolveProcessChatMessage())"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        set(STRPM_RESOLVER_SUCCESS_MARKER "                    g_sendResolverReady.store(true);")
        string(REPLACE
            "${STRPM_RESOLVER_SUCCESS_MARKER}"
            "                    Log(\"send resolver pass completed in %llu ms\", static_cast<unsigned long long>(GetTickCount64() - resolverStartedAt));\n                    g_sendResolverReady.store(true);"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        # Retain the v0.4.8 periodic diagnostic snapshots so the next runtime test
        # shows whether the UTF-16 anchor proceeds through xrefs/call candidates.
        set(STRPM_BOOTSTRAP_DIAG_VAR_MARKER "        bool loggedReceiveFailure = false;")
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_BOOTSTRAP_DIAG_VAR_MARKER}" STRPM_BOOTSTRAP_DIAG_VAR_POS)
        if(STRPM_BOOTSTRAP_DIAG_VAR_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate BootstrapLoop diagnostic variables")
        endif()
        string(REPLACE
            "${STRPM_BOOTSTRAP_DIAG_VAR_MARKER}"
            "        bool loggedReceiveFailure = false;\n        ULONGLONG lastResolverDiagnosticAt = 0;"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        set(STRPM_FAILURE_BLOCK [=[                else if (!loggedSendFailure)
                {
                    Log("send resolver waiting for mapped STR 1.8.0 runtime");
                    loggedSendFailure = true;
                }]=])
        set(STRPM_DIAGNOSTIC_FAILURE_BLOCK [=[                else
                {
                    const auto resolverFailedAt = GetTickCount64();
                    const bool shouldLogResolverDiagnostic =
                        !loggedSendFailure ||
                        lastResolverDiagnosticAt == 0 ||
                        resolverFailedAt - lastResolverDiagnosticAt >= 5000;

                    if (shouldLogResolverDiagnostic)
                    {
                        Log("send resolver diagnostic snapshot after failed pass:");
                        LogResolverDetails();
                        Log("send resolver pass failed in %llu ms; waiting for mapped STR 1.8.0 runtime",
                            static_cast<unsigned long long>(resolverFailedAt - resolverStartedAt));
                        lastResolverDiagnosticAt = resolverFailedAt;
                    }
                    loggedSendFailure = true;
                }]=])
        string(FIND "${STRPM_BRIDGE_SOURCE_TEXT}" "${STRPM_FAILURE_BLOCK}" STRPM_FAILURE_BLOCK_POS)
        if(STRPM_FAILURE_BLOCK_POS EQUAL -1)
            message(FATAL_ERROR "Unable to locate resolver failure block")
        endif()
        string(REPLACE
            "${STRPM_FAILURE_BLOCK}"
            "${STRPM_DIAGNOSTIC_FAILURE_BLOCK}"
            STRPM_BRIDGE_SOURCE_TEXT
            "${STRPM_BRIDGE_SOURCE_TEXT}"
        )

        set(STRPM_GENERATED_BRIDGE "${CMAKE_CURRENT_BINARY_DIR}/STRPluginMessagingBridge.guarded.cpp")
        file(WRITE "${STRPM_GENERATED_BRIDGE}" "${STRPM_BRIDGE_SOURCE_TEXT}")
        set(STRPM_BRIDGE_SOURCE "${STRPM_GENERATED_BRIDGE}")
    endif()

    add_library(STRPluginMessagingBridge SHARED
        ${STRPM_BRIDGE_SOURCE}
        src/STRPluginMessagingBridgeReceive.cpp
        src/STRPluginMessagingBridgeSKSE.cpp
    )
    target_compile_features(STRPluginMessagingBridge PUBLIC cxx_std_20)
    target_compile_definitions(STRPluginMessagingBridge
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
    target_include_directories(STRPluginMessagingBridge
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(STRPluginMessagingBridge
        PRIVATE
            Shell32
            Ole32
    )
    set_target_properties(STRPluginMessagingBridge
        PROPERTIES
            OUTPUT_NAME STRPluginMessagingBridge
    )
endif()

# Retained regression client from v0.5.1. It still builds in CI so public-API E2E
# test code remains compile-checked, but normal release packages omit this DLL.
add_library(STRPluginMessagingDiagnostic SHARED
    src/STRPluginMessagingDiagnosticSKSE.cpp
)
target_compile_features(STRPluginMessagingDiagnostic PUBLIC cxx_std_20)
target_include_directories(STRPluginMessagingDiagnostic
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(STRPluginMessagingDiagnostic
    PRIVATE
        STRPluginMessagingAPIClient
        Shell32
        Ole32
)
set_target_properties(STRPluginMessagingDiagnostic
    PROPERTIES
        OUTPUT_NAME STRPluginMessagingDiagnostic
)

if(MSVC)
    target_compile_options(STRPluginMessagingAPIClient PRIVATE /W4 /permissive- /EHsc)
    target_compile_options(STRPluginMessagingAPIRuntime
        PRIVATE
            /W4
            /permissive-
            /EHsc
            /FI${CMAKE_CURRENT_SOURCE_DIR}/src/STRPMRuntimeLoadRedirect.h
    )
    target_compile_options(STRPluginMessagingDiagnostic
        PRIVATE
            /W4
            /permissive-
            /EHsc
            /FI${CMAKE_CURRENT_SOURCE_DIR}/src/STRPMDiagnosticLogRedirect.h
    )
    if(STRPM_BUILD_STR180_BRIDGE_PROBE)
        target_compile_options(STRPluginMessagingBridge
            PRIVATE
                /W4
                /permissive-
                /EHsc
                /FI${CMAKE_CURRENT_SOURCE_DIR}/src/STRPMLogRedirect.h
        )
    endif()
else()
    target_compile_options(STRPluginMessagingAPIClient PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(STRPluginMessagingAPIRuntime PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(STRPluginMessagingDiagnostic PRIVATE -Wall -Wextra -Wpedantic)
    if(STRPM_BUILD_STR180_BRIDGE_PROBE)
        target_compile_options(STRPluginMessagingBridge PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

add_executable(STRPluginMessagingAPIExample
    examples/ClientUsageExample.cpp
)

target_link_libraries(STRPluginMessagingAPIExample
    PRIVATE
        STRPluginMessagingAPIClient
)