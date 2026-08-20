#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE="$($ROOT/scripts/version.sh --base)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/repo/scripts"
cp "$ROOT/VERSION" "$TMP/repo/VERSION"
cp "$ROOT/scripts/version.sh" "$TMP/repo/scripts/version.sh"
chmod +x "$TMP/repo/scripts/version.sh"
git -C "$TMP/repo" init -q -b main
git -C "$TMP/repo" config user.name 'ClipReg Version Test'
git -C "$TMP/repo" config user.email 'clipreg-version-test@example.invalid'
git -C "$TMP/repo" add -- VERSION scripts/version.sh
git -C "$TMP/repo" commit -qm genesis
git -C "$TMP/repo" tag -a "v$BASE" -m "test $BASE"
[[ "$($TMP/repo/scripts/version.sh --full)" == "$BASE" ]]
printf 'checkpoint\n' > "$TMP/repo/note"
git -C "$TMP/repo" add -- note
git -C "$TMP/repo" commit -qm checkpoint
UNTAGGED="$($TMP/repo/scripts/version.sh --full)"
[[ "$UNTAGGED" =~ ^${BASE//./\.}\+g[0-9a-f]{12}$ ]]
printf 'dirty\n' >> "$TMP/repo/note"
DIRTY="$($TMP/repo/scripts/version.sh --full)"
[[ "$DIRTY" =~ ^${BASE//./\.}\+g[0-9a-f]{12}\.dirty$ ]]
echo 'version identity tests: PASS'
