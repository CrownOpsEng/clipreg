#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REF=HEAD
OUT="$ROOT/dist"
while (($#)); do
  case "$1" in
    --ref) shift; REF="${1:?--ref requires REF}" ;;
    --out) shift; OUT="${1:?--out requires DIR}" ;;
    -h|--help) echo 'Usage: scripts/package.sh [--ref REF] [--out DIR]'; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
command -v git >/dev/null
VERSION="$(git -C "$ROOT" show "$REF:VERSION" | tr -d '[:space:]')"
[[ "$VERSION" =~ ^0\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]] || {
  echo "ERROR: invalid VERSION at $REF: $VERSION" >&2; exit 1;
}
mkdir -p "$OUT"
ASSET="clipreg-v$VERSION.tar.gz"
git -C "$ROOT" archive --format=tar.gz --prefix="clipreg-v$VERSION/" -o "$OUT/$ASSET" "$REF"
( cd "$OUT" && sha256sum "$ASSET" > "$ASSET.sha256" )
printf '%s\n' "$OUT/$ASSET"
