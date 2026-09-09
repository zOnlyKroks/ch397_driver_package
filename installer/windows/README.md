# Windows GUI installer

Builds a per-architecture GUI installer (`ch397-driver-setup-x64.exe`,
`ch397-driver-setup-arm64.exe`) using [Inno Setup](https://jrsoftware.org/isinfo.php).

## Prerequisites

1. Build `ch397svc.exe` for the target architecture (see `../../windows/README.md`),
   producing `windows/service/build-<arch>/Release/ch397svc.exe`.
2. Place the matching `wintun.dll` at `windows/service/third_party/wintun/<arch>/wintun.dll`
   (from https://www.wintun.net).
3. Install Inno Setup (`iscc` on PATH).

## Build

```powershell
iscc /DAPP_ARCH=x64   ch397-installer.iss
iscc /DAPP_ARCH=arm64 ch397-installer.iss
```

## Driver signing

`pnputil /add-driver` requires `ch397.inf`'s catalog (`ch397.cat`) to be
signed, or the target machine to have test-signing/unsigned-driver
installation enabled. Options, roughly in order of how "real" the release
is:

- **EV code-signing certificate** + Microsoft attestation signing — needed
  for a driver meant to install silently on unmodified end-user machines.
- **Test-signing** (`bcdedit /set testsigning on` + a self-signed cert via
  `signtool`/`Inf2Cat`) — fine for internal/development use, but end users
  will see a reboot + "test mode" watermark.

This repo does not include a signing step or certificate; wire one in here
once you've decided which path fits the release.
