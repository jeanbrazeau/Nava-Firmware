#!/bin/bash
# Migrate Nava Arduino project to PlatformIO structure
# This script maintains Arduino IDE compatibility by keeping jjjjj_firmware/ intact

set -e  # Exit on error

PROJECT_ROOT="/Users/admin/offline/Nava-Firmware"
cd "$PROJECT_ROOT"

echo "=== Nava Firmware PlatformIO Migration ==="
echo ""

# 1. Create PlatformIO directory structure
echo "[1/5] Creating PlatformIO directory structure..."
mkdir -p src
mkdir -p lib
mkdir -p include

# 2. Copy source files to src/
echo "[2/5] Copying Arduino source files to src/..."
cp jjjjj_firmware/*.ino src/
cp jjjjj_firmware/*.h src/
[ -f jjjjj_firmware/*.cpp ] && cp jjjjj_firmware/*.cpp src/ || true

# Rename main .ino to main.cpp for PlatformIO
if [ -f src/jjjjj_firmware.ino ]; then
    mv src/jjjjj_firmware.ino src/main.cpp
    echo "  - Renamed jjjjj_firmware.ino -> main.cpp"
fi

# 3. Move custom libraries to lib/
echo "[3/5] Moving custom libraries to lib/..."
if [ -d jjjjj_firmware/src/SPI ]; then
    cp -r jjjjj_firmware/src/SPI lib/
    echo "  - Copied SPI library"
fi
if [ -d jjjjj_firmware/src/WireN ]; then
    cp -r jjjjj_firmware/src/WireN lib/
    echo "  - Copied WireN library"
fi
if [ -d jjjjj_firmware/src/MemoryFree ]; then
    cp -r jjjjj_firmware/src/MemoryFree lib/
    echo "  - Copied MemoryFree library"
fi

# 4. Update .gitignore
echo "[4/5] Updating .gitignore..."
cat >> .gitignore << 'EOF'

# PlatformIO
.pio/
.vscode/
*.pio
build/
EOF
echo "  - Added PlatformIO entries to .gitignore"

# 5. Create README for the migration
echo "[5/5] Creating migration documentation..."
cat > PLATFORMIO_MIGRATION.md << 'EOF'
# PlatformIO Migration Notes

## Directory Structure

```
Nava-Firmware/
├── platformio.ini          # PlatformIO configuration
├── src/                    # PlatformIO source files (auto-generated from jjjjj_firmware/)
│   ├── main.cpp           # Main firmware (was jjjjj_firmware.ino)
│   ├── *.ino              # Other Arduino modules
│   └── *.h                # Headers
├── lib/                    # Custom libraries
│   ├── SPI/
│   ├── WireN/
│   └── MemoryFree/
├── include/                # Global headers (if needed)
├── jjjjj_firmware/        # ORIGINAL Arduino IDE project (keep for compatibility)
│   └── ...                # Use this for Arduino IDE development
└── tools/                  # Build tools
    └── hex2sysex/
```

## Development Workflows

### PlatformIO (VSCode/CLI)
```bash
# Build firmware
pio run

# Upload to device
pio run --target upload

# Clean build
pio run --target clean

# Build and auto-convert to SysEx
pio run  # SysEx file created in .pio/build/nava_sysex/
```

### Arduino IDE (Still Supported)
1. Open `jjjjj_firmware/jjjjj_firmware.ino` in Arduino IDE
2. Develop and test as before
3. After changes, sync to PlatformIO:
   ```bash
   ./sync_to_platformio.sh
   ```

## Synchronization

If you modify files in `jjjjj_firmware/`, run:
```bash
./sync_to_platformio.sh
```

This will copy updated files to `src/` and `lib/`.

## Notes

- `src/` and `lib/` are derived from `jjjjj_firmware/` - treat as build artifacts
- Always edit in `jjjjj_firmware/` if using Arduino IDE
- PlatformIO users can edit `src/` directly
- Both workflows are supported - choose based on preference
EOF

# Create sync script
cat > sync_to_platformio.sh << 'EOF'
#!/bin/bash
# Sync changes from jjjjj_firmware/ to src/ and lib/
echo "Syncing Arduino sources to PlatformIO structure..."
cp jjjjj_firmware/*.ino src/
cp jjjjj_firmware/*.h src/
[ -f jjjjj_firmware/*.cpp ] && cp jjjjj_firmware/*.cpp src/ || true
mv src/jjjjj_firmware.ino src/main.cpp 2>/dev/null || true
cp -r jjjjj_firmware/src/* lib/
echo "Sync complete!"
EOF
chmod +x sync_to_platformio.sh

echo ""
echo "=== Migration Complete! ==="
echo ""
echo "Project structure:"
echo "  ✓ src/         - PlatformIO source files"
echo "  ✓ lib/         - Custom libraries (SPI, WireN, MemoryFree)"
echo "  ✓ include/     - Global headers"
echo ""
echo "Original Arduino IDE project preserved in:"
echo "  → jjjjj_firmware/"
echo ""
echo "Next steps:"
echo "  1. Review PLATFORMIO_MIGRATION.md"
echo "  2. Run: pio run"
echo "  3. Test build and SysEx conversion"
echo ""
echo "To sync future Arduino IDE changes:"
echo "  ./sync_to_platformio.sh"
echo ""
