#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export HOME="$TMP/home"
export XDG_STATE_HOME="$TMP/state"
mkdir -p "$HOME/.local/bin" "$XDG_STATE_HOME/clipreg"

# Missing binary must rebuild.
out="$($ROOT/scripts/install.sh --plan)"
grep -q '^rebuild=yes$' <<<"$out"
grep -q 'binary-missing' <<<"$out"

# Create a version-correct fake binary, then obtain the source fingerprint from
# the planner and seed an exactly matching build state.
cat > "$HOME/.local/bin/clipreg" <<'BIN'
#!/usr/bin/env bash
[[ "${1:-}" == "--version" ]] && { echo 0.2.0-dev.2; exit 0; }
exit 1
BIN
chmod +x "$HOME/.local/bin/clipreg"
out="$($ROOT/scripts/install.sh --plan)"
source_hash="$(awk -F= '$1=="source_sha256"{print $2}' <<<"$out")"
binary_hash="$(sha256sum "$HOME/.local/bin/clipreg" | awk '{print $1}')"
cat > "$XDG_STATE_HOME/clipreg/build-state" <<STATE
version=0.2.0-dev.2
source_sha256=$source_hash
binary_sha256=$binary_hash
STATE
out="$($ROOT/scripts/install.sh --plan)"
grep -q '^rebuild=no$' <<<"$out"

# Binary tampering and an explicit rebuild both force compilation.
printf '\n# changed\n' >> "$HOME/.local/bin/clipreg"
out="$($ROOT/scripts/install.sh --plan)"
grep -q '^rebuild=yes$' <<<"$out"
grep -q 'binary-hash-mismatch' <<<"$out"
out="$($ROOT/scripts/install.sh --plan --rebuild)"
grep -q '^rebuild=yes$' <<<"$out"
grep -q 'forced' <<<"$out"

echo 'install rebuild-plan tests: PASS'
