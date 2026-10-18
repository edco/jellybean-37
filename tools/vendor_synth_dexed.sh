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

DEST_DIR="${PROJECT_ROOT}/external/synth_dexed_msfa"
VERSION_FILE="$DEST_DIR/UPSTREAM_VERSION"
REPO_URL="https://codeberg.org/dcoredump/Synth_Dexed.git"
BRANCH="dev"
PATCH_FILE="${SCRIPT_DIR}/patches/synth_dexed_msfa.patch"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    echo "Usage: $0 [--save-patch] [--force]"
    echo "  --save-patch  Export diff in external/synth_dexed_msfa to $PATCH_FILE"
    echo "  --force       Re-vendor and re-apply patches even if upstream hash matches"
    echo "  (default)     Fetch upstream, vendor files, and apply local patch"
    exit 0
fi

if [[ "${1:-}" == "--save-patch" ]]; then
    echo "Generating patch against upstream..."
    TEMP_DIR=$(mktemp -d)
    trap 'rm -rf "$TEMP_DIR"' EXIT

    UPSTREAM_DIR="$TEMP_DIR/a/external/synth_dexed_msfa"
    LOCAL_DIR="$TEMP_DIR/b/external/synth_dexed_msfa"
    CLONE_DIR="$TEMP_DIR/upstream_repo"
    CLEAN_ALL="$TEMP_DIR/clean_all"

    mkdir -p "$UPSTREAM_DIR" "$LOCAL_DIR" "$CLEAN_ALL"

    UPSTREAM_TARGET="$BRANCH"
    if [[ -f "$VERSION_FILE" ]]; then
        UPSTREAM_TARGET="$(cat "$VERSION_FILE")"
    fi

    echo "Attempting to fetch upstream at ${UPSTREAM_TARGET} from $REPO_URL..."
    if git clone --depth 1 --branch "$BRANCH" "$REPO_URL" "$CLONE_DIR" 2>/dev/null; then
        if [[ "$UPSTREAM_TARGET" != "$BRANCH" ]]; then
            git -C "$CLONE_DIR" checkout -q "$UPSTREAM_TARGET" 2>/dev/null || true
        fi
        grep -rl "Apache License, Version 2.0" "$CLONE_DIR/src" | while read -r file; do
            cp "$file" "$CLEAN_ALL/"
        done
        cp "$CLONE_DIR/src/dexed.h" "$CLEAN_ALL/"
        cp "$CLONE_DIR/src/dexed.cpp" "$CLEAN_ALL/"
        cp "$CLONE_DIR/src/int_math.h" "$CLEAN_ALL/"
        cp "$CLONE_DIR/examples/Banks/banks.h" "$CLEAN_ALL/"
    else
        echo "Notice: Could not reach $REPO_URL (offline/firewalled). Extracting pristine baseline from git history..."
        BASE_COMMIT=$(git -C "$PROJECT_ROOT" log --format="%H" -S "$UPSTREAM_TARGET" -n 1 -- "external/synth_dexed_msfa/UPSTREAM_VERSION" 2>/dev/null || true)
        if [[ -z "$BASE_COMMIT" ]]; then
            BASE_COMMIT=$(git -C "$PROJECT_ROOT" log --format="%H" --diff-filter=A -n 1 -- "external/synth_dexed_msfa/UPSTREAM_VERSION" 2>/dev/null || true)
        fi
        if [[ -n "$BASE_COMMIT" ]]; then
            git -C "$PROJECT_ROOT" archive "$BASE_COMMIT" "external/synth_dexed_msfa" | tar -x -C "$CLEAN_ALL" --strip-components=2
        else
            echo "Error: Could not reach upstream remote and found no baseline in git history." >&2
            exit 1
        fi
    fi

    # Compare each file against upstream and generate a clean git-compatible patch
    mkdir -p "${SCRIPT_DIR}/patches"
    : > "$PATCH_FILE"
    for file in "$DEST_DIR"/*; do
        fname=$(basename "$file")
        if [[ "$fname" == "UPSTREAM_VERSION" ]]; then
            continue
        fi
        if [[ -f "$CLEAN_ALL/$fname" ]] && ! cmp -s "$CLEAN_ALL/$fname" "$file"; then
            diff -u --label "a/external/synth_dexed_msfa/$fname" --label "b/external/synth_dexed_msfa/$fname" "$CLEAN_ALL/$fname" "$file" >> "$PATCH_FILE" || true
        fi
    done

    echo "Saved patch against upstream to $PATCH_FILE"
    exit 0
fi

mkdir -p "$DEST_DIR"

# 1. Query remote commit hash
REMOTE_HASH=$( (git ls-remote "$REPO_URL" "refs/heads/$BRANCH" 2>/dev/null || true) | awk '{print $1}' )
if [[ -z "$REMOTE_HASH" ]]; then
    if [[ -f "$VERSION_FILE" ]]; then
        echo "Notice: Could not reach $REPO_URL (offline/firewalled). Using vendored commit: $(cat "$VERSION_FILE")"
        exit 0
    fi
    echo "Error: Could not resolve branch '$BRANCH' on $REPO_URL and no vendored version found." >&2
    exit 1
fi

# 2. Check against recorded hash
if [[ -f "$VERSION_FILE" && "${1:-}" != "--force" ]]; then
    LOCAL_HASH=$(cat "$VERSION_FILE")
    if [[ "$LOCAL_HASH" == "$REMOTE_HASH" ]]; then
        echo "Already up to date at commit $LOCAL_HASH."
        exit 0
    fi
fi

echo "New commit found: $REMOTE_HASH (was: ${LOCAL_HASH:-none})"

# 3. Clone when update needed
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$TEMP_DIR"' EXIT

git clone --depth 1 --branch "$BRANCH" "$REPO_URL" "$TEMP_DIR"

# 4. Copy Apache-licensed files, plus dexed engine wrapper and banks.h
grep -rl "Apache License, Version 2.0" "$TEMP_DIR/src" | while read -r file; do
    cp "$file" "$DEST_DIR/"
done

cp "$TEMP_DIR/src/dexed.h" "$DEST_DIR/"
cp "$TEMP_DIR/src/dexed.cpp" "$DEST_DIR/"
cp "$TEMP_DIR/src/int_math.h" "$DEST_DIR/"
cp "$TEMP_DIR/src/EngineMkI.cpp" "$DEST_DIR/"
cp "$TEMP_DIR/src/EngineOpl.h" "$DEST_DIR/"
cp "$TEMP_DIR/src/EngineOpl.cpp" "$DEST_DIR/"
cp "$TEMP_DIR/examples/Banks/banks.h" "$DEST_DIR/"

# 5. Apply local patch if present
if [[ -f "$PATCH_FILE" ]]; then
    echo "Applying local patch from $PATCH_FILE..."
    if git -C "$PROJECT_ROOT" apply --check "$PATCH_FILE" 2>/dev/null; then
        git -C "$PROJECT_ROOT" apply "$PATCH_FILE"
        echo "Successfully applied local patch."
    else
        echo "Warning: Local patch failed to apply cleanly to upstream commit $REMOTE_HASH." >&2
        echo "Attempting 3-way merge..." >&2
        git -C "$PROJECT_ROOT" apply --3way "$PATCH_FILE" || {
            echo "Error: Failed to apply $PATCH_FILE. Please resolve manually." >&2
            exit 1
        }
    fi
fi

# 6. Store commit hash
echo "$REMOTE_HASH" > "$VERSION_FILE"
echo "Updated $DEST_DIR to commit $REMOTE_HASH."
