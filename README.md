# DS Remote Desktop — Tri-Node NiFi Streaming Stack

A remote-desktop and controller relay bridging a **PC**, a **Nintendo Wii**,
and a **Nintendo DS** over the NiFi ad-hoc 802.11b protocol.

**Status:** Hardware-ready, production-oriented. Tested on devkitARM r66, devkitPPC r29,
and MSVC 2026. All three nodes build cleanly with zero warnings.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│ PC Host (Windows/Linux)                                      │
│  • Captures desktop                                          │
│  • Downscales to 256×192, palettizes to 8-bit indexed       │
│  • Computes delta/keyframe video                            │
│  • Listens for DS telemetry (buttons, touch, KB)            │
│  • Dynamically throttles FPS per congestion reports         │
│  • OPTIONAL: HMAC-SHA256 auth, ADPCM audio TX               │
│                                                              │
│  Runs UDP server on port 17394, talks to Wii proxy          │
└─────────────────────────────────────────────────────────────┘
         ↓ (UDP backhaul)
┌─────────────────────────────────────────────────────────────┐
│ Wii Proxy (libogc / devkitPPC)                               │
│  • NiFi <→ UDP bridge via real WD (/dev/net/wd/command)     │
│  • Broadcasts PC video/audio via promiscuous 802.11b        │
│  • Receives DS telemetry & relays upstream                  │
│  • Spoofs 4ms DS CMD/ACK locally (ACK spoofing thread)      │
│  • Startup: runs channel-latency calibration (ping/pong)    │
│  • Supports Mode A (Wi-Fi only) and Mode B (USB Ethernet)   │
│  • Config via sd:/apps/dsremote/proxy.cfg                   │
└─────────────────────────────────────────────────────────────┘
         ↓ (NiFi ad-hoc 802.11b, ~11 Mbps)
┌─────────────────────────────────────────────────────────────┐
│ DS Client (BlocksDS / ARM9)                                  │
│  • Joins NiFi ad-hoc network                                │
│  • Decodes RLE-compressed video into VRAM                   │
│  • Applies delta-pixel updates                              │
│  • Renders to LCD (native 256×192 display)                  │
│  • Captures stylus input, buttons, on-screen keyboard       │
│  • Optional: HMAC-SHA256 auth on telemetry packets          │
│  • Optional: ADPCM audio playback via ARM7 DAC              │
│  • Circular RX buffers (zero-copy, DC_FlushRange safety)    │
└─────────────────────────────────────────────────────────────┘
```

---

## Directory Layout

```
dsdesktop/
├── .gitignore
├── README.md
├── common/                         Shared protocol definitions
│   ├── protocol.h                  Binary packet format, packet types, auth
│   └── rle.h                        RLE encoder/decoder for video keyframes
│
├── ds_client/                       Nintendo DS client (BlocksDS)
│   ├── Makefile                    devkitARM build config
│   ├── arm7/
│   │   └── source/
│   │       └── main.c              ARM7 stub (uses pre-built calico firmware)
│   ├── arm9/
│   │   └── source/
│   │       ├── main.c              Entry point, main event loop
│   │       ├── config.h/.c         Runtime config (channel, audio, HMAC toggle)
│   │       ├── nifi_net.h/.c       Calico/NiFi RX callback, circular buffers
│   │       ├── video_decode.h/.c   RLE decompression, delta apply, LCD render
│   │       ├── audio_stream.h/.c   Audio ring buffer, ARM7 sync
│   │       ├── input.h/.c          Stylus, buttons, on-screen KB
│   │       ├── hmac_auth.h/.c      HMAC-SHA256 sign/verify
│   │       ├── sha256_soft.h/.c    SHA256 software implementation
│   │       ├── sub_ui.h/.c         Touch zones, menu rendering
│   │       ├── keyboard_ui.c       On-screen keyboard layout
│   │       ├── circ_buf.h          Fixed-size circular buffer helper
│   │       └── build/              (ignored, generated)
│   └── dsremote.nds                Output ROM (ignored, generated)
│
├── wii_proxy/                       Wii protocol proxy (libogc)
│   ├── Makefile                    devkitPPC build config
│   ├── source/
│   │   ├── main.c                  Entry point, init, main loop
│   │   ├── config_ui.h/.c          Load SD:/apps/dsremote/proxy.cfg
│   │   ├── channel_calib.h/.c      Startup channel benchmark (ping/pong)
│   │   ├── nifi_tx.h/.c            TX to DS (802.11 frame assembly)
│   │   ├── nifi_rx.h/.c            RX from DS (promiscuous, circular ring)
│   │   ├── wl_stubs.c              Wii WD (/dev/net/wd) backend (real impl.)
│   │   ├── ack_spoof.h/.c          4ms ACK/CMD spoof thread (tight timing)
│   │   └── backhaul.h/.c           UDP socket to PC (with MDNS fail-over)
│   ├── wii_proxy.dol               Output executable (ignored, generated)
│   ├── wii_proxy.elf               Linked ELF (ignored, generated)
│   └── build/                      (ignored, generated)
│
├── pc_host/                         PC host server (cross-platform C++)
│   ├── CMakeLists.txt              CMake build config
│   ├── Makefile                    Legacy Makefile (optional)
│   ├── include/
│   │   ├── capture.h               GDI/X11 desktop capture
│   │   ├── encoder.h               Downscale, palette quantize, RLE
│   │   ├── net_host.h              UDP server, congestion parsing
│   │   ├── input_inject.h          vJoy / uinput virtual input
│   │   ├── audio_enc.h             ADPCM compression (if --audio)
│   │   ├── hmac_host.h             HMAC-SHA256 verify (if --hmac)
│   │   └── throttle.h              FPS/bitrate adaptive control
│   ├── src/
│   │   ├── main.cpp                Entry point, arg parsing, startup
│   │   ├── capture.cpp
│   │   ├── encoder.cpp
│   │   ├── net_host.cpp
│   │   ├── input_inject.cpp
│   │   ├── audio_enc.cpp
│   │   ├── hmac_host.cpp
│   │   └── throttle.cpp
│   ├── build/                      CMake-generated (ignored)
│   ├── dsrd_host.exe               Output binary Windows (ignored, generated)
│   └── Makefile.bak                Old makefile reference
│
└── .git/                           Version control (GitHub remote)
```

---

## Build Prerequisites

| Node       | Toolchain / SDK                                    | Notes                                                   |
|------------|----------------------------------------------------|---------------------------------------------------------|
| **DS**     | BlocksDS (latest)                                  | Includes libnds, dswifi, libcalico, libmbedtls         |
| **Wii**    | devkitPPC r29+, libogc 2.x, libfat, libbte         | Real WD backend via `/dev/net/wd/command` IOS v21     |
| **PC**     | MSVC 2022+ (Windows) or GCC 12+ (Linux)            | CMake 3.20+, SDL2 (optional), liblzo2, vJoy SDK        |

---

## Quick Build

### All Three Targets

```bash
# From workspace root (C:\sourcecode\dsdesktop\ or ~/dsdesktop/)

