cmake_minimum_required(VERSION 3.24)

project(
    MorphSyncTogether
    VERSION 0.4.2
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CommonLibSSE REQUIRED)

add_commonlibsse_plugin(${PROJECT_NAME}
    SOURCES
        src/PCH.h
        src/main.cpp
        src/Config.cpp
        src/Config.h
        src/UdpTransport.cpp
        src/UdpTransport.h
        src/STRPMCompat.h
        src/SkeeInterfaces.h
        src/AppearanceProbeBodyHair.cpp
        src/AppearanceProbe.h
        src/MorphSyncServiceBodyHair.cpp
        src/MorphSyncService.h
        src/TngSync.cpp
        src/TngSync.h
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
)
