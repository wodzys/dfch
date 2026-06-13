cmake_minimum_required(VERSION 3.16)
project(dfch)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_SUBDIR x64)
else()
    set(ARCH_SUBDIR x86)
endif()

# --- 1. Specify SDL2 header path (from DFHack) ---
if(NOT EXISTS "${SDL2_INCLUDE_DIRS}/SDL.h")
    message(FATAL_ERROR "SDL2 headers not found at: ${SDL2_INCLUDE_DIRS}")
endif()

# --- 2. Find dependency libraries (via vcpkg) ---
if(NOT DEFINED VCPKG_ROOT AND DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
endif()


if(NOT VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows")
endif()

set(VCPKG_INSTALLED "${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/vcpkg_installed/${VCPKG_TARGET_TRIPLET}")

# Detours
find_path(DETOURS_INCLUDE_DIRS "detours/detours.h" PATHS ${VCPKG_INSTALLED}/include)
find_library(DETOURS_LIBRARY detours PATHS ${VCPKG_INSTALLED}/lib)
if(NOT DETOURS_INCLUDE_DIRS OR NOT DETOURS_LIBRARY)
    message(FATAL_ERROR "Detours not found in ${VCPKG_INSTALLED}")
endif()

# spdlog (header-only)
find_path(SPDLOG_INCLUDE_DIRS "spdlog/spdlog.h" PATHS ${VCPKG_INSTALLED}/include)
if(NOT SPDLOG_INCLUDE_DIRS)
    message(FATAL_ERROR "SPDLOG not found in ${VCPKG_INSTALLED}")
endif()

# toml++
find_path(TOMLPLUSPLUS_INCLUDE_DIRS "toml++/toml.hpp" PATHS "${VCPKG_INSTALLED}/include")
if(NOT TOMLPLUSPLUS_INCLUDE_DIRS)
    message(FATAL_ERROR "toml++ header files not found in ${VCPKG_INSTALLED}")
endif()

# --- 3. Download SDL2_ttf dependency ---
option(USE_SYSTEM_SDL2_TTF "Use system-installed SDL2_ttf" OFF)

if(USE_SYSTEM_SDL2_TTF)
else()
    # Download SDL2_ttf release and extract into 3rdParty in the build dir
    # all we need are the header files, so the same release package
    # will work for all platforms
    set(SDL2_TTF_VERSION 2.24.0)
    set(SDL2_TTF_ZIP_MD5 64f9ef877af440dbc11c840732a60104)
    set(SDL2_TTF_ZIP_FILE SDL2_ttf-devel-${SDL2_TTF_VERSION}-VC.zip)
    set(SDL2_TTF_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdParty/SDL2_ttf)
    set(SDL2_TTF_ZIP_PATH ${SDL2_TTF_SOURCE_DIR}/${SDL2_TTF_ZIP_FILE})
    set(SDL2_TTF_EXTRACT_DIR ${SDL2_TTF_SOURCE_DIR}/SDL2_ttf-${SDL2_TTF_VERSION})
    download_file("https://github.com/libsdl-org/SDL_ttf/releases/download/release-${SDL2_TTF_VERSION}/${SDL2_TTF_ZIP_FILE}"
        ${SDL2_TTF_ZIP_PATH}
        ${SDL2_TTF_ZIP_MD5})
    file(ARCHIVE_EXTRACT INPUT ${SDL2_TTF_ZIP_PATH} DESTINATION ${SDL2_TTF_SOURCE_DIR})

    set(SDL2_TTF_INCLUDE_DIR ${SDL2_TTF_EXTRACT_DIR}/include)
    set(SDL2_TTF_DLL_PATH ${SDL2_TTF_EXTRACT_DIR}/lib/${ARCH_SUBDIR}/SDL2_ttf.dll)
endif()

if(NOT SDL2_TTF_INCLUDE_DIR)
    message(FATAL_ERROR "SDL2_ttf include directory not found.")
endif()

# --- 4. Source file list ---
set(PROJECT_SRCS
    dfch.cpp
    hooks.cpp
    sdl2_hooks.cpp
    dict_manager.cpp
    rulesets_manager.cpp
    sentence_detector.cpp
    screen_manager.cpp
    ttf_manager.cpp
    logger.cpp
    config.cpp
)

# --- 5. Create DFHack plugin target ---
dfhack_plugin(dfch ${PROJECT_SRCS}
    LINK_LIBRARIES ${DETOURS_LIBRARY}
    COMPILE_FLAGS_MSVC "/utf-8"
)

# --- 6. Set header paths ---
target_include_directories(dfch
    PRIVATE
        ${DETOURS_INCLUDE_DIRS}
        ${SPDLOG_INCLUDE_DIRS}
        ${TOMLPLUSPLUS_INCLUDE_DIRS}
        ${SDL2_INCLUDE_DIRS}          # SDL2.h provided by DFHack
        ${SDL2_TTF_INCLUDE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}   # Local header files
)

# --- 7. Compile definitions ---
target_compile_definitions(dfch PRIVATE FMT_HEADER_ONLY)

# --- 8. Install data files ---
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
    DESTINATION "${DFHACK_DATA_DESTINATION}/data/dfch"
)

# --- 9. Install SDL2_ttf.dll to game root directory (critical!) ---
if(EXISTS "${SDL2_TTF_DLL_PATH}")
    install(FILES "${SDL2_TTF_DLL_PATH}"
        DESTINATION "${DFHACK_BINARY_DESTINATION}"  # ← Game root directory (same level as exe)
        COMPONENT Runtime
    )
else()
    message(WARNING "SDL2_ttf.dll not found at ${SDL2_TTF_DLL_PATH}")
endif()
