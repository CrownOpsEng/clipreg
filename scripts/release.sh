#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO="CrownOpsEng/clipreg"
VERSION=""
APPLY=0

usage(){ cat <<'USAGE'
Usage: scripts/release.sh --version VERSION [--repo OWNER/REPO] [--apply]

Plan a beta/RC/final release from the exact protected-main commit. --apply runs
canonical checks, creates an annotated immutable tag, pushes it, and creates the
GitHub release. VERSION/CHANGELOG changes belong in a reviewed release-prep PR.
USAGE
}
while (($#)); do
  case "$1" in
    --version) shift; VERSION="${1#v}" ;;
    --repo) shift; REPO="${1:?--repo requires OWNER/REPO}" ;;
    --apply) APPLY=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "ERROR: unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done
[[ -n "$VERSION" ]] || { echo 'ERROR: --version required' >&2; exit 2; }
cd "$ROOT"
BASE="$(./scripts/version.sh --base)"
[[ "$VERSION" == "$BASE" ]] || { echo "ERROR: requested $VERSION but VERSION is $BASE" >&2; exit 1; }
TAG="v$VERSION"
[[ "$VERSION" =~ ^0\.[0-9]+\.[0-9]+(-[0-9A-Za-z-]+(\.[0-9A-Za-z-]+)*)?$ ]] || { echo 'ERROR: release VERSION must be SemVer without build metadata' >&2; exit 1; }
[[ -z "$(git status --porcelain)" ]] || { echo 'ERROR: release requires a clean worktree' >&2; exit 1; }
[[ "$(git branch --show-current)" == main ]] || { echo 'ERROR: release must run from main' >&2; exit 1; }
git fetch origin main --tags >/dev/null
[[ "$(git rev-parse HEAD)" == "$(git rev-parse origin/main)" ]] || { echo 'ERROR: local main is not exact origin/main' >&2; exit 1; }
if git rev-parse "$TAG" >/dev/null 2>&1; then echo "ERROR: tag already exists: $TAG" >&2; exit 1; fi
./scripts/check-repo.py
./scripts/changelog-section.py "$VERSION" >/dev/null

PRE=0
[[ "$VERSION" == *-* ]] && PRE=1
STATE=final
[[ "$VERSION" == *-beta.* ]] && STATE=beta
[[ "$VERSION" == *-rc.* ]] && STATE=rc

if [[ "$STATE" == rc || "$STATE" == final ]]; then
  command -v gh >/dev/null || { echo 'ERROR: gh required for RC/final blocker check' >&2; exit 1; }
  OPEN_BLOCKERS="$(gh issue list --repo "$REPO" --state open --label priority:blocker --milestone v0.2.0 --json number,title --jq 'length')"
  [[ "$OPEN_BLOCKERS" == 0 ]] || { echo "ERROR: $OPEN_BLOCKERS open v0.2.0 blocker issue(s); $STATE promotion refused" >&2; exit 1; }
fi

if [[ "$STATE" == final ]]; then
  LATEST_RC="$(git tag --list 'v0.2.0-rc.*' --sort=-v:refname | head -n1)"
  [[ -n "$LATEST_RC" ]] || { echo 'ERROR: final v0.2.0 requires a prior RC tag' >&2; exit 1; }
  BAD="$(git diff --name-only "$LATEST_RC"..HEAD -- | grep -Ev '^(VERSION|CHANGELOG\.md)$' || true)"
  [[ -z "$BAD" ]] || { echo 'ERROR: executable/target-state changes exist after latest RC; cut another RC first:' >&2; echo "$BAD" >&2; exit 1; }
fi

cat <<PLAN
Release plan
  repo:     $REPO
  commit:   $(git rev-parse HEAD)
  version:  $VERSION
  tag:      $TAG
  state:    $STATE
  github:   $([[ $PRE == 1 ]] && echo prerelease || echo release)
PLAN
((APPLY)) || { echo 'PLAN ONLY — rerun with --apply after reviewing qualification evidence.'; exit 0; }
make check

DIST="$ROOT/dist"
rm -rf "$DIST"; mkdir -p "$DIST"
./scripts/package.sh --ref HEAD --out "$DIST" >/dev/null
ASSET="$DIST/clipreg-$TAG.tar.gz"
SUM="$ASSET.sha256"
NOTES="$(mktemp)"; trap 'rm -f "$NOTES"' EXIT
./scripts/changelog-section.py "$VERSION" > "$NOTES"

git tag -a "$TAG" -m "ClipReg $TAG"
git push origin "$TAG"
args=(release create "$TAG" "$ASSET" "$SUM" --repo "$REPO" --title "ClipReg $TAG" --notes-file "$NOTES")
((PRE)) && args+=(--prerelease)
gh "${args[@]}"
echo "Published $TAG"
