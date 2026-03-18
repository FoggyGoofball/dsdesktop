#!/bin/bash
# Quick SD card deployment script
# Copies all necessary files to an SD card mounted at the specified path

# Usage: ./deploy_to_sd.sh /media/sdcard
# Or on macOS: ./deploy_to_sd.sh /Volumes/SD

if [ -z "$1" ]; then
    echo "Usage: $0 <SD_CARD_PATH>"
    echo ""
    echo "Examples:"
    echo "  Linux:   $0 /media/sdcard"
    echo "  macOS:   $0 /Volumes/SD"
    echo "  Windows: (Use PowerShell script instead)"
    exit 1
fi

SDCARD="$1"
DSREMOTE="$SDCARD/apps/dsremote"

# Verify SD card path exists
if [ ! -d "$SDCARD" ]; then
    echo "ERROR: SD card path not found: $SDCARD"
    exit 1
fi

echo "Deploying DS Remote Desktop to: $SDCARD"

# Create directory structure
mkdir -p "$DSREMOTE"

# Copy files
echo "  Copying boot.dol..."
cp wii_proxy/wii_proxy.dol "$DSREMOTE/boot.dol" || exit 1

echo "  Copying meta.xml..."
cp wii_proxy/meta.xml "$DSREMOTE/meta.xml" || exit 1

echo "  Copying proxy.cfg..."
cp wii_proxy/proxy.cfg "$DSREMOTE/proxy.cfg" || exit 1

# Try to copy icons if available
if [ -f wii_proxy/icon.png ]; then
    echo "  Copying icon.png..."
    cp wii_proxy/icon.png "$DSREMOTE/icon.png"
fi

if [ -f wii_proxy/banner.png ]; then
    echo "  Copying banner.png..."
    cp wii_proxy/banner.png "$DSREMOTE/banner.png"
fi

echo ""
echo "✓ Deployment complete!"
echo ""
echo "SD card layout:"
ls -la "$DSREMOTE/" || true
echo ""
echo "Next steps:"
echo "1. Safely eject SD card"
echo "2. Insert into Wii"
echo "3. Go to Homebrew Channel"
echo "4. Select 'DS Remote Desktop' and launch"