# DS Client (outputs: ds_client/dsremote.nds)
cd ds_client && make

# Wii Proxy (outputs: wii_proxy/wii_proxy.dol)
cd wii_proxy && make

# PC Host (Windows: CMake + MSVC; Linux: CMake + GCC)
cd pc_host
cmake -S . -B build -G "Visual Studio 18 2026" -A x64  # Windows
cmake --build build --config Release
# OR
cmake -S . -B build                                      # Linux
cmake --build build --config Release
```

**Output binaries:**
- `ds_client/dsremote.nds` — DS ROM (294 KB typical)
- `wii_proxy/wii_proxy.dol` — Wii executable (380 KB typical)
- `pc_host/build/Release/dsrd_host.exe` — PC host (20 KB minimal, dynamically linked)

---

## Deployment

### Step 1: Wii Setup

1. Format SD card (FAT32).
2. Create: `sd:/apps/dsremote/` directory.
3. Choose **one** DS boot method:
   - **Flashcart / modded DS path** *(recommended if available)*:
     - Copy `ds_client/dsremote.nds` to your `R4`/flashcart microSD, or launch it via your existing DS homebrew method.
     - In this mode, **`haxxstation.nds` is not required**.
   - **Download Play bootstrap path** *(for unmodded DS workflows)*:
     - Supply your own externally obtained `haxxstation.nds` and place it at `sd:/apps/dsremote/haxxstation.nds`.
4. ~~Create config file~~ **Skip this step** — the Wii will auto-configure on first boot!
5. Copy `wii_proxy/wii_proxy.dol` to `sd:/apps/dsremote/boot.dol`.
6. Launch from Homebrew Channel.

**First Boot: Setup Wizard**

On first boot (when no `proxy.cfg` exists), the Wii will automatically display a setup wizard:

```
╔════════════════════════════════════════════════╗
║   DS Remote Desktop — First Boot Setup         ║
╚════════════════════════════════════════════════╝

Welcome! Let's configure your Wii proxy.
This wizard will help you set up:
  1. Backhaul mode (USB or Wi-Fi)
  2. PC host IP address
  3. Wi-Fi channel settings
