#!/bin/bash
# Create a minimal Wii Homebrew Channel icon (48×48 PNG, RGBA)
# This generates a placeholder programmatically using ImageMagick or similar.
# For production: use a proper graphics editor.

# Install ImageMagick if needed:
#   Ubuntu/Debian: sudo apt-get install imagemagick
#   macOS: brew install imagemagick
#   Windows: Download from https://imagemagick.org/

# Create a simple gradient placeholder icon (48×48, blue to cyan)
convert -size 48x48 \
  gradient:blue-cyan \
  -gravity center \
  -pointsize 20 \
  -fill white \
  -annotate +0+0 "DSRD" \
  icon.png

# Create a simple banner (192×64, gradient)
convert -size 192x64 \
  gradient:navy-blue \
  -gravity center \
  -pointsize 24 \
  -fill white \
  -annotate +0+0 "DS Remote Desktop" \
  banner.png

echo "Generated icon.png (48x48) and banner.png (192x64)"
echo "Copy these to: sd:/apps/dsremote/"
