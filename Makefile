# LVGL Applet Manager Makefile with SDL2

# Project name
TARGET = baresip-lvgl

# Directories
SRC_DIR = src
APPLET_DIR = $(SRC_DIR)/applets
INC_DIR = include
LVGL_DIR = lvgl
LV_DRIVERS_DIR = lv_drivers
BARESIP_INSTALL = install
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Compiler and flags
CC = gcc
UNAME_S := $(shell uname -s)

# Common CFLAGS
COMMON_CFLAGS = -Wall -Wextra -O2 -I$(INC_DIR) -I$(LVGL_DIR) -I$(LV_DRIVERS_DIR) -I. \
                 -I deps/baresip/include -I deps/re/include -I deps/rem/include \
                 $(shell sdl2-config --cflags) \
                 $(shell pkg-config --cflags libavcodec libavutil libavformat libswscale libswresample opus) \
                 -DSTATIC

# Platform Specific Configuration
ifeq ($(UNAME_S),Linux)
    # Linux
    PLATFORM_CFLAGS = $(shell pkg-config --cflags alsa libv4l2) -D__linux__ -D_DEFAULT_SOURCE -D_BSD_SOURCE
    
    # Check for optional dependencies that baresip might have built modules for
    OPTIONAL_DEPS = libpng sndfile aom x11 xext portaudio-2.0 opencore-amrnb libpulse glib-2.0 gobject-2.0 gio-2.0 dbus-1
    OPTIONAL_LDFLAGS = $(shell for pkg in $(OPTIONAL_DEPS); do pkg-config --exists $$pkg && pkg-config --libs $$pkg; done)
    
    PLATFORM_LDFLAGS = $(shell pkg-config --libs alsa libv4l2) $(OPTIONAL_LDFLAGS) \
                       -lssl -lcrypto -lpthread -lz -lopus -lresolv -lsqlite3 -lm \
                       -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale \
                       -lx264 -lvpx 
    
    AUDIO_MOD_SRC = deps/baresip/modules/alsa/alsa.c deps/baresip/modules/alsa/alsa_src.c deps/baresip/modules/alsa/alsa_play.c
    VIDEO_MOD_SRC = deps/baresip/modules/v4l2/v4l2.c
    MODULE_SRCS_OBJC = 
else
    # macOS (Darwin)
    PLATFORM_CFLAGS = -I/opt/homebrew/include
    PLATFORM_LDFLAGS = -L/opt/homebrew/lib \
          -lssl -lcrypto -lpthread -lz -lopus -lresolv -lsqlite3 \
          -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale \
          -lx264 -lvpx -lpng -lsndfile -laom $(shell pkg-config --libs x11 xext portaudio-2.0 opencore-amrnb) \
          -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework SystemConfiguration -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework Foundation

    AUDIO_MOD_SRC = deps/baresip/modules/audiounit/audiounit.c
    VIDEO_MOD_SRC = 
    MODULE_SRCS_OBJC = deps/baresip/modules/avcapture/avcapture.m
endif

CFLAGS = $(COMMON_CFLAGS) $(PLATFORM_CFLAGS) -std=c99
OBJCFLAGS = $(COMMON_CFLAGS) $(PLATFORM_CFLAGS) -fno-objc-arc

LDFLAGS = -lm $(shell sdl2-config --libs) \
          deps/baresip/build/libbaresip.a \
          deps/re/build/libre.a \
          $(PLATFORM_LDFLAGS)


# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/manager/applet_manager.c \
       $(SRC_DIR)/manager/config_manager.c \
       $(SRC_DIR)/manager/baresip_manager.c \
       $(SRC_DIR)/manager/contact_manager.c \
       $(SRC_DIR)/manager/history_manager.c \
       $(SRC_DIR)/manager/database_manager.c \
       $(SRC_DIR)/ui/ui_helpers.c \
       $(APPLET_DIR)/home_applet.c \
       $(APPLET_DIR)/settings_applet.c \
       $(APPLET_DIR)/calculator_applet.c \
       $(APPLET_DIR)/call_applet.c \
       $(APPLET_DIR)/contacts_applet.c \
       $(APPLET_DIR)/call_log_applet.c \
       $(APPLET_DIR)/chat_applet.c \
       $(APPLET_DIR)/about_applet.c

# LVGL source files
LVGL_SRCS = $(shell find $(LVGL_DIR)/src -name '*.c')

# lv_drivers source files (only SDL-related)
LV_DRIVERS_SRCS = $(LV_DRIVERS_DIR)/sdl/sdl_common.c \
                  $(LV_DRIVERS_DIR)/sdl/sdl.c

# All source files
ALL_SRCS = $(SRCS) $(LVGL_SRCS) $(LV_DRIVERS_SRCS)

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))
LVGL_OBJS = $(patsubst $(LVGL_DIR)/%.c,$(OBJ_DIR)/lvgl/%.o,$(LVGL_SRCS))
LV_DRIVERS_OBJS = $(patsubst $(LV_DRIVERS_DIR)/%.c,$(OBJ_DIR)/lv_drivers/%.o,$(LV_DRIVERS_SRCS))

