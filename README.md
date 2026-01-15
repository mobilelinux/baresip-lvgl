# baresip-lvgl

An embedded SIP Softphone application for ARM platforms using [Baresip](https://github.com/baresip/baresip) and [LVGL](https://lvgl.io).

## Features
- **SIP Telephony**: Make and receive VoIP calls using Baresip (v4.4.0+).
- **Touch UI**: Responsive interface built with LVGL (Light and Versatile Graphics Library).
- **Video Calls**: Support for raw video stream display (via SDL or FBDEV).
- **History & Contacts**: SQLite-backed call history and contact management.
- **Applet-based Architecture**: Modular design with independent applets:
    - **Home**: Dashboard with account status, "Incomings..." notification, and quick actions.
    - **Dialer**: Keypad for making calls with smart status integration.
    - **Contacts**: Address book management.
    - **History**: Call logs (Incoming, Outgoing, Missed) with interactive callbacks.
    - **Settings**: Configuration for Accounts, Audio, and System.
    - **Chat**: Instant messaging.
    - **Call**: Dedicated In-Call and Incoming Call screens with multi-call handling.
- **Smart Navigation**: Automatically returns to the previous screen (Home/Dialer) when a call ends, preserving workflow.
- **Robustness**: 
    - Idempotent state transitions to prevent infinite loops.
    - Global watchdog for active call monitoring.
    - Multi-session support: Handle multiple incoming calls with a sidebar list and seamless switching.
- **Hardware Support**: Optimized for ARM VersatilePB (QEMU) with Framebuffer output and `evdev` input.

## Dependencies
- **baresip**: SIP stack (v4.4.0+).
- **re**: Libre SIP library (v4.4.0+).
- **lvgl**: Graphics library (v8.3+).
- **sqlite3**: Database for persistence.
- **ffmpeg**: (Optional) For video coding.

## Project Structure
The source code is organized in `src/`:
- `src/main.c`: Entry point with dual support for SDL (dev) and FBDEV (target).
- `src/applets/`: Individual applet logic and UI (`call_applet.c`, `home_applet.c`, etc.).
- `src/manager/`: Core managers (Baresip, Config, Database, Applet).
- `src/ui/`: Shared UI helpers and components.

## Usage
Run directly on the target:
```sh
baresip-lvgl
```

The application normally starts via init script `/etc/init.d/S99baresip-lvgl`.
