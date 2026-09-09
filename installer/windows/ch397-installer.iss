; Inno Setup script for the CH397/CH398 Windows driver package.
; Produces a per-architecture GUI installer that:
;   1. Installs the bundled Wintun DLL next to the service binary
;      (WintunCreateAdapter installs the Wintun driver itself on first use).
;   2. Installs windows/driver/ch397.inf (binds the device to WinUSB) via pnputil.
;   3. Installs and starts ch397svc as a Windows service.
;
; Build separately for each architecture, e.g.:
;   iscc /DAPP_ARCH=x64   ch397-installer.iss
;   iscc /DAPP_ARCH=arm64 ch397-installer.iss
;
; Requires: windows/service/build-<arch>/Release/ch397svc.exe already built,
; and windows/service/third_party/wintun/<arch>/wintun.dll present.
;
; NOTE: the ch397.inf catalog must be signed (or the target machine must
; allow unsigned/test-signed driver installs) for `pnputil /add-driver` to
; succeed silently — see windows/README.md and the top-level plan notes on
; driver signing.

#ifndef APP_ARCH
  #define APP_ARCH "x64"
#endif

#define AppName "CH397 USB Ethernet Driver"
#define AppVersion "1.0.0"
#define RepoRoot ".."  ; installer/windows -> repo root is two levels up
#define ServiceBuildDir RepoRoot + "\..\windows\service\build-" + APP_ARCH + "\Release"
#define WintunDir RepoRoot + "\..\windows\service\third_party\wintun\" + APP_ARCH
#define DriverDir RepoRoot + "\..\windows\driver"

[Setup]
AppId={{2E3B9F2C-6C7D-4A1B-9E9E-CH397DRIVER01}
AppName={#AppName}
AppVersion={#AppVersion}
DefaultDirName={autopf}\CH397Driver
DefaultGroupName=CH397 Driver
DisableProgramGroupPage=yes
OutputBaseFilename=ch397-driver-setup-{#APP_ARCH}
#if APP_ARCH == "arm64"
ArchitecturesAllowed=arm64
ArchitecturesInstallIn64BitMode=arm64
#else
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#endif
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
Uninstallable=yes

[Files]
Source: "{#ServiceBuildDir}\ch397svc.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#WintunDir}\wintun.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#DriverDir}\ch397.inf"; DestDir: "{app}\driver"; Flags: ignoreversion
Source: "{#DriverDir}\ch397.cat"; DestDir: "{app}\driver"; Flags: ignoreversion skipifsourcedoesntexist

[Run]
Filename: "pnputil.exe"; Parameters: "/add-driver ""{app}\driver\ch397.inf"" /install"; \
    Flags: runhidden waituntilterminated; StatusMsg: "Installing WinUSB device driver..."
Filename: "{app}\ch397svc.exe"; Parameters: "--install"; \
    Flags: runhidden waituntilterminated; StatusMsg: "Registering ch397svc service..."
Filename: "net.exe"; Parameters: "start ch397svc"; \
    Flags: runhidden waituntilterminated; StatusMsg: "Starting service..."

[UninstallRun]
Filename: "net.exe"; Parameters: "stop ch397svc"; Flags: runhidden waituntilterminated
Filename: "{app}\ch397svc.exe"; Parameters: "--uninstall"; Flags: runhidden waituntilterminated
