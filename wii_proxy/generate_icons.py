#!/bin/bash
# Minimal PNG icon generator for Wii Homebrew Channel
# Generates placeholder RGBA PNG images in correct size

# For production, replace with proper artwork

# This script assumes ImageMagick 'convert' is available
# macOS:     brew install imagemagick
# Linux:     apt-get install imagemagick
# Windows:   choco install imagemagick OR download from imagemagick.org

set -e

echo "Generating Wii Homebrew Channel icons..."

# Generate icon.png (48×48)
# Simple gradient with text
python3 << 'PYTHON_END'
from PIL import Image, ImageDraw, ImageFont

# Create 48×48 icon (blue gradient)
img = Image.new('RGBA', (48, 48), color=(20, 50, 150, 255))
draw = ImageDraw.Draw(img)

# Fill with gradient (simple approximation)
for i in range(48):
    r = int(20 + (100 * i / 48))
    g = int(50 + (100 * i / 48))
    b = int(150 + (50 * i / 48))
    draw.rectangle([(i, 0), (i, 48)], fill=(r, g, b, 255))

# Draw text "DSRD" (if font available)
try:
    # Try to use a default system font
    draw.text((12, 16), "DSRD", fill=(255, 255, 255, 255))
except:
    # Fallback: just white circle in center
    draw.ellipse([(20, 20), (28, 28)], fill=(255, 255, 255, 255))

img.save('icon.png')
print("✓ Generated icon.png (48×48 RGBA)")

# Generate banner.png (192×64)
img = Image.new('RGBA', (192, 64), color=(0, 0, 100, 255))
draw = ImageDraw.Draw(img)

# Fill with gradient
for i in range(192):
    r = int(0 + (50 * i / 192))
    g = int(0 + (50 * i / 192))
    b = int(100 + (100 * i / 192))
    draw.rectangle([(i, 0), (i, 64)], fill=(r, g, b, 255))

# Add text if possible
try:
    draw.text((50, 20), "DS Remote Desktop", fill=(255, 255, 255, 255))
except:
    pass

img.save('banner.png')
print("✓ Generated banner.png (192×64 RGBA)")

print("\nCopy these to: sd:/apps/dsremote/")
print("  icon.png   → sd:/apps/dsremote/icon.png")
print("  banner.png → sd:/apps/dsremote/banner.png")
PYTHON_END

