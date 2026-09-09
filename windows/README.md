# CH397/CH398 Windows driver (x64 / ARM64)

This is an independent reimplementation of WCH's CH397/CH398 USB-to-Ethernet
vendor protocol for Windows, built as a **user-mode WinUSB client** rather
than a kernel-mode NDIS miniport. See the top-level README for why: WCH does
not publish an official ARM64 driver, and this approach works identically on
x64 and ARM64 since none of the code is architecture-specific.

The protocol itself (register map, command IDs, frame headers) is
documented once in `../docs/protocol.md` and was extracted from the
official Linux driver at `../linux/driver/ch397.c`.

## How it works

1. `driver/ch397.inf` binds the device's USB VID/PID to the in-box
   `WinUSB.sys` driver (no custom kernel driver).
2. `service/` builds `ch397svc.exe`, a Windows service that:
   - watches for CH397/CH398 device arrival/removal (`device_watch.cpp`),
   - talks to the device over WinUSB (`usb_transport.cpp`),
   - creates a Wintun virtual network adapter and bridges Ethernet frames
     between it and the USB device (`netif_bridge.cpp`).

## Current scope

Implemented: device detection (all 6 supported PIDs), MAC address read,
basic TX/RX frame bridging, multicast/promiscuous mode groundwork.

**Not yet implemented** (present in the Linux driver, deferred here):
TSO, hardware checksum offload, EEE, Wake-on-LAN, jumbo frames on CH398.
Frames are bridged one-at-a-time without USB packet aggregation, so
throughput will be lower than the Linux driver until that's added.

## Building

Requires Visual Studio 2022 (or the standalone Build Tools) with the
"Desktop development with C++" and ARM64 build tools components, and
CMake 3.20+.

```powershell
# Fetch wintun.h into service/third_party/wintun/ first — see that
# directory's README.md.

cmake -G "Visual Studio 17 2022" -A x64 -B windows/service/build-x64 windows/service
cmake --build windows/service/build-x64 --config Release

cmake -G "Visual Studio 17 2022" -A ARM64 -B windows/service/build-arm64 windows/service
cmake --build windows/service/build-arm64 --config Release
```

## Installing

Use `installer/windows/` to produce a GUI installer per architecture — see
that directory's README for details, including the driver-signing
requirement.
