#!/usr/bin/env bash
#
# Copyright (c) 2026 Ed Coffey
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEST_ROOT="${PROJECT_ROOT}/toolchain"
TOOLCHAIN_DIR="${DEST_ROOT}/pi32v2"
POSTBUILD_DIR="${DEST_ROOT}/post-build"
VERSION_FILE="${DEST_ROOT}/VERSION.json"

TOOLCHAIN_URL="https://pkgman.jieliapp.com/s/linux-toolchain"
POSTBUILD_URL="https://pkgman.jieliapp.com/s/linux-postbuild"

mkdir -p "$DEST_ROOT"

# Resolve redirect target URL/filename without downloading the payload
get_redirect_url() {
    local url="$1"
    (curl -sIL "$url" 2>/dev/null || true) | (grep -i "^location:" || true) | tail -n 1 | awk '{print $2}' | tr -d '\r'
}

echo "=== Jellybean-37 Toolchain Setup ==="

# Read existing versions if present
CURRENT_TOOLCHAIN_VER=""
CURRENT_POSTBUILD_VER=""
if [[ -f "$VERSION_FILE" ]]; then
    CURRENT_TOOLCHAIN_VER=$(python3 -c "import json; d=json.load(open('$VERSION_FILE')); print(d.get('toolchain', ''))" 2>/dev/null || true)
    CURRENT_POSTBUILD_VER=$(python3 -c "import json; d=json.load(open('$VERSION_FILE')); print(d.get('postbuild', ''))" 2>/dev/null || true)
fi

REMOTE_TOOLCHAIN_URL=$(get_redirect_url "$TOOLCHAIN_URL")
REMOTE_POSTBUILD_URL=$(get_redirect_url "$POSTBUILD_URL")

if [[ -z "$REMOTE_TOOLCHAIN_URL" || -z "$REMOTE_POSTBUILD_URL" ]]; then
    if [[ -d "$TOOLCHAIN_DIR" && -d "$POSTBUILD_DIR" && -f "$VERSION_FILE" ]]; then
        echo "Notice: Unable to reach remote package manager (offline/firewalled). Using existing installation:"
        echo "Toolchain directory:  $TOOLCHAIN_DIR (${CURRENT_TOOLCHAIN_VER:-unknown})"
        echo "Post-build directory: $POSTBUILD_DIR (${CURRENT_POSTBUILD_VER:-unknown})"
        exit 0
    fi
    echo "Error: Failed to resolve remote redirect endpoints and no existing installation found." >&2
    exit 1
fi

REMOTE_TOOLCHAIN_FILE=$(basename "$REMOTE_TOOLCHAIN_URL")
REMOTE_POSTBUILD_FILE=$(basename "$REMOTE_POSTBUILD_URL")

echo "Remote toolchain version:  $REMOTE_TOOLCHAIN_FILE (installed: ${CURRENT_TOOLCHAIN_VER:-none})"
echo "Remote post-build version: $REMOTE_POSTBUILD_FILE (installed: ${CURRENT_POSTBUILD_VER:-none})"

# 1. Toolchain Setup
if [[ "$REMOTE_TOOLCHAIN_FILE" != "$CURRENT_TOOLCHAIN_VER" || ! -d "$TOOLCHAIN_DIR" ]]; then
    echo "Downloading toolchain from $REMOTE_TOOLCHAIN_URL..."
    TEMP_DIR=$(mktemp -d)
    trap 'rm -rf "$TEMP_DIR"' EXIT
    curl -fSL "$REMOTE_TOOLCHAIN_URL" -o "$TEMP_DIR/$REMOTE_TOOLCHAIN_FILE"
    echo "Extracting toolchain to $DEST_ROOT..."
    tar -xf "$TEMP_DIR/$REMOTE_TOOLCHAIN_FILE" -C "$DEST_ROOT"
    
    EXTRACTED_SUBDIR=$(find "$DEST_ROOT" -maxdepth 1 -mindepth 1 -type d -name "jieli-linux-toolchains*" 2>/dev/null | head -n 1)
    if [[ -n "$EXTRACTED_SUBDIR" && -d "$EXTRACTED_SUBDIR" ]]; then
        cp -a "$EXTRACTED_SUBDIR"/* "$DEST_ROOT/"
        rm -rf "$EXTRACTED_SUBDIR"
    fi
    CURRENT_TOOLCHAIN_VER="$REMOTE_TOOLCHAIN_FILE"
else
    echo "Toolchain is up to date: $CURRENT_TOOLCHAIN_VER"
fi

# 2. Post-Build Tools Setup
if [[ "$REMOTE_POSTBUILD_FILE" != "$CURRENT_POSTBUILD_VER" || ! -d "$POSTBUILD_DIR" ]]; then
    echo "Downloading post-build tools from $REMOTE_POSTBUILD_URL..."
    TEMP_DIR_PB=$(mktemp -d)
    curl -fSL "$REMOTE_POSTBUILD_URL" -o "$TEMP_DIR_PB/$REMOTE_POSTBUILD_FILE"
    echo "Extracting post-build tools to $POSTBUILD_DIR..."
    mkdir -p "$POSTBUILD_DIR"
    tar -xf "$TEMP_DIR_PB/$REMOTE_POSTBUILD_FILE" -C "$POSTBUILD_DIR"
    rm -rf "$TEMP_DIR_PB"
    
    EXTRACTED_PB_SUBDIR=$(find "$POSTBUILD_DIR" -maxdepth 1 -mindepth 1 -type d -name "jieli-linux-post-build*" 2>/dev/null | head -n 1)
    if [[ -n "$EXTRACTED_PB_SUBDIR" && -d "$EXTRACTED_PB_SUBDIR" ]]; then
        cp -a "$EXTRACTED_PB_SUBDIR"/* "$POSTBUILD_DIR/"
        rm -rf "$EXTRACTED_PB_SUBDIR"
    fi
    CURRENT_POSTBUILD_VER="$REMOTE_POSTBUILD_FILE"
else
    echo "Post-build tools are up to date: $CURRENT_POSTBUILD_VER"
fi

# Save current version record
python3 -c "
import json
with open('$VERSION_FILE', 'w') as f:
    json.dump({'toolchain': '$CURRENT_TOOLCHAIN_VER', 'postbuild': '$CURRENT_POSTBUILD_VER'}, f, indent=2)
"

# Set executable permissions on binaries
chmod +x "$TOOLCHAIN_DIR"/bin/* 2>/dev/null || true
if [[ -d "$DEST_ROOT/common/bin" ]]; then
    chmod +x "$DEST_ROOT/common/bin"/* 2>/dev/null || true
fi
chmod +x "$POSTBUILD_DIR"/* 2>/dev/null || true

echo "=== Toolchain Setup Complete ==="
echo "Toolchain directory:  $TOOLCHAIN_DIR"
echo "Post-build directory: $POSTBUILD_DIR"
