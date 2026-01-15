cmake_minimum_required(VERSION 3.10)
project(baresip-lvgl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -O2 -D_THREAD_SAFE -DHAVE_INET6")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

add_definitions(-DAPP_USE_FBDEV)
add_definitions(-DUSE_EVDEV)

# Force static build for baresip and modules
set(STATIC OFF CACHE BOOL "Build static" FORCE)

# Force variables for FindRE.cmake and FindREM.cmake inside subprojects
set(RE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/re/include CACHE PATH "re include" FORCE)
set(RE_LIBRARY re CACHE STRING "re target" FORCE)
set(RE_LIBRARIES re CACHE STRING "re target" FORCE)
set(RE_INCLUDE_DIRS ${RE_INCLUDE_DIR} CACHE PATH "re include" FORCE)
set(re_FOUND TRUE CACHE BOOL "re found" FORCE)

set(REM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/deps/rem/include CACHE PATH "rem include" FORCE)
set(REM_LIBRARY rem CACHE STRING "rem target" FORCE)
set(REM_LIBRARIES rem CACHE STRING "rem target" FORCE)
set(REM_INCLUDE_DIRS ${REM_INCLUDE_DIR} CACHE PATH "rem include" FORCE)
set(rem_FOUND TRUE CACHE BOOL "rem found" FORCE)

# Find Threads, OpenSSL, ZLIB
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# Disable Tests to avoid mod_table linking error in shared mode
set(BUILD_TESTING OFF CACHE BOOL "Disable tests" FORCE)
set(BARESIP_UNIT_TESTS OFF CACHE BOOL "Disable tests" FORCE)

# Build dependencies
add_subdirectory(deps/re)
# add_subdirectory(deps/rem)
add_subdirectory(deps/baresip)

# Find SDL2 (Optional/Unused for FBDEV but kept for reference or dependencies)
find_package(PkgConfig REQUIRED)
# pkg_check_modules(SDL2 sdl2) 

if(NOT APPLE)
    pkg_check_modules(ALSA REQUIRED alsa)
    pkg_check_modules(LIBV4L2 REQUIRED libv4l2)
endif()

# Include Directories
include_directories(
    include
    fake_sdl
    lvgl
    lv_drivers
    .
    deps/baresip/include
    deps/re/include
    src/manager
    # deps/rem/include (Merged into re)
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

# Source Files
file(GLOB_RECURSE LVGL_SOURCES "lvgl/src/*.c")

# Use FBDEV and EVDEV drivers instead of SDL
set(LV_DRIVERS_SOURCES
    lv_drivers/display/fbdev.c
    lv_drivers/indev/evdev.c
)

file(GLOB_RECURSE APP_SOURCES 
    "src/*.c"
)

# Module Sources
set(MODULE_SOURCES
    # src/modules/gb28181/gb28181.c
)

# Executable
add_executable(baresip-lvgl
    ${APP_SOURCES}
    ${LVGL_SOURCES}
    ${LV_DRIVERS_SOURCES}
    ${MODULE_SOURCES}
)

# Enable exporting symbols so dynamic modules (opus.so) can resolve them
set_target_properties(baresip-lvgl PROPERTIES ENABLE_EXPORTS TRUE)

# Link Libraries
target_link_libraries(baresip-lvgl
    -Wl,--whole-archive baresip re -Wl,--no-whole-archive
    ssl crypto pthread z resolv sqlite3
    # avcodec avformat avutil swscale avdevice swresample
)

if(APPLE)
    target_link_libraries(baresip-lvgl
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework SystemConfiguration"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Foundation"
        "-framework Cocoa"
    )
else()
    target_link_libraries(baresip-lvgl
        ${ALSA_LIBRARIES}
        # ${LIBV4L2_LIBRARIES}
    )
endif()

install(TARGETS baresip-lvgl DESTINATION bin)
