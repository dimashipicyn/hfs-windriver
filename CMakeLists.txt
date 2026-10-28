cmake_minimum_required(VERSION 3.21)
project(hfsplus_windriver LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED OFF)

option(ENABLE_ASAN "Enable AddressSanitizer and UBSan in Debug builds" ON)
option(BUILD_TESTS "Build test suite" ON)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -pedantic)
    if(ENABLE_ASAN AND CMAKE_BUILD_TYPE MATCHES "Debug")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /utf-8)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CORE_SOURCES
    src/core/block_device.c
    src/core/volume.c
    src/core/fork.c
    src/core/unicode.c
    src/core/btree.c
    src/core/catalog.c
    src/core/extents.c
)

add_library(hfsplus_core STATIC ${CORE_SOURCES})
target_include_directories(hfsplus_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    # WinFsp detection
    set(WINFSP_SEARCH_PATHS
        "$ENV{WINFSP_DIR}"
        "C:/Program Files (x86)/WinFsp"
        "C:/Program Files/WinFsp"
    )

    find_path(WINFSP_INCLUDE_DIR winfsp/winfsp.h
        PATHS ${WINFSP_SEARCH_PATHS}
        PATH_SUFFIXES inc
    )

    find_library(WINFSP_LIBRARY
        NAMES winfsp-x64 winfsp-a64 winfsp-x86 winfsp-aarch64 winfsp-arm64
        PATHS ${WINFSP_SEARCH_PATHS}
        PATH_SUFFIXES lib
    )

    if(WINFSP_INCLUDE_DIR AND WINFSP_LIBRARY)
        message(STATUS "Found WinFsp: ${WINFSP_INCLUDE_DIR}")
        add_executable(hfsplus src/winfsp/main.c src/winfsp/hfsplus_winfsp.c)
        target_include_directories(hfsplus PRIVATE ${WINFSP_INCLUDE_DIR})
        target_link_libraries(hfsplus PRIVATE hfsplus_core ${WINFSP_LIBRARY} shell32)
    else()
        message(WARNING "WinFsp development files not found! Please install WinFsp from https://winfsp.dev/")
        add_executable(hfsplus src/winfsp/main.c)
        target_link_libraries(hfsplus PRIVATE hfsplus_core shell32)
    endif()
else()
    add_executable(hfsplus src/winfsp/main.c)
    target_link_libraries(hfsplus PRIVATE hfsplus_core)
endif()

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