# Baresip Module Sources
MODULE_SRCS = deps/baresip/modules/avcodec/avcodec.c \
              deps/baresip/modules/avcodec/decode.c \
              deps/baresip/modules/avcodec/encode.c \
              deps/baresip/modules/avcodec/sdp.c \
              deps/baresip/modules/avformat/avformat.c \
              deps/baresip/modules/avformat/video.c \
              deps/baresip/modules/avformat/audio.c \
              deps/baresip/modules/swscale/swscale.c \
              src/modules/gb28181/gb28181.c \
              deps/baresip/modules/fakevideo/fakevideo.c \
              deps/baresip/modules/selfview/selfview.c \
              deps/baresip/modules/g711/g711.c \
              deps/baresip/modules/opus/opus.c \
              $(AUDIO_MOD_SRC) \
              $(VIDEO_MOD_SRC) \
              deps/baresip/modules/stun/stun.c \
              deps/baresip/modules/turn/turn.c \
              deps/baresip/modules/ice/ice.c

MODULE_OBJS_C = $(patsubst %.c,$(OBJ_DIR)/%.o,$(MODULE_SRCS))

# Obc Module Sources
# MODULE_SRCS_OBJC is defined above
MODULE_OBJS_OBJC = $(patsubst %.m,$(OBJ_DIR)/%.o,$(MODULE_SRCS_OBJC))


ALL_OBJS = $(OBJS) $(LVGL_OBJS) $(LV_DRIVERS_OBJS) $(MODULE_OBJS_C) $(MODULE_OBJS_OBJC)

# Default target
all: $(BUILD_DIR)/$(TARGET)

# Create build directories
$(OBJ_DIR):
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(OBJ_DIR)/applets
	@mkdir -p $(OBJ_DIR)/lvgl
	@mkdir -p $(OBJ_DIR)/lv_drivers
	@mkdir -p $(OBJ_DIR)/deps

# Link
$(BUILD_DIR)/$(TARGET): $(OBJ_DIR) $(ALL_OBJS) deps/baresip/build/libbaresip.a deps/re/build/libre.a
	@echo "Linking $(TARGET) with SDL2 for $(UNAME_S)..."
	$(CC) $(ALL_OBJS) -o $@ $(LDFLAGS)
	@echo "Build complete: $@"

# Compile application source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile LVGL source files
$(OBJ_DIR)/lvgl/%.o: $(LVGL_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling LVGL: $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile lv_drivers source files
$(OBJ_DIR)/lv_drivers/%.o: $(LV_DRIVERS_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling lv_drivers: $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile deps source files
$(OBJ_DIR)/deps/%.o: deps/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling deps: $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Compile deps objc source files
$(OBJ_DIR)/deps/%.o: deps/%.m
	@mkdir -p $(dir $@)
	@echo "Compiling deps objc: $<..."
	$(CC) $(OBJCFLAGS) -c $< -o $@

# Compile src/modules source files
$(OBJ_DIR)/src/modules/%.o: src/modules/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling src modules: $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean
clean:
	@echo "Cleaning build files..."
	rm -rf $(BUILD_DIR)
	@echo "Cleaning dependencies..."
	cd deps/re && rm -rf build
	cd deps/baresip && rm -rf build

# Dependency Rules
deps/re/build/libre.a:
	@echo "Building libre..."
	cd deps/re && cmake -B build -DLIBRE_BUILD_STATIC=ON -DLIBRE_BUILD_SHARED=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_C_FLAGS="$(COMMON_CFLAGS)" && cmake --build build

deps/baresip/build/libbaresip.a: deps/re/build/libre.a
	@echo "Building libbaresip..."
	cd deps/baresip && cmake -B build -DSTATIC=ON -Dre_DIR=../re/cmake \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_C_FLAGS="$(COMMON_CFLAGS)" \
		-DMODULES="stun;turn;ice;opus;g711;alsa;v4l2;avcodec;avformat;swscale;fakevideo;selfview;stdio" \
		&& cmake --build build

# Run
run: $(BUILD_DIR)/$(TARGET)
	@echo "Running $(TARGET) with SDL2..."
	./$(BUILD_DIR)/$(TARGET)

# Help
help:
	@echo "LVGL Applet Manager Build System with SDL2"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build the project (default)"
	@echo "  clean   - Remove build files"
	@echo "  run     - Build and run the application"
	@echo "  help    - Show this help message"
	@echo ""
	@echo "Platform Detected: $(UNAME_S)"
	@echo "LVGL and lv_drivers are included and will be compiled automatically"
	@echo "SDL2 is used for display and input (mouse, keyboard)"

.PHONY: all clean run help
