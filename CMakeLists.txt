cmake_minimum_required(VERSION 3.10)
project(baresip-lvgl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -D_THREAD_SAFE")

# Find SDL2
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

# Include Directories
include_directories(
    include
    lvgl
    lv_drivers
    .
    deps/baresip/include
    deps/re/include
    ${SDL2_INCLUDE_DIRS}
    /opt/homebrew/include
)

# Link Directories
link_directories(
    /opt/homebrew/lib
)

# Source Files
file(GLOB_RECURSE LVGL_SOURCES "lvgl/src/*.c")

set(LV_DRIVERS_SOURCES
    lv_drivers/sdl/sdl_common.c
    lv_drivers/sdl/sdl.c
)

file(GLOB_RECURSE APP_SOURCES 
    "src/*.c"
)

# Executable
add_executable(baresip-lvgl
    ${APP_SOURCES}
    ${LVGL_SOURCES}
    ${LV_DRIVERS_SOURCES}
)

# Link Libraries
target_link_libraries(baresip-lvgl
    ${SDL2_LIBRARIES}
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/baresip/build/libbaresip.a
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/re/build/libre.a
    ssl crypto pthread z opus resolv sqlite3
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreFoundation"
    "-framework SystemConfiguration"
)
