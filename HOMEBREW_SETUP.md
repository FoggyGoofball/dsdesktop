# Homebrew Channel Installation Guide

## SD Card Layout for Wii Homebrew

```
sd:/
├── apps/
│   └── dsremote/
│       ├── boot.dol              ← wii_proxy.dol renamed here
│       ├── meta.xml              ← Homebrew Channel metadata
│       ├── icon.png              ← 48×48 RGBA icon (optional but recommended)
│       ├── banner.png            ← 192×64 PNG banner (optional)
│       ├── proxy.cfg             ← Runtime configuration
│       └── haxxstation.nds        ← For DS exploit (optional)
└── boot.elf                       ← Homebrew Channel launcher (pre-installed)
```

---

## Step 1: Prepare SD Card Directory

1. Insert SD card into PC (or card reader).
2. Create the directory structure if it doesn't exist:
   ```
   mkdir -p sd:/apps/dsremote
   ```

---

## Step 2: Copy Files

1. **Boot executable:**
   ```
   cp wii_proxy/wii_proxy.dol → sd:/apps/dsremote/boot.dol
   ```

2. **Metadata (this enables Homebrew Channel display):**
   ```
   cp wii_proxy/meta.xml → sd:/apps/dsremote/meta.xml
   ```

3. **Configuration:**
   ```
   cp wii_proxy/proxy.cfg → sd:/apps/dsremote/proxy.cfg
   ```
   (Or create manually with content from README.md deployment section)

4. **Icons (optional but strongly recommended for polish):**
   ```
   cp wii_proxy/icon.png → sd:/apps/dsremote/icon.png
   cp wii_proxy/banner.png → sd:/apps/dsremote/banner.png
   ```

5. **DS Exploit ROM (if using Haxxstation method):**
   ```
   Rename: "DS Download Station - Volume 1.nds" → "haxxstation.nds"
   cp haxxstation.nds → sd:/apps/dsremote/haxxstation.nds
   ```

---

## Step 3: Generate Icons (Optional)

If you don't have icon/banner images yet, use the provided script:

**On Linux/macOS:**
```bash
cd wii_proxy
chmod +x generate_icons.sh
./generate_icons.sh
# Copies icon.png and banner.png to sd:/apps/dsremote/
```

**On Windows (PowerShell with ImageMagick installed):**
```powershell
cd wii_proxy
# Using ImageMagick's convert command:
convert -size 48x48 gradient:blue-cyan -gravity center -pointsize 20 -fill white -annotate +0+0 "DSRD" icon.png
convert -size 192x64 gradient:navy-blue -gravity center -pointsize 24 -fill white -annotate +0+0 "DS Remote Desktop" banner.png
```

---

## Step 4: Verify SD Card Layout

After copying, your SD card should look like:
```
sd:/apps/dsremote/
├── boot.dol                  (executable)
├── meta.xml                  (Homebrew Channel metadata)
├── icon.png                  (48×48 optional)
├── banner.png                (192×64 optional)
├── proxy.cfg                 (configuration)
└── haxxstation.nds           (optional, for Download Play exploit)
```

**Check:** If `meta.xml` is present and valid XML, Homebrew Channel will:
- Discover the app
- Display the name "DS Remote Desktop"
- Show the icon (if provided)
- List version "1.0.0"

---

## Step 5: Launch from Homebrew Channel

1. Insert SD card into Wii.
2. Go to Wii Menu → Homebrew Channel.
3. You should see "DS Remote Desktop" in the app list.
4. Select it and press A to launch.
5. Wii proxy starts up, runs channel calibration, and listens for DS connection.

**Expected console output (if connected via USB loader):**
```
=== DS Remote Desktop — Wii Proxy ===
Initialising backhaul (USB Ethernet)...
Backhaul ready. IP = 192.168.1.50
Initialising NiFi radio...
[CAL] Running channel latency benchmark...
[CAL] Best channel selected: 1
Proxy active. Listening on UDP 17394...
```

---

## File Format Reference

### meta.xml

Required fields:
- `<name>` — Display name in Homebrew Channel
- `<version>` — Semantic version (1.0.0 format)
- `<coder>` — Author
- `<short_description>` — One-liner shown in menu
- `<long_description>` — Detailed description (shown when selected)
- `<release_date>` — YYYYMMDD format

Optional fields:
- `<changelog>` — Release notes
- `<app_type>` — Homebrew, Emulator, etc.

### icon.png / banner.png

- **Icon:** 48×48 pixels, RGBA PNG format
- **Banner:** 192×64 pixels, RGB or RGBA PNG format
- Both are optional; Homebrew Channel uses defaults if missing
- Recommended for visual polish

### proxy.cfg

INI-style configuration:
```ini
mode=usb                    # usb or wifi
channel=1                   # 1-11
auto_channel=1              # 1 or 0
channels=1,6,11             # CSV list
probe_count=18
probe_timeout_ms=80
pc_ip=192.168.1.100
pc_port=17394
```

---

## Troubleshooting

| Issue | Cause | Solution |
|-------|-------|----------|
| App doesn't appear in Homebrew Channel | `meta.xml` missing or invalid XML | Verify file exists and is well-formed XML |
| App appears but shows generic icon | `icon.png` missing | Generate or provide 48×48 PNG icon |
| Wii freezes on launch | `boot.dol` incompatible or corrupted | Rebuild `wii_proxy.dol` cleanly |
| Config not loaded | `proxy.cfg` path wrong or malformed | Check `sd:/apps/dsremote/proxy.cfg` exists and is valid INI |
| USB Ethernet not detected | IOS mismatch or cable not recognized | Use IOS 21 or later; verify USB dongle model |

---

## Tips

1. **Keep a backup** of your `meta.xml` and config files.
2. **Test the path** by manually checking SD card: `sd:/apps/dsremote/boot.dol` should exist.
3. **Use a FAT32 SD card** (Wii requirement); exFAT won't work.
4. **Rebuild icons** if you make graphical updates.
5. **Version management:** Increment `<version>` in `meta.xml` for each release.

---

## Next Steps

Once Homebrew Channel shows the app:
1. Launch it → Wii proxy runs channel calibration
2. Prepare DS with haxxstation or FlashMe
3. Start PC host (`dsrd_host.exe --wii 192.168.1.50`)
4. Send DS client ROM to DS RAM (using wii-ds-rom-sender or equivalent)
5. DS should join NiFi network and display PC desktop
