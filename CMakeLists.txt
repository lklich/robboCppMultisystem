# Robbo – konwersja gry LK Avalon (1989) na C++ i SDL3 (macOS, Windows, Linux)
cmake_minimum_required(VERSION 3.20)

project(Robbo VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Typ budowania" FORCE)
endif()

# macOS: minimalna wersja systemu (jak w wersji Swift).
# Aplikacja uniwersalna (Apple Silicon + Intel): -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimalna wersja macOS")

# Windows (MSVC): statyczne CRT dla programu i SDL – Robbo.exe działa bez Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- SDL3 ---------------------------------------------------------------------------------
# Domyślnie SDL3 jest pobierane z GitHuba i linkowane statycznie (jeden plik programu,
# bez dodatkowych bibliotek). -DROBBO_SYSTEM_SDL=ON używa SDL3 zainstalowanego w systemie
# (np. brew install sdl3, vcpkg install sdl3).
option(ROBBO_SYSTEM_SDL "Użyj SDL3 zainstalowanego w systemie" OFF)
set(ROBBO_SDL_TAG "release-3.4.18" CACHE STRING "Wersja SDL3 pobierana z GitHuba")

if(ROBBO_SYSTEM_SDL)
    find_package(SDL3 3.4 REQUIRED CONFIG)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${ROBBO_SDL_TAG}
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

# --- Program ------------------------------------------------------------------------------
set(ROBBO_SOURCES
    src/main.cpp
    src/App.cpp
    src/AtariData.cpp
    src/Engine.cpp
    src/Game.cpp
    src/OriginalTape.cpp
    src/Pokey.cpp
    src/Renderer.cpp
    src/SaveStore.cpp
    src/SelfTest.cpp
    src/Sha256.cpp
    src/Storage.cpp
)

# Systemowe menu „Gra” (jak w wersji Swift)
if(APPLE)
    enable_language(OBJCXX)
    list(APPEND ROBBO_SOURCES src/NativeMenu_mac.mm)
elseif(WIN32)
    list(APPEND ROBBO_SOURCES src/NativeMenu_win.cpp)
else()
    list(APPEND ROBBO_SOURCES src/NativeMenu_none.cpp)
endif()

if(APPLE)
    set(ROBBO_ICON ${CMAKE_CURRENT_SOURCE_DIR}/res/robbo.icns)
    set_source_files_properties(${ROBBO_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(Robbo MACOSX_BUNDLE ${ROBBO_SOURCES} ${ROBBO_ICON})
    set_target_properties(Robbo PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/res/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER pl.lk.robbo
        MACOSX_BUNDLE_BUNDLE_NAME Robbo
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER pl.lk.robbo
    )
elseif(WIN32)
    add_executable(Robbo WIN32 ${ROBBO_SOURCES} res/robbo.rc)
else()
    add_executable(Robbo ${ROBBO_SOURCES})
endif()

target_link_libraries(Robbo PRIVATE SDL3::SDL3)
if(APPLE)
    target_link_libraries(Robbo PRIVATE "-framework Cocoa")
endif()

if(MSVC)
    # źródła w UTF-8 (polskie napisy), bez ostrzeżeń o „niebezpiecznych” funkcjach CRT
    target_compile_options(Robbo PRIVATE /utf-8 /W3)
    target_compile_definitions(Robbo PRIVATE _CRT_SECURE_NO_WARNINGS)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Robbo)
else()
    target_compile_options(Robbo PRIVATE -Wall -Wextra)
endif()

if(MINGW)
    # MinGW: bez zależności od bibliotek libgcc/libstdc++ w DLL
    target_link_options(Robbo PRIVATE -static -static-libgcc -static-libstdc++)
endif()