```

**Step-by-step:**
1. Select backhaul mode (USB Ethernet recommended, or Wi-Fi only)
2. Enter PC IP address using Wiimote controls (↑↓ to adjust digits, ←→ to select octet)
3. Choose channel calibration (auto or fixed)
4. Review and confirm settings
5. Configuration automatically saved to SD card

The wizard takes ~30 seconds and eliminates manual config file editing entirely.

**Expected startup output (after wizard):**
```
=== DS Remote Desktop — Wii Proxy ===
Initialising backhaul (USB Ethernet)...
Backhaul ready. IP = 192.168.1.50
Initialising NiFi radio...
NiFi TX ready (ch 1)
[CAL] Running channel latency benchmark...
[CAL] ch 1: sent=18 recv=18 loss=0.0% med=2.45ms p95=3.21ms score=...
[CAL] ch 6: sent=18 recv=18 loss=0.0% med=2.51ms p95=3.18ms score=...
[CAL] ch 11: sent=18 recv=18 loss=0.0% med=2.88ms p95=3.45ms score=...
[CAL] Best channel selected: 1
Proxy active. Listening on UDP 17394...
Press SELECT to manage PC IPs
Press HOME to exit
```

### Step 2: DS Boot / Payload

Use the method that matches your hardware:

1. **R4 / flashcart / modded DS**
   - Copy `ds_client/dsremote.nds` to the DS storage device.
   - Launch `dsremote.nds` directly from the flashcart or homebrew menu.
   - No `haxxstation.nds` or Download Play bootstrap is needed.

2. **Unmodded DS using Download Play bootstrap**
   - Use your preferred NiFi sender (or FIX94's `wii-ds-rom-sender`).
   - Select `ds_client/dsremote.nds` as payload.
   - Trigger the exploit/bootstrap flow using your externally supplied `haxxstation.nds`.
   - Wait ~10–30 seconds for Wii to push code to DS RAM.
   - DS boots into the remote-desktop client.

### Step 3: PC Host

```bash
# Windows (PowerShell)
cd pc_host\build\Release
.\dsrd_host.exe --wii 192.168.1.50 --fps 30 --audio --hmac

