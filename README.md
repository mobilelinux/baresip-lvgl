# Baresip-LVGL

**Baresip-LVGL** is a SIP User Agent (VoIP Client) with a modern, touch-friendly graphical user interface built using [LVGL](https://lvgl.io/) (Light and Versatile Graphics Library). It runs on top of the powerful [Baresip](https://github.com/baresip/baresip) core, designed for embedded Linux systems.

## Overview

This project provides a complete VoIP solution with video calling capabilities, suitable for embedded devices, control panels, and mobile Linux platforms. It uses a modular **Applet Architecture** to manage different UI contexts.

## Features

### 📞 Core VoIP
*   **SIP Signaling:** Robust SIP handling via Baresip.
*   **Audio Calls:** High-quality voice calls with G.711, Opus, etc.
*   **Video Calls:** H.264/VP8 video support with valid remote and local video previews.
*   **Call Management:** 
    *   Multi-call support (Hold/Resume).
    *   **Call Forwarding** (Blind Transfer).
    *   **DTMF Keypad** (In-call tone generation).
    *   **Mute/Unmute** (visual microphone indicator).
    *   Speakerphone toggle.

### 🖥️ User Interface (LVGL)
*   **Touch-Friendly:** Designed for touchscreen interaction.
*   **Applet System:**
    *   **Home:** Quick overview, time/date, account status.
    *   **Call (Dialer):** Keypad, Active Call screen, Incoming Call cards.
    *   **Contacts:** Address book management.
    *   **Recents:** Call history log.
    *   **Messages:** SIP instant messaging (Chat).
    *   **Settings:** Configuration for Accounts, Audio/Video, Network, etc.
*   **Gestures:** Swipe navigation between applets.

## Architecture

The application is structured around an **Applet Manager**:

*   `src/main.c`: Entry point, initializes Baresip and LVGL.
*   `src/applet_manager.c`: Manages the lifecycle and switching of UI modules (Applets).
*   `src/manager/baresip_manager.c`: The bridge between the UI and the Baresip core thread.
*   `src/applets/`: Individual logic and UI for each feature (Call, Home, Settings, etc.).

## Build Instructions

Building is handled via `Make`. The project includes LVGL as a submodule.

For detailed build steps, please refer to **[BUILD.md](BUILD.md)**.

```bash
# Quick Build
git submodule update --init --recursive
make
./build/baresip-lvgl
```

## Configuration

*   **SIP Accounts:** Managed via the Settings Applet or `config_manager`.
*   **Video:** Supports Framebuffer and SDL display drivers (configurable in `lv_conf.h` / `lv_drv_conf.h`).

## License

See [LICENSE](LICENSE) file.
