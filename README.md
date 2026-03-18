# DS Remote Desktop — Tri-Node NiFi Streaming Stack

A remote-desktop and controller relay bridging a **PC**, a **Nintendo Wii**,
and a **Nintendo DS** over the NiFi ad-hoc 802.11b protocol.

## Directory Layout

```
ds-remote-desktop/
├── common/                 Shared protocol definitions
│   ├── protocol.h
│   └── rle.h
├── ds_client/              Nintendo DS client  (BlocksDS / libnds / dswifi)
│   ├── arm9/
│   │   └── source/
│   ├── arm7/
│   │   └── source/
│   └── Makefile
├── wii_proxy/              Wii protocol proxy  (libogc / devkitPPC)
│   ├── source/
│   └── Makefile
├── pc_host/                PC host server       (C++ / SDL2 / cross-platform)
│   ├── src/
│   ├── include/
│   └── Makefile
└── README.md
```

## Build Prerequisites

| Node       | Toolchain                                         |
|------------|----------------------------------------------------|
| DS Client  | BlocksDS (latest), libnds, dswifi, libmbedtls      |
| Wii Proxy  | devkitPPC r44+, libogc 2.x, libfat, libbte         |
| PC Host    | GCC 12+ or MSVC 2022+, SDL2, liblzo2, vJoy SDK     |

## Quick Build

```bash
# DS Client
cd ds_client && make

# Wii Proxy
cd wii_proxy && make

# PC Host
cd pc_host && make          # Linux
cd pc_host && make OS=WIN   # Windows (MinGW / MSYS2)
```

## Deployment

1. Place `haxxstation.nds` (renamed *DS Download Station – Vol 1*) on the
   Wii's SD card at `sd:/apps/dsremote/haxxstation.nds`.
2. The Wii homebrew compresses the DS client `.nds` with LZO and pushes it
   to the DS via Download Play.
3. Launch the PC host, point it at the Wii's IP, and begin streaming.

## Runtime Toggles

* **Audio streaming** – DS client menu or PC host flag (`--audio`)
* **HMAC authentication** – DS client menu or PC host flag (`--hmac`)
* **Wii backhaul mode** – Wii on-screen menu: *Wi-Fi Only* / *USB Ethernet*
* **Framerate** – Auto-throttled; PC host monitors DS congestion telemetry
