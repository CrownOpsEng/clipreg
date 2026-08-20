#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-only
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bash -n "$ROOT/bin/clipreg"
bash -n "$ROOT/scripts/clipreg-keybinds"
bash -n "$ROOT/scripts/install.sh"
bash -n "$ROOT/scripts/uninstall.sh"
[[ "$($ROOT/bin/clipreg --version)" == "0.1.0" ]]
echo 'ClipReg v0.1.0 syntax/version tests: PASS'
