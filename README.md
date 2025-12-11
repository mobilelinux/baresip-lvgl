# LVGL Applet Manager

A modular applet manager system for LVGL where each applet acts as an isolated application with its own lifecycle, UI, and state management.

## Features

- **Modular Architecture**: Each applet is a self-contained module with complete UI isolation
- **Lifecycle Management**: Full lifecycle callbacks (init, start, pause, resume, stop, destroy)
- **Navigation Stack**: Built-in back navigation support
- **Screen Transitions**: Smooth animations between applets
- **Example Applets**: Includes Home launcher, Settings menu, and Calculator

## Architecture

### Core Components

- **applet.h**: Defines the applet interface and lifecycle states
- **applet_manager.h/c**: Manages applet registration, navigation, and lifecycle

### Example Applets

1. **Home Applet**: Launcher with grid layout displaying all registered applets
2. **Settings Applet**: Demonstrates menu navigation and list widgets
3. **Calculator Applet**: Interactive calculator showing stateful applet implementation

## Building

### Requirements

- GCC compiler
- LVGL library (v8.x)
- Make

### Build Instructions

```bash
# Build the project
make

# Clean build files
make clean

# Build and run
make run
```

## Project Structure

```
baresip-lvgl/
├── include/
│   ├── applet.h              # Applet interface
│   └── applet_manager.h      # Applet manager API
├── src/
│   ├── main.c                # Application entry point
│   ├── applet_manager.c      # Applet manager implementation
│   └── applets/
│       ├── home_applet.c     # Home screen launcher
│       ├── settings_applet.c # Settings menu
│       └── calculator_applet.c # Calculator app
├── lv_conf.h                 # LVGL configuration
├── Makefile                  # Build system
└── README.md                 # This file
```

## Creating New Applets

To create a new applet:

1. Create a new file in `src/applets/your_applet.c`
2. Define the applet structure and implement lifecycle callbacks:

```c
#include "applet.h"
#include "applet_manager.h"

static int your_applet_init(applet_t *applet) {
    // Create UI on applet->screen
    return 0;
}

// Implement other callbacks...

APPLET_DEFINE(your_applet, "YourApp", "Description", LV_SYMBOL_HOME);

void your_applet_register(void) {
    your_applet.callbacks.init = your_applet_init;
    // Set other callbacks...
    applet_manager_register(&your_applet);
}
```

3. Add the registration function to `main.c`
4. Update the Makefile to include your source file

## Lifecycle Callbacks

- **init**: Initialize resources and create UI (called once)
- **start**: Called when applet becomes visible for the first time
- **pause**: Called when applet is hidden but kept in memory
- **resume**: Called when returning to a paused applet
- **stop**: Called before destroying the applet
- **destroy**: Cleanup resources and free memory

## Display Integration

The current implementation uses a stub display driver. For actual display output, integrate with:

- **SDL**: For desktop development and testing
- **DRM/KMS**: For embedded Linux systems
- **Framebuffer**: For simpler embedded systems

## License

This is a demonstration project for LVGL applet management.
