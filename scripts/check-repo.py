#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
from __future__ import annotations

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SEMVER = re.compile(r"^0\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$")


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


version = (ROOT / "VERSION").read_text().strip()
if not SEMVER.fullmatch(version):
    fail(f"VERSION is not valid pre-1.0 SemVer: {version}")

source = (ROOT / "src/clipreg.c").read_text()
if f'CLIPREG_VERSION "{version}"' in source:
    fail("src/clipreg.c hard-codes VERSION; the build must inject it from VERSION")

for required in (
    "AGENTS.md",
    ".github/AGENTS.md",
    "src/AGENTS.md",
    "scripts/AGENTS.md",
    "docs/foundation.md",
    "docs/architecture.md",
    "docs/deployment.md",
    "docs/qualification.md",
    "docs/release.md",
    ".github/pull_request_template.md",
    ".github/workflows/ci.yml",
):
    if not (ROOT / required).is_file():
        fail(f"missing repository authority surface: {required}")

workflows = sorted((ROOT / ".github/workflows").glob("*.yml"))
permanent = [p for p in workflows if not p.name.startswith("tmp-")]
if permanent != [ROOT / ".github/workflows/ci.yml"]:
    fail("CI must be the only permanent workflow")
if any(p.name.startswith("tmp-") for p in workflows):
    fail("temporary workflow definitions must be removed before acceptance")

changelog = (ROOT / "CHANGELOG.md").read_text()
if f"## v{version}" not in changelog:
    fail(f"CHANGELOG.md has no release heading for v{version}")

# If this checkout is exactly on a version tag, it must agree with VERSION.
try:
    exact = subprocess.run(
        ["git", "-C", str(ROOT), "describe", "--exact-match", "--tags", "HEAD"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    ).stdout.strip()
except FileNotFoundError:
    exact = ""
if exact.startswith("v") and exact != f"v{version}":
    fail(f"tag/VERSION drift: tag={exact} VERSION=v{version}")

if os.environ.get("GITHUB_REF_TYPE") == "tag":
    ref = os.environ.get("GITHUB_REF_NAME", "")
    if ref != f"v{version}":
        fail(f"GitHub tag/VERSION drift: {ref} != v{version}")

# Generated outputs must never enter history.
try:
    tracked = subprocess.run(
        ["git", "-C", str(ROOT), "ls-files"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    ).stdout.splitlines()
except FileNotFoundError:
    tracked = []
for path in tracked:
    if path == "build" or path.startswith("build/") or path == "dist" or path.startswith("dist/"):
        fail(f"generated output is tracked: {path}")

print(f"repository/version policy: PASS ({version})")
