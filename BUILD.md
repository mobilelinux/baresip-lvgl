# Build Instructions

## Quick Start

```bash
# Initialize dependencies
git submodule update --init --recursive

# Build the project
make

# Run the application
./build/baresip-lvgl

# Clean build files
make clean
```

## What Gets Built

The build system automatically:
1. Compiles all application source files
2. Compiles LVGL library from the `lvgl/` subdirectory
3. Links everything into a single executable

## Build Output

```
build/
├── baresip-lvgl          # Executable
└── obj/
    ├── main.o
    ├── applet_manager.o
    ├── applets/
    │   ├── home_applet.o
    │   ├── settings_applet.o
    │   └── calculator_applet.o
    └── lvgl/             # LVGL object files
        └── src/
            └── ...
```

## Dependencies

- **GCC**: C compiler
- **Make**: Build system
- **LVGL**: Included as subdirectory (no separate installation needed)

## Notes

- LVGL v8.3 is included in the project
- No external LVGL installation required
- Build time: ~30-60 seconds (first build compiles LVGL)
- Subsequent builds are faster (only changed files recompile)
