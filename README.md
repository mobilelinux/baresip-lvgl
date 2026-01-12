# baresip-lvgl

An embedded SIP Softphone application for ARM platforms using [Baresip](https://github.com/baresip/baresip) and [LVGL](https://lvgl.io).

## Features
- **SIP Telephony**: Make and receive VoIP calls using Baresip.
- **Touch UI**: Responsive interface built with LVGL (Light and Versatile Graphics Library).
- **Video Calls**: Support for raw video stream display (via SDL or FBDEV).
- **History & Contacts**: SQLite-backed call history and contact management.
- **Applet-based Architecture**: Modular design with independent applets:
    - **Home**: Dashboard with status and quick actions.
    - **Dialer**: Keypad for making calls.
    - **Contacts**: Address book management.
    - **History**: Call logs (Incoming, Outgoing, Missed).
    - **Settings**: Configuration for Accounts, Audio, and System.
    - **Chat**: Instant messaging.
- **Hardware Support**: Optimized for ARM VersatilePB (QEMU) with Framebuffer output.

## Dependencies
- **baresip**: SIP stack.
- **re**: Libre SIP library.
- **lvgl**: Graphics library (v8.3+).
- **sqlite3**: Database for persistence.
- **ffmpeg**: (Optional) For video coding.

## Project Structure
The source code is organized in `src/`:
- `src/main_fbdev.c`: Entry point and LVGL initialization.
- `src/applets/`: Individual applet logic and UI.
- `src/manager/`: Core managers (Baresip, Config, Database, etc.).
- `src/ui/`: Shared UI helpers and components.

## Building

1.  **Clone the repository**:
    ```sh
    git clone https://github.com/your-repo/baresip-lvgl.git
    cd baresip-lvgl
    ```

2.  **Dependencies**:
    Ensure you have `sdl2`, `openssl`, `zlib`, `sqlite3`, and `opus` development libraries installed (or in your cross-compilation environment).

3.  **Build**:
    ```sh
    make -j$(nproc)
    ```
    This produces the `build/baresip-lvgl` executable.

## Configuration & Notes

### Color Depth (Vital for SDL)
If running with the SDL driver (e.g., on PC/QEMU), `LV_COLOR_DEPTH` **MUST** be set to `32` in `lv_conf.h`.
- **Reason**: The SDL driver creates an ARGB8888 texture. Any other bit depth (16, 24) will cause buffer overflows (heap corruption) or severe visual color artifacts (pink/green tint).
- **Video**: The `active_video` module in `baresip_manager.c` has been updated to support `ARGB8888` conversion.

### Usage
Run directly on the target:
```sh
./build/baresip-lvgl
```

The application normally starts via init script `/etc/init.d/S99baresip-lvgl` on the embedded target.

