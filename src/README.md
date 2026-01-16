# baresip-lvgl

An embedded SIP Softphone application for ARM platforms using [Baresip](https://github.com/baresip/baresip) and [LVGL](https://lvgl.io).

## Features
- **SIP Telephony**: Make and receive VoIP calls using Baresip.
- **Advanced Call Handling**:
    - **Multi-Call Support**: Seamlessly switch between multiple active calls (Up to 8 concurrent).
    - **Zombie Call Buffer**: Internal architecture allows 32 background call slots to prevent blocking during network teardown.
    - **Instant Slot Recycling**: Forcibly recycles call slots on hangup to ensure immediate UI responsiveness.
    - **Gestures**: Swipe Up to minimize call screen; intuitive list navigation.
    - **Robust State Management**: Automatic recovery from network drops, "ghost" calls, and restart loops.
- **Touch UI**: Responsive interface built with LVGL (Light and Versatile Graphics Library).
- **Visuals**: Clean, modern aesthetics with transparent overlays and smooth transitions.
- **Video Calls**: Support for raw video stream display (via SDL or FBDEV).
- **History & Contacts**: SQLite-backed call history and contact management.
- **Hardware Support**: Optimized for ARM VersatilePB (QEMU) with Framebuffer output.

## Dependencies
- **baresip**: SIP stack.
- **re**: Libre SIP library.
- **lvgl**: Graphics library (v8.3+).
- **sqlite3**: Database for persistence.
- **ffmpeg**: (Optional) For video coding.

## Usage
Run directly on the target:
```sh
baresip-lvgl
```

The application normally starts via init script `/etc/init.d/S99baresip-lvgl`.
