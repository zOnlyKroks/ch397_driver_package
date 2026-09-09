cmake_minimum_required(VERSION 3.20)
project(ch397svc LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "ch397svc only builds for Windows (x64/ARM64)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# wintun.h is vendored separately; see third_party/wintun/README.md.
# Configure with -DWINTUN_INCLUDE_DIR=<path> if it lives elsewhere.
set(WINTUN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/wintun" CACHE PATH
    "Directory containing wintun.h")

if(NOT EXISTS "${WINTUN_INCLUDE_DIR}/wintun.h")
    message(FATAL_ERROR
        "wintun.h not found in ${WINTUN_INCLUDE_DIR}. "
        "Download it from https://www.wintun.net and place it there, "
        "or pass -DWINTUN_INCLUDE_DIR=<path>. See third_party/wintun/README.md.")
endif()

add_executable(ch397svc
    src/service_main.cpp
    src/device_watch.cpp
    src/usb_transport.cpp
    src/netif_bridge.cpp
)

target_include_directories(ch397svc PRIVATE src ${WINTUN_INCLUDE_DIR})

target_link_libraries(ch397svc PRIVATE
    winusb.lib
    setupapi.lib
    advapi32.lib
    user32.lib
)

target_compile_definitions(ch397svc PRIVATE UNICODE _UNICODE)

# CMAKE_VS_PLATFORM_NAME / generator platform selects x64 vs ARM64 when
# using the Visual Studio generator, e.g.:
#   cmake -G "Visual Studio 17 2022" -A ARM64 -B build-arm64
#   cmake -G "Visual Studio 17 2022" -A x64   -B build-x64
