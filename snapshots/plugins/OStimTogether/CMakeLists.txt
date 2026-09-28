cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OST_VERSION)
string(STRIP "${OST_VERSION}" OST_VERSION)

project(
    OStimTogether
    VERSION ${OST_VERSION}
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CommonLibSSE REQUIRED)

# OStim Standalone 7.5c reports OStim.dll 7.5.0.3. The concrete GraphActor
# prefix used by OStim Together's read-only graph probe is unchanged from
# 7.5b, but v0.37.5 intentionally rejected unknown DLL builds. Generate a
# build-local bridge copy that admits the verified 7.5c runtime without
# rewriting the large historical source file through remote tooling.
set(OSTIM_BRIDGE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/OStimBridge.cpp")
file(READ "${OSTIM_BRIDGE_SOURCE}" OSTIM_BRIDGE_COMPAT)

set(_ostim75c_guard "if (version.Is(7, 5, 0, 2)) {")
string(FIND "${OSTIM_BRIDGE_COMPAT}" "${_ostim75c_guard}" _ostim75c_guard_pos)
if(_ostim75c_guard_pos EQUAL -1)
    message(FATAL_ERROR "OStimBridge.cpp no longer contains the expected 7.5b runtime guard; revalidate OStim 7.5c compatibility before building.")
endif()
string(REPLACE
    "if (version.Is(7, 5, 0, 2)) {"
    "if (version.Is(7, 5, 0, 2) ||\n                version.Is(7, 5, 0, 3)) {"
    OSTIM_BRIDGE_COMPAT
    "${OSTIM_BRIDGE_COMPAT}")

set(_ostim75c_assert
"SelectGraphLayout({ 7, 5, 0, 3 }) ==\n            OStimInternalProbe::GraphLayout::Unsupported")
string(FIND "${OSTIM_BRIDGE_COMPAT}" "${_ostim75c_assert}" _ostim75c_assert_pos)
if(_ostim75c_assert_pos EQUAL -1)
    message(FATAL_ERROR "OStimBridge.cpp no longer contains the expected 7.5c rejection assertion.")
endif()
string(REPLACE
    "SelectGraphLayout({ 7, 5, 0, 3 }) ==\n            OStimInternalProbe::GraphLayout::Unsupported"
    "SelectGraphLayout({ 7, 5, 0, 3 }) ==\n            OStimInternalProbe::GraphLayout::OStim75b"
    OSTIM_BRIDGE_COMPAT
    "${OSTIM_BRIDGE_COMPAT}")

string(REPLACE
    "return \"OStim-7.5b\";"
    "return \"OStim-7.5b/c\";"
    OSTIM_BRIDGE_COMPAT
    "${OSTIM_BRIDGE_COMPAT}")

string(REPLACE
    "supported versions are 7.4.0.3 (7.4c) and 7.5.0.2 (7.5b)"
    "supported versions are 7.4.0.3 (7.4c), 7.5.0.2 (7.5b), and 7.5.0.3 (7.5c)"
    OSTIM_BRIDGE_COMPAT
    "${OSTIM_BRIDGE_COMPAT}")

string(REPLACE
    "OStim 7.5b requires Threads ABI 3"
    "OStim 7.5b/7.5c requires Threads ABI 3"
    OSTIM_BRIDGE_COMPAT
    "${OSTIM_BRIDGE_COMPAT}")

# OStim 7.5c's ThreadInterface::getVersion() returns the packed plugin
# version (0x07050003), not the historical ABI ordinal. Normalize that token
# to the interface ABI guaranteed by each explicitly supported runtime so
# SupportsThreadFurniture() and the ABI guards retain their intended meaning.
set(_thread_version_block
"        _threadInterfaceVersion =\n            _threads->getVersion();\n\n        SKSE::log::info(\n            \"OStim Threads interface version={}\",\n            _threadInterfaceVersion);")
string(FIND "${OSTIM_BRIDGE_COMPAT}" "${_thread_version_block}" _thread_version_block_pos)
if(_thread_version_block_pos EQUAL -1)
    message(FATAL_ERROR "OStimBridge.cpp no longer contains the expected Threads version block; revalidate version normalization before building.")
endif()
string(REPLACE
    "        _threadInterfaceVersion =\n            _threads->getVersion();\n\n        SKSE::log::info(\n            \"OStim Threads interface version={}\",\n            _threadInterfaceVersion);"
    "        const auto reportedThreadVersion =\n            _threads->getVersion();\n\n        _threadInterfaceVersion =\n            reportedThreadVersion;\n\n        if (reportedThreadVersion > 0xFFU) {\n            if (_graphLayout ==\n                OStimInternalProbe::GraphLayout::OStim75b) {\n                _threadInterfaceVersion = 3;\n            } else if (_graphLayout ==\n                       OStimInternalProbe::GraphLayout::OStim74c) {\n                _threadInterfaceVersion = 2;\n            }\n        }\n\n        SKSE::log::info(\n            \"OStim Threads interface reported={} normalizedABI={}\",\n            reportedThreadVersion,\n            _threadInterfaceVersion);"
    OSTIM_BRIDGE_COMPAT
    "${OSTIM_BRIDGE_COMPAT}")

set(OSTIM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${OSTIM_GENERATED_DIR}")
set(OSTIM_BRIDGE_GENERATED "${OSTIM_GENERATED_DIR}/OStimBridge.cpp")
file(WRITE "${OSTIM_BRIDGE_GENERATED}" "${OSTIM_BRIDGE_COMPAT}")

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES
        src/PCH.h
        src/main.cpp
        src/AddonBridge.cpp
        src/AddonBridge.h
        src/AddonStateRepair.cpp
        src/AddonStateRepair.h
        src/AddActorConsentGate.cpp
        src/CoopSessionManager.cpp
        src/CoopSessionManager.h
        src/OCumStateSync.cpp
        src/OCumStateSync.h
        src/OCumOverlayVisibility.cpp
        src/OCumOverlayVisibility.h
        src/AccuratePenetrationAPI.h
        src/PPAIntegration.cpp
        src/PPAAbsoluteHooks.cpp
        src/PPAIntegration.h
        src/PapyrusConsentBridge.cpp
        src/PapyrusConsentBridge.h
        src/PreflightGuard.cpp
        src/PreflightGuard.h
        src/ProxyOnlyThreadCleanup.cpp
        src/ProxyOnlyThreadCleanup.h
        src/SafeMessageBox.cpp
        src/SafeMessageBox.h
        src/Config.cpp
        src/Config.h
        src/EquipmentLock.cpp
        src/EquipmentLock.h
        src/DefaultOutfitGuard.cpp
        src/DefaultOutfitGuard.h
        src/NetworkProbe.cpp
        src/NetworkProbe.h
        src/STRPMApi.h
        src/STRPMTransport.cpp
        src/STRPMTransport.h
        src/ActorResolverSTRPM.cpp
        src/ActorResolver.h
        src/VisualKeepAlive.cpp
        src/VisualKeepAlive.h
        src/PapyrusAnimationBridge.cpp
        src/PapyrusAnimationBridge.h
        src/MirrorUndressRepair.cpp
        src/MirrorUndressRepair.h
        src/Input.cpp
        src/Input.h
        src/FreeSceneAlignmentFix.cpp
        src/FreeSceneAlignmentFix.h
        src/FreeScenePhaseSync.cpp
        src/FreeScenePhaseSync.h
        src/ParticipantAlignmentSync.cpp
        src/ParticipantAlignmentSync.h
        src/FreeSceneSelfOriginLock.cpp
        src/FreeSceneSelfOriginLock.h
        src/FreeSceneRootSync.cpp
        src/FreeSceneRootSync.h
        src/SharedSceneControl.cpp
        src/SharedSceneControl.h
        ${OSTIM_BRIDGE_GENERATED}
        src/OStimBridge.h
        src/RaceMenuOverlayBridge.cpp
        src/RaceMenuOverlayBridge.h
        src/SceneCenter.h
        src/OStimInternalGraphProbe.h
        src/OStimAPI/PluginInterface.h
        src/OStimAPI/ThreadActor.h
        src/OStimAPI/Thread.h
        src/OStimAPI/ThreadEventListener.h
        src/OStimAPI/ThreadInterface.h
        src/OStimAPI/InterfaceMap.h
        src/OStimAPI/InterfaceExchangeMessage.h
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_precompile_headers(${PROJECT_NAME}
    PRIVATE
        src/PCH.h
)

target_compile_options(${PROJECT_NAME}
    PRIVATE
        /utf-8
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        OSTIM_TOGETHER_VERSION="${OST_VERSION}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE version)
