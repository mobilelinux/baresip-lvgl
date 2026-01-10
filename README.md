# baresip-lvgl

An advanced embedded SIP Softphone application for Linux/ARM platforms, combining the robust [Baresip](https://github.com/baresip/baresip) SIP stack with a modern [LVGL](https://lvgl.io) touch user interface.

![Status](https://img.shields.io/badge/status-active-success)
![License](https://img.shields.io/badge/license-BSD-blue)

## Features

### Core Telephony
- **Voice & Video Calls**: Full support for SIP voice and video calls using `g711`, `opus`, `h264` (via ffmpeg/avcodec), and more.
- **Conferencing**: Basic support for audio handling.
- **DTMF**: In-call keypad for DTMF signaling.

### User Interface (LVGL 8.3)
- **Applet Architecture**: Modular UI composed of isolated "Applets" managed by an `AppletManager`.
- **Dialer Applet**: Keypad, recent call status, and easy account switching.
- **Contacts Applet**: Add, edit, and delete contacts (stored in SQLite).
- **Messages (Chat) Applet**: Full SIP MESSAGE support with conversation history.
- **Call History**: Detailed log of Incoming, Outgoing, and Missed calls.
- **Settings**: On-device configuration for SIP Accounts, Audio/Video settings, and Network params.

### Architecture
- **Baresip Manager**: A high-level C wrapper around `libre` and `libbaresip` that handles the event loop, thread safety, and state management.
- **Database Manager**: SQLite3 backend for persistent storage of Contacts, History, and Messages.
- **SDL2 Driver**: Robust display and input driver for development/desktop environments (800x600 resolution).

## Build Instructions

### Prerequisites
- **Libraries**: `SDL2`, `sqlite3`, `openssl`, `opus`, `ffmpeg` (optional for video).
- **Tools**: `gcc`, `make`, `cmake`.

### Compiling
0. Initialize submodules:
   ```bash
   git submodule update --init --recursive
   ```

1. Build the project:
   ```bash
   make
   ```
   *Note: First build compiles LVGL and Baresip dependencies and may take a minute.*

2. Run:
   ```bash
   ./build/baresip-lvgl
   ```

### Debugging
The application outputs logs to `stdout`.
- **Log Level**: Configurable in `Settings > App Settings` or via `config.json`.
- **Visuals**: Ensure your SDL2 environment supports 32-bit color (`ARGB8888`) to match `LV_COLOR_DEPTH=32`.

## Configuration
Application state is persisted in `~/.baresip/`:
- `accounts`: SIP Account configurations.
- `config`: Core baresip settings.
- `settings.json`: App-specific settings (Log level, default account).
- `baresip_lvgl.db`: SQLite database for user data.

## Recent Updates
- **Stability**: Fixed memory corruption issues in Baresip integration.
- **Visuals**: Upgraded to 32-bit color depth and 800x600 SVGA resolution for crisp rendering without artifacts.
- **Chat**: Newly integrated messaging applet with real-time updates.

## License
BSD-3-Clause (See LICENSE file).
