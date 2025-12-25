# Baresip-LVGL VoIP Client

A feature-rich **VoIP Softphone** built by integrating **LVGL** (Light and Versatile Graphics Library) with **Baresip** (SIP Stack). This project provides a modular, touchscreen-friendly UI for audio and video calls on macOS and Linux.

---

## 🚀 Features

### Core Functionality
-   **Audio & Video Calls**: Full SIP rendering using SDL2 textures.
-   **Contacts Manager**: Add, edit, and call contacts locally.
-   **Call History**: Log of incoming, outgoing, and missed calls.
-   **DTMF Keypad**: In-call keypad for IVR navigation.
-   **Account Management**: Configure SIP accounts directly in the UI.

### UI & Architecture
-   **Modular Applets**: Application logic is split into isolated applets (Home, Call, Contacts, Settings).
-   **Baresip Manager**: A unified C API wrapper around Baresip's core modules (UA, Call, Conf, Vidisp).
-   **SDL2 Rendering**: Hardware-accelerated window and input handling.
-   **Video PiP**: Picture-in-Picture support for local self-view.

---

## 🛠️ Prerequisites

### macOS (Homebrew)
```bash
brew install sdl2 ffmpeg opus mpg123 libsndfile
```
*Note: The project builds Baresip and re static libraries from source (included in `deps/`), but links against system codec libraries.*

### Linux (Debian/Ubuntu)
```bash
sudo apt-get install build-essential git \
    libsdl2-dev libavcodec-dev libavformat-dev libswscale-dev \
    libavdevice-dev libopus-dev libmpg123-dev libsndfile1-dev \
    libasound2-dev libv4l-dev
```

---

## 🏗️ Build Instructions

### Compilation
The project uses a standard `Makefile`.

```bash
# Clean previous builds
make clean

# Build the application
make
```

### Running the Application
The binary is generated in the `build/` directory.

```bash
./build/baresip-lvgl
```

---

## ⚙️ Configuration

Baresip configuration files are stored in `~/.baresip`.
The application automatically manages `config` and `accounts`, but you can verify them manually:

-   **Config**: `~/.baresip/config`
-   **Accounts**: `~/.baresip/accounts`

### Video Configuration (Auto-Patched)
The application automatically patches `~/.baresip/config` to ensure correct video rendering:
-   **Display Module**: `sdl_vidisp` (for Remote Video)
-   **Selfview Module**: `window` (Mapped to `sdl_vidisp_self` for Local Video)
-   **Video Source**: `avcapture` (macOS) or `v4l2` (Linux)

---

## 📂 Project Structure

```
baresip-lvgl/
├── src/
│   ├── applets/            # UI Modules (Call, Contacts, Settings)
│   ├── manager/
│   │   ├── baresip_manager.c  # Core SIP/Baresip integration logic
│   │   └── applet_manager.c   # Applet lifecycle & navigation
│   └── main.c              # Entry point & SDL/LVGL loop
├── include/                # Header files
├── deps/                   # Internal dependencies (baresip, re, rem)
├── lvgl/                   # LVGL Graphics Library
└── Makefile                # Build script
```

