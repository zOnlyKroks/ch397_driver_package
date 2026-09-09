#!/usr/bin/env bash
# Easy installer for the ch397 Linux driver: builds the module, and
# registers it with DKMS so it survives kernel upgrades. Falls back to a
# plain `make install` if DKMS isn't available.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
DRIVER_DIR="$REPO_ROOT/linux/driver"
DKMS_SRC_DIR="$SCRIPT_DIR/ch397-dkms"
MODULE_NAME="ch397"
MODULE_VERSION="$(grep -m1 'define VERSION_DESC' "$DRIVER_DIR/ch397.c" | sed -E 's/.*"V([0-9]+\.[0-9]+).*/\1/')"

if [[ "$EUID" -ne 0 ]]; then
    echo "This installer needs root (it loads/installs a kernel module). Re-run with sudo." >&2
    exit 1
fi

if ! command -v dkms >/dev/null 2>&1; then
    echo "dkms not found; falling back to a one-off 'make install' (won't survive kernel upgrades)." >&2
    make -C "$DRIVER_DIR" install
    echo "Installed. Run 'sudo make -C $DRIVER_DIR uninstall' to remove."
    exit 0
fi

DKMS_TREE="/usr/src/${MODULE_NAME}-${MODULE_VERSION}"
echo "Installing via DKMS as ${MODULE_NAME}/${MODULE_VERSION} ..."

rm -rf "$DKMS_TREE"
mkdir -p "$DKMS_TREE"
cp "$DRIVER_DIR/ch397.c" "$DKMS_TREE/"
cp "$DRIVER_DIR/Makefile" "$DKMS_TREE/"
sed "s/@MODULE_VERSION@/${MODULE_VERSION}/" "$DKMS_SRC_DIR/dkms.conf.in" > "$DKMS_TREE/dkms.conf"

dkms remove -m "$MODULE_NAME" -v "$MODULE_VERSION" --all >/dev/null 2>&1 || true
dkms add -m "$MODULE_NAME" -v "$MODULE_VERSION"
dkms build -m "$MODULE_NAME" -v "$MODULE_VERSION"
dkms install -m "$MODULE_NAME" -v "$MODULE_VERSION"

modprobe usbnet || true
modprobe "$MODULE_NAME" || true

echo "Done. The driver will be rebuilt automatically on future kernel upgrades."
echo "To remove it: sudo dkms remove -m ${MODULE_NAME} -v ${MODULE_VERSION} --all"
