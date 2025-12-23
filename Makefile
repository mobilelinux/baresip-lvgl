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
COMMON_CFLAGS = -Wall -Wextra -O2 -I$(INC_DIR) -I$(LVGL_DIR) -I$(LV_DRIVERS_DIR) -I. \
                 -I deps/baresip/include -I deps/re/include \
                 $(shell sdl2-config --cflags) \
                 $(shell pkg-config --cflags libavcodec libavutil libavformat libswscale libswresample opus) \
                 -I/opt/homebrew/include -DSTATIC

CFLAGS = $(COMMON_CFLAGS) -std=c99
OBJCFLAGS = $(COMMON_CFLAGS) -fno-objc-arc

LDFLAGS = -L/opt/homebrew/lib -lm $(shell sdl2-config --libs) \
          deps/baresip/build/libbaresip.a \
          deps/re/build/libre.a \
          -lssl -lcrypto -lpthread -lz -lopus -lresolv -lsqlite3 \
          -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale \
          -lx264 -lvpx \
          -framework CoreAudio -framework AudioToolbox -framework CoreFoundation -framework SystemConfiguration -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework Foundation


# Source files
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/logger/logger.c \
       $(SRC_DIR)/manager/applet_manager.c \
       $(SRC_DIR)/manager/config_manager.c \
       $(SRC_DIR)/manager/baresip_manager.c \
       $(SRC_DIR)/manager/contact_manager.c \
       $(SRC_DIR)/manager/history_manager.c \
       $(SRC_DIR)/manager/database_manager.c \
       $(APPLET_DIR)/home_applet.c \
       $(APPLET_DIR)/settings_applet.c \
       $(APPLET_DIR)/calculator_applet.c \
       $(APPLET_DIR)/call_applet.c \
       $(APPLET_DIR)/contacts_applet.c \
       $(APPLET_DIR)/call_log_applet.c \
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
              deps/baresip/modules/audiounit/audiounit.c \
              deps/baresip/modules/stun/stun.c \
              deps/baresip/modules/turn/turn.c \
              deps/baresip/modules/ice/ice.c

MODULE_OBJS_C = $(patsubst %.c,$(OBJ_DIR)/%.o,$(MODULE_SRCS))

# Obc Module Sources
MODULE_SRCS_OBJC = deps/baresip/modules/avcapture/avcapture.m
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

# Link
$(BUILD_DIR)/$(TARGET): $(OBJ_DIR) $(ALL_OBJS)
	@echo "Linking $(TARGET) with SDL2..."
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
	@echo "LVGL and lv_drivers are included and will be compiled automatically"
	@echo "SDL2 is used for display and input (mouse, keyboard)"

.PHONY: all clean run help

