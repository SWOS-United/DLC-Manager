#!/usr/bin/env bash
# Downloads the prebuilt Slint C++ SDK into third_party/slint.
#
# The prebuilt package is used so the build needs no Rust toolchain. If your
# platform has no prebuilt (notably macOS on Intel), install Rust and build
# Slint from source instead -- see docs/BUILDING.md.
set -euo pipefail

SLINT_VERSION="${SLINT_VERSION:-1.17.1}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/third_party/slint"

case "$(uname -s)" in
    Linux)  os="Linux" ;;
    Darwin) os="Darwin" ;;
    *)      echo "Unsupported OS: $(uname -s). On Windows run scripts/fetch_slint.ps1." >&2
            exit 1 ;;
esac

case "$(uname -m)" in
    x86_64|amd64) arch="x86_64" ;;
    arm64|aarch64) arch="arm64" ;;
    armv7l)       arch="armhf" ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 1 ;;
esac

# The Linux packages are named x86_64/arm64/armhf; macOS ships arm64 only.
if [ "$os" = "Darwin" ] && [ "$arch" != "arm64" ]; then
    echo "No prebuilt Slint for macOS $arch -- build from source (see docs/BUILDING.md)." >&2
    exit 1
fi

archive="Slint-cpp-${SLINT_VERSION}-${os}-${arch}.tar.gz"
url="https://github.com/slint-ui/slint/releases/download/v${SLINT_VERSION}/${archive}"

echo "Fetching $url"
mkdir -p "$DEST"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl --fail --location --progress-bar -o "$tmp/$archive" "$url"
rm -rf "${DEST:?}/"*
tar -xzf "$tmp/$archive" -C "$DEST" --strip-components=1

echo "Slint $SLINT_VERSION installed in $DEST"
