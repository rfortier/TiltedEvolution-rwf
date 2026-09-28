cmake_minimum_required(VERSION 3.24)

project(
    DAVSyncTogether
    VERSION 0.6.3
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CommonLibSSE CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES
        src/PCH.h
        src/main.cpp
        src/DAVConfigIndex.cpp
        src/DAVConfigIndex.h
        src/DAVProbe.cpp
        src/DAVProbe.h
        src/DAVStateSnapshot.cpp
        src/DAVStateSnapshot.h
        src/FormIdentity.cpp
        src/FormIdentity.h
        src/NetworkProtocol.cpp
        src/NetworkProtocol.h
        src/DAVNetworkService.cpp
        src/DAVNetworkService.h
        src/DAVRemoteApplier.cpp
        src/DAVRemoteApplier.h
        src/STRPMApi.cpp
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive- /W4 /utf-8)
endif()

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        _UNICODE
        UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        DAVST_VERSION_STRING="${PROJECT_VERSION}"
)