# Linux
cd pc_host/build/Release
./dsrd_host --wii 192.168.1.50 --fps 30 --audio --hmac
```

**Arguments:**
- `--wii <IP>` — Wii proxy IP (required).
- `--fps <N>` — Target framerate (default 30; auto-throttles to 15 on congestion).
- `--audio` — Enable audio streaming (default off; saves ~2 Mbps).
- `--hmac` — Require HMAC authentication (default off; use on untrusted networks).

**Expected startup:**
```
PC Host connecting to Wii 192.168.1.50...
Connected. Starting capture...
DS is online.
Video: 256×192, 8-bit paletted, delta-compressed, keyframes every 60 frames.
Audio: disabled (--audio to enable).
HMAC: enabled.
Starting stream at 30 FPS...
```

---

## Runtime Features & Toggles

### On DS (in-game menu)

- **Audio toggle** — Press SELECT to open menu, toggle audio on/off.
- **On-screen keyboard** — Stylus-driven input device mapping.
- **HMAC toggle** — Press a combo (configurable) to enable/disable auth.

### On PC Host

- `--fps <N>` — Dynamic throttle based on DS congestion feedback.
- `--audio` — ADPCM stereo to DS DAC.
- `--hmac` — Packet-level authentication.

### On Wii (Runtime)

- **SELECT button** — Open IP manager menu to switch PC IP without reboot.
- **First boot** — Automatic setup wizard (backhaul mode, PC IP, channel calibration).
- Re-run wizard: Delete `proxy.cfg` from SD card and reboot.

### Wii Configuration File (Automatic)

The wizard generates `sd:/apps/dsremote/proxy.cfg` with:
- `mode` — `usb` (Ethernet, low latency) or `wifi` (context-switching penalty).
- `auto_channel` — 1 = benchmark startup, 0 = use fixed channel.
- `pc_ip` — PC host IP (set by wizard).
- `pc_port` — UDP port (default 17394).

---

## Technical Highlights

### Channel Calibration

At Wii startup, if `auto_channel=1`:
1. NiFi radio cycles through candidate channels (default 1, 6, 11).
2. Sends ping probes; DS replies with pong.
3. Measures RTT (median, p95) and packet loss per channel.
4. Scores each: `(p95×0.5) + (median×0.3) + (loss×0.2)`.
5. Locks best channel before normal operation.
6. **Timing:** ACK spoof runs during this phase, so 4ms DS link is preserved.

### Video Compression

- **Capture** → Downscale to 256×192 → Median-cut palette quantize (256 colors, RGB555) → RLE compress.
- **Streaming** → Keyframes every 60 frames (full RLE). Deltas = list of changed pixels (x, y, color).
- **Auto-fallback** → If >60% of screen changed, send keyframe instead (bandwidth win).
- **Loss recovery** → Keyframes periodically "clean" any delta corruption from dropped packets.

### Audio Streaming

- **Codec** → IMA-ADPCM (256:1 compression ratio, 22 kHz mono typical).
- **Buffer** → 512-sample ring buffer on ARM7 (fast recycling, no fragmentation).
- **Optional** → Off by default (conserves bandwidth and CPU for video).

### Telemetry Authentication

- **HMAC-SHA256** over 4-byte button payload + 4-byte sequence.
- **Zero encryption** on video (massive bandwidth cost unjustified).
- **Overhead** → Negligible (<1% CPU on 67 MHz ARM9).

### ACK Spoofing (Wii ↔ DS)

- **Problem solved** → DS NiFi requires hardware 4ms CMD/ACK timing; PC backhaul is jittery.
- **Solution** → Wii spawns tight IRQ-level thread that locally answers DS timing requests.
- **Benefit** → PC can be arbitrarily slow; DS sees perfect low-latency handshakes from Wii.

---

## Operational Best Practices

### Mode Selection

| Scenario | Recommendation |
|----------|---|
| **Controlled LAN** (5 GHz available, USB 3.0 Ethernet) | Mode B (USB) + auto_channel + all features enabled |
| **Wi-Fi only, good RSSI** (strong 2.4 GHz, no interference) | Mode A (Wi-Fi) + channels={1,6,11} only + disable audio |
| **Noisy RF environment** | Mode B + single fixed channel + reduce probe_count |
| **Untrusted network** | Enable --hmac; disable audio/video during sensitive use |

### Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| **DS doesn't join network** | Wrong channel, NiFi not in promiscuous, RSA failure | Verify Haxxstation; check Wii startup logs |
| **DS screen frozen** | Video RX buffer overflow, missed keyframe | Reduce `--fps`, disable audio, check channel |
| **Input lag** | Calibration chose poor channel, PC backhaul saturated | Re-run calibration, use USB Ethernet |
| **Audio stutters** | ARM7 ↔ ARM9 sync issue, FIFO underrun | Disable audio, or reduce FPS to 15 |
| **Dropped telemetry** | HMAC seq out-of-order (replay attack attempt) | Check clocks, restart PC/DS sync |

---

## Repository

- **GitHub:** https://github.com/FoggyGoofball/dsdesktop
- **Commits:** 
  - Initial hardware-ready tri-node implementation with channel calibration
  - Clean repository: remove build artifacts and add .gitignore
  - Fix .gitignore to exclude binaries and build outputs
  - Strengthen ignore rules for generated build artifacts

---

## License

MIT. See LICENSE file.

---

## Contributing

Before submitting PRs:
1. Ensure all three targets build cleanly: `DS_OK`, `WII_OK`, `PC_OK`.
2. No warnings on any platform.
3. Test on actual hardware (or emulator) before commit.
4. Keep protocol structs packed and 1-byte-aligned.
5. Maintain circular-buffer safety: no dynamic allocation during RX.

---

## Acknowledgments

- **FIX94** — wii-ds-rom-sender, Download Play exploitation.
- **jpenny1993** — dsnifi template, event-driven NiFi abstractions.
- **BlocksDS** / **devkitPro** — Modern DS/Wii toolchains.
- **calico** — ARM7/ARM9 wireless stack on BlocksDS.

---

## External Prerequisite (Not Included)

`haxxstation.nds` is **not included** in this repository or release bundle.

Reason: it is derived from copyrighted Nintendo distribution content.
This project only provides original code and tooling.

You must provide your own legally obtained input and produce/integrate the
required file in your existing DS payload sender flow.

Expected location on SD card:

```text
sd:/apps/dsremote/haxxstation.nds
```

Important:
- If you use an **R4**, another **DS flashcart**, or a **modded DS** that can launch homebrew directly, you can ignore this requirement and run `dsremote.nds` directly.
- `haxxstation.nds` is only needed for **Download Play bootstrap workflows** on otherwise unmodded DS hardware.

At runtime, Wii now performs a prerequisite check:
- if file exists: startup continues normally
- if missing: a clear on-screen instruction panel is shown
  - `A` continue anyway (for `R4` / flashcart / modded DS flows)
  - `B` exit and install prerequisite first
