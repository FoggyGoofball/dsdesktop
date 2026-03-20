# DS Remote Desktop - Noobs Guide

This guide assumes you want the simplest working setup:

- a Windows PC
- a Wii with Homebrew Channel
- a Nintendo DS
- both Wii and PC on the same home network

## What each device does

- `PC Host` captures your desktop and receives controller input
- `Wii Proxy` bridges between your home network and the DS wireless link
- `DS Client` shows the video and sends touch/button input

## Before you start

You need:

1. a Wii with Homebrew Channel
2. an SD card for the Wii
3. a built `Wii` app: `wii_proxy.dol`
4. a built `DS` app: `dsremote.nds`
5. a built `PC` app: `dsrd_host.exe`
6. your PC and Wii connected to the same network

## Step 1 - Find your PC IP address

On Windows:

1. Press `Win + R`
2. Type `cmd`
3. Run:
   - `ipconfig`
4. Look for your active network adapter
5. Write down the `IPv4 Address`

Example:
- `192.168.1.100`

You will use this on the Wii.

## Step 2 - Put the Wii files on the SD card

On the SD card create this folder:

- `sd:/apps/dsremote/`

Copy:

- `wii_proxy/wii_proxy.dol` -> `sd:/apps/dsremote/boot.dol`

Optional:

- `wii_proxy/proxy.cfg` -> `sd:/apps/dsremote/proxy.cfg`

If there is no config file, the Wii setup flow should guide you on first boot.

## Step 3 - Prepare the DS app

Choose one method.

### Option A - Flashcart / direct homebrew launch

1. Copy `ds_client/dsremote.nds` to your DS storage
2. Launch it from your flashcart or homebrew menu

### Option B - Download Play / wireless launch

1. Use your usual DS payload sender workflow
2. Send `ds_client/dsremote.nds`
3. Wait for the DS client to boot

## Step 4 - Start the Wii

1. Insert the SD card into the Wii
2. Open Homebrew Channel
3. Launch `DS Remote Desktop`
4. If setup appears, follow it carefully:
   - choose backhaul mode
   - enter your PC IP
   - choose auto or fixed channel
   - save and continue

If asked which backhaul mode to use:

- use `USB Ethernet` if you already have a working Wii USB Ethernet adapter
- otherwise use `Wi-Fi Only`

## Step 5 - Start the PC host

### Windows easiest method

1. Open:
   - `pc_host/build/Release/`
2. Run:
   - `dsrd_host.exe`
3. If no command-line arguments are given, the Windows setup window should appear
4. Enter:
   - Wii IP address
   - whether to enable audio
   - whether to enable HMAC
   - target FPS
5. Click `Start`

### Windows command-line method

If you prefer the terminal:

- `dsrd_host.exe --wii 192.168.1.50 --fps 30 --audio --hmac`

Replace the Wii IP with the actual IP shown by your Wii.

## Step 6 - Start the DS client

1. Boot `dsremote.nds`
2. Wait for it to connect to the Wii
3. After handshake completes, the PC desktop should appear on the DS top screen
4. The DS bottom screen becomes your input surface

## Step 7 - Basic controls

### On the DS

- `Trackpad` area controls the mouse
- `SHOW KB` opens the on-screen keyboard
- `HIDE KB` closes it
- `MAP` opens the remap menu
- `PAD` shows or hides the DS control cluster

### Remapping buttons

1. Tap `MAP`
2. Tap a DS button row like `A`, `B`, `X`, `Y`, `L`, `R`, `START`, or `SELECT`
3. In the popup choose up to two keyboard outputs
4. Use `PREV` and `NEXT` to browse more keys
5. Tap `APPLY`
6. Tap `DONE` when finished

## If it does not work

### No video appears on DS

Check these first:

1. PC host is running
2. Wii proxy is running
3. PC IP in Wii config is correct
4. Wii and PC are on the same network
5. DS client is actually started

### Wii cannot talk to PC

1. Re-check the PC IPv4 address
2. Make sure Windows Firewall is not blocking the PC host
3. Try disabling VPN software
4. Make sure you did not enter the router IP by mistake

### DS boots but does not connect

1. Restart all three devices in this order:
   - Wii
   - PC host
   - DS
2. Let the Wii finish setup/channel work first
3. Then start the DS client

### Controls work strangely in games

1. Open `MAP`
2. Remap the DS buttons to the game keys you actually need
3. Remember each DS button can emit up to two keys

## Recommended first test

Use something simple before trying games:

1. Start PC host
2. Start Wii proxy
3. Start DS client
4. Open Notepad on the PC
5. Use the DS trackpad to move the mouse
6. Open the DS keyboard
7. Type a few letters
8. Open `MAP`
9. Map `A` to `SPACE`
10. Press `A` and confirm the input arrives on the PC

## Best practice

When testing:

1. change one thing at a time
2. test after every change
3. if something breaks, restart in this order:
   - Wii
   - PC host
   - DS

## Short version

If you want the absolute shortest checklist:

1. put `boot.dol` on Wii SD card
2. boot Wii app
3. enter your PC IP in Wii setup
4. run `dsrd_host.exe` on the PC
5. boot `dsremote.nds` on the DS
6. wait for connection
7. use `MAP` to customize controls
