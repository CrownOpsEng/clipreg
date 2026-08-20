#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
BUILD_VERSION="$($ROOT/scripts/version.sh --full)"
VERSION_BASE="$($ROOT/scripts/version.sh --base)"

printf '[1/12] shell/python syntax\n'
while IFS= read -r -d '' script; do bash -n "$script"; done < <(find "$ROOT" -maxdepth 4 -type f \( -name '*.sh' -o -path '*/scripts/clipreg-keybinds' \) -print0)
python3 -m py_compile "$ROOT/scripts/migrate-v1.py" "$ROOT/scripts/check-repo.py"

printf '[2/12] version identity\n'
"$ROOT/tests/test-version.sh"
printf '[3/12] repository/version policy\n'
python3 "$ROOT/scripts/check-repo.py"

printf '[4/12] config/protocol syntax\n'
python3 - "$ROOT" <<'PY'
from pathlib import Path
import sys, tomllib, xml.etree.ElementTree as ET
root=Path(sys.argv[1])
with (root/'config/keymap.toml').open('rb') as f: data=tomllib.load(f)
assert data.get('version') == 1
assert len(data.get('binding', [])) >= 22
for p in (root/'protocol').glob('*.xml'): ET.parse(p)
print('config/protocol syntax: PASS')
PY

printf '[5/12] keybinding generator\n'
"$ROOT/tests/test-keybinds.sh"
printf '[6/12] v0.1.x CopyQ register migration\n'
"$ROOT/tests/test-migration.sh"
printf '[7/12] strict native compile (offline protocol stubs)\n'
CC="${CC:-cc}"
COMMON=(-std=c11 -g -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wconversion -Werror -I"$ROOT/tests/stubs" "-DCLIPREG_VERSION=\"$BUILD_VERSION\"")
"$CC" "${COMMON[@]}" -O2 "$ROOT/src/clipreg.c" "$ROOT/tests/stubs/stub_impl.c" -o "$TMP/clipreg"
printf '[8/12] runtime lifecycle regressions\n'
"$CC" "${COMMON[@]}" -O2 "$ROOT/tests/test-runtime.c" "$ROOT/tests/stubs/stub_impl.c" -o "$TMP/test-runtime"
"$TMP/test-runtime"
printf '[9/12] binary self-test\n'
"$TMP/clipreg" --selftest
[[ "$("$TMP/clipreg" --version)" == "$BUILD_VERSION" ]]
printf '[10/12] client/autostart behavior\n'
CLIPREG_TEST_BINARY="$TMP/clipreg" CLIPREG_TEST_VERSION="$BUILD_VERSION" "$ROOT/tests/test-client.sh"
printf '[11/12] install rebuild planning\n'
"$ROOT/tests/test-install-plan.sh"
printf '[12/12] sanitizers (when available)\n'
if "$CC" "${COMMON[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer "$ROOT/src/clipreg.c" "$ROOT/tests/stubs/stub_impl.c" -o "$TMP/clipreg-asan" >/dev/null 2>"$TMP/sanitize.err"; then
  ASAN_OPTIONS=detect_leaks=1 "$TMP/clipreg-asan" --selftest
  "$CC" "${COMMON[@]}" -O1 -fsanitize=address,undefined -fno-omit-frame-pointer "$ROOT/tests/test-runtime.c" "$ROOT/tests/stubs/stub_impl.c" -o "$TMP/test-runtime-asan"
  ASAN_OPTIONS=detect_leaks=1 "$TMP/test-runtime-asan"
else
  echo 'sanitizers unavailable; skipped'
fi
printf 'ClipReg %s offline test suite: PASS\n' "$VERSION_BASE"
