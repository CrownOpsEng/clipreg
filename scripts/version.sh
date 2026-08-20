#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BASE="$(tr -d '[:space:]' < "$ROOT/VERSION")"
SEMVER_RE='^0\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?(\+[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$'

validate() {
  [[ "$BASE" =~ $SEMVER_RE ]] || {
    echo "ERROR: VERSION is not valid pre-1.0 SemVer: $BASE" >&2
    return 1
  }
}

full_version() {
  validate
  # Release/source archives intentionally have no .git directory and therefore
  # identify exactly as VERSION. Git working-tree builds add provenance without
  # mutating the release-line version for every PR commit.
  if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    local sha dirty exact
    sha="$(git -C "$ROOT" rev-parse --short=12 HEAD)"
    dirty=''
    git -C "$ROOT" diff --quiet --ignore-submodules -- 2>/dev/null || dirty='.dirty'
    git -C "$ROOT" diff --cached --quiet --ignore-submodules -- 2>/dev/null || dirty='.dirty'
    exact="$(git -C "$ROOT" describe --exact-match --tags --match "v$BASE" HEAD 2>/dev/null || true)"
    if [[ "$exact" == "v$BASE" && -z "$dirty" ]]; then
      printf '%s\n' "$BASE"
    else
      # Build metadata is ignored for SemVer precedence but uniquely identifies
      # unreleased PR/main builds from the same release line.
      printf '%s+g%s%s\n' "${BASE%%+*}" "$sha" "$dirty"
    fi
  else
    printf '%s\n' "$BASE"
  fi
}

case "${1:-}" in
  --base) validate; printf '%s\n' "$BASE" ;;
  --validate) validate ;;
  --tag)
    validate
    printf 'v%s\n' "$BASE"
    ;;
  ''|--full) full_version ;;
  *) echo "Usage: scripts/version.sh [--base|--full|--validate|--tag]" >&2; exit 2 ;;
esac
