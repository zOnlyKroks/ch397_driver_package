# CH397/CH398 USB Ethernet driver package

Cross-platform driver support for WCH's CH397/CH398/CH336/CH339/CH396/CH9153
USB-to-Ethernet chips (USB VID `0x1a86`), for Linux and Windows (x64 + ARM64).

| Platform | Approach | Location |
|----------|----------|----------|
| Linux | Official vendor kernel module (`ch397.ko`) | [`linux/`](linux/) |
| Windows x64 / ARM64 | User-mode WinUSB client + Wintun virtual adapter, reimplementing the same USB vendor protocol (no official WCH ARM64 driver exists) | [`windows/`](windows/) |

The shared USB vendor protocol (register map, command IDs, frame framing)
used by both implementations is documented once in
[`docs/protocol.md`](docs/protocol.md).

## Installing

- **Linux**: `sudo installer/linux/install.sh` — builds the module and
  registers it with DKMS so it survives kernel upgrades. See
  [`linux/README.md`](linux/README.md) for manual `make`-based steps.
- **Windows**: run the GUI installer for your architecture from
  `installer/windows/` (`ch397-driver-setup-x64.exe` /
  `ch397-driver-setup-arm64.exe`). See
  [`installer/windows/README.md`](installer/windows/README.md) for how
  those are built, including the driver-signing step required before
  distributing one.

## Why Windows isn't just "the vendor driver, ARM64 build"

WCH only publishes an official Windows driver for x86/x64
(`WCHUSBNIC.EXE`); there is no official ARM64 build. Rather than depend on
an unofficial, unverifiable ARM64 binary circulating in a forum thread, the
Windows side of this repo reimplements the same USB vendor protocol as a
user-mode WinUSB client (see [`windows/README.md`](windows/README.md) for
scope and current limitations vs. the Linux driver).

## Repo layout

```
linux/        Linux kernel module (unchanged from the original vendor driver)
windows/      Windows WinUSB client + Wintun bridge service, and the INF that binds the device to WinUSB
installer/    Per-platform "easy install" packaging (DKMS script for Linux, Inno Setup project for Windows)
docs/         Shared USB protocol reference used by both platforms
```
