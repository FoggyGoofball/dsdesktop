# Wii Homebrew Channel Setup Guide

## Quick Reference

SD Card Final Layout:
\\\
sd:/apps/dsremote/
+-- boot.dol         (wii_proxy.dol renamed)
+-- meta.xml         (Homebrew Channel metadata - REQUIRED)
+-- proxy.cfg        (Wii configuration - REQUIRED)
+-- icon.png         (optional: 48×48 RGBA icon)
+-- banner.png       (optional: 192×64 RGBA banner)
\\\

## Deployment Steps

### 1. Create Directory
On SD card: Create folder \sd:/apps/dsremote/\

### 2. Copy Required Files
- \wii_proxy/wii_proxy.dol\ ? \sd:/apps/dsremote/boot.dol\
- \wii_proxy/meta.xml\ ? \sd:/apps/dsremote/meta.xml\
- \wii_proxy/proxy.cfg\ ? \sd:/apps/dsremote/proxy.cfg\

### 3. Configure proxy.cfg
Edit \sd:/apps/dsremote/proxy.cfg\:
\\\ini
pc_ip=192.168.1.100         # Change to your PC host IP
mode=usb                    # usb (recommended) or wifi
auto_channel=1              # Auto-calibrate channel (1 or 0)
\\\

### 4. Insert into Wii
1. Safely eject SD from PC
2. Insert SD into Wii
3. Go to Homebrew Channel
4. Select \"DS Remote Desktop\" and press A

## Expected Console Output

\\\
[CAL] Running channel latency benchmark...
[CAL] ch 1: sent=18 recv=18 loss=0.0% med=2.45ms p95=3.21ms
[CAL] Best channel selected: 1
Proxy active. Listening on UDP 17394...
\\\

## Troubleshooting

**App doesn't appear:** Verify \meta.xml\ is in \sd:/apps/dsremote/\ and is valid XML
**App won't launch:** Rebuild: \cd wii_proxy && make clean && make\
**PC can't reach Wii:** Check PC IP in proxy.cfg matches your network
**DS won't connect:** Verify channel number, check NiFi promiscuous mode

## More Info
Full documentation: See README.md
