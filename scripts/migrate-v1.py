#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Migrate CopyQ-backed ClipReg v1 items to native ClipReg v2 register files.

The migration reads CopyQ's stored tab items through its scripting API. It does
not select/copy an item and therefore never mutates the live clipboard.
"""
from __future__ import annotations

import argparse
import base64
import binascii
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

MAGIC = b"CLPRG002"
VERSION = 2
SOURCE_COPYQ_V1 = 5
APP_ID = b"copyq-v1"
MAX_MIMES = 256
MAX_MIME_NAME = 4096
DEFAULT_MAX_PAYLOAD = 256 * 1024 * 1024
REGISTER_RE = re.compile(r"^[A-Za-z0-9._-]{1,32}$")
DONE_FILE = ".v1-copyq-migration-complete"

EXPORT_JS = r'''
var TAB = "Clipboard Registers";
var MARKER = "application/x-crownops-clipreg-key";
var COPYQ_SECRET = "application/x-copyq-secret";
var KDE_SECRET = "x-kde-passwordmanagerhint";
var tabs = tab();
var found = false;
for (var ti = 0; ti < tabs.length; ++ti) {
    if (str(tabs[ti]) === TAB) { found = true; break; }
}
if (!found) {
    print("NO_TAB\n");
} else {
    tab(TAB);
    for (var row = 0; row < size(); ++row) {
        var item = getItem(row);
        if (item[MARKER] === undefined)
            continue;
        var key = str(item[MARKER]);
        if (key.indexOf("__restore__:") === 0)
            continue;
        var secret = item[COPYQ_SECRET] !== undefined;
        for (var sf in item) {
            if (sf.toLowerCase() === KDE_SECRET) {
                try {
                    if (str(item[sf]).toLowerCase().indexOf("secret") >= 0)
                        secret = true;
                } catch (e) {}
            }
        }
        print("REGISTER\t" + toBase64(fromUnicode(key, "UTF-8")) + "\t" + (secret ? "1" : "0") + "\n");
        if (!secret) {
            for (var format in item) {
                var lower = format.toLowerCase();
                if (format === MARKER || lower.indexOf("application/x-copyq-") === 0)
                    continue;
                print("MIME\t" + toBase64(fromUnicode(format, "UTF-8")) + "\t" + toBase64(item[format]) + "\n");
            }
        }
        print("END\n");
    }
}
'''


def eprint(*args: object) -> None:
    print(*args, file=sys.stderr)


def decode64(value: str, what: str) -> bytes:
    try:
        return base64.b64decode(value, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise RuntimeError(f"invalid base64 for {what}") from exc


def fsync_dir(path: Path) -> None:
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_DIRECTORY", 0))
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def encode_register(blobs: list[tuple[bytes, bytes]]) -> bytes:
    if len(blobs) > MAX_MIMES:
        raise RuntimeError(f"item has {len(blobs)} MIME types; maximum is {MAX_MIMES}")
    out = bytearray(MAGIC)
    out += struct.pack("<IIQII", VERSION, len(blobs), int(time.time() * 1000), SOURCE_COPYQ_V1, len(APP_ID))
    out += APP_ID
    for mime, data in blobs:
        out += struct.pack("<IQ", len(mime), len(data))
        out += mime
        out += data
    return bytes(out)


def write_register(path: Path, payload: bytes) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.migrate.", dir=path.parent)
    tmp = Path(tmp_name)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "wb", closefd=True) as f:
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        os.chmod(path, 0o600)
        fsync_dir(path.parent)
    except Exception:
        try:
            os.close(fd)
        except OSError:
            pass
        tmp.unlink(missing_ok=True)
        raise


def write_done(path: Path, migrated: int, skipped_existing: int, skipped_secret: int) -> None:
    text = (
        "ClipReg v1 CopyQ migration complete\n"
        f"migrated={migrated}\n"
        f"skipped_existing={skipped_existing}\n"
        f"skipped_secret={skipped_secret}\n"
    ).encode()
    write_register_like_text(path, text)


def write_register_like_text(path: Path, payload: bytes) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    os.chmod(path.parent, 0o700)
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.tmp.", dir=path.parent)
    tmp = Path(tmp_name)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "wb", closefd=True) as f:
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, path)
        fsync_dir(path.parent)
    except Exception:
        try:
            os.close(fd)
        except OSError:
            pass
        tmp.unlink(missing_ok=True)
        raise


def run_copyq_export() -> subprocess.Popen[str] | None:
    copyq = shutil.which("copyq")
    if not copyq:
        return None
    # Starting the server is clipboard-neutral; the `tab` expression is only a
    # connectivity probe and does not select or copy any stored item.
    try:
        probe = subprocess.run(
            [copyq, "--start-server", "tab"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        eprint(f"WARNING: cannot reach CopyQ for v1 migration: {exc}")
        return None
    if probe.returncode != 0:
        detail = probe.stderr.strip()
        eprint("WARNING: CopyQ server unavailable; v1 register migration deferred" + (f": {detail}" if detail else ""))
        return None
    return subprocess.Popen(
        [copyq, "eval", "-"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )


def migrate(data_home: Path, max_payload: int, force: bool = False) -> int:
    root = data_home / "clipreg"
    reg_dir = root / "registers"
    done = root / DONE_FILE
    if done.exists() and not force:
        print(f"[MIGRATE] v1 CopyQ migration already completed: {done}")
        return 0

    proc = run_copyq_export()
    if proc is None:
        return 0
    assert proc.stdin is not None and proc.stdout is not None and proc.stderr is not None
    try:
        proc.stdin.write(EXPORT_JS)
        proc.stdin.close()

        migrated = skipped_existing = skipped_secret = 0
        saw_tab_state = False
        key: str | None = None
        secret = False
        blobs: list[tuple[bytes, bytes]] = []
        total = 0

        for raw in proc.stdout:
            line = raw.rstrip("\r\n")
            if line == "NO_TAB":
                if key is not None:
                    raise RuntimeError("unexpected NO_TAB inside register record")
                saw_tab_state = True
                continue
            parts = line.split("\t")
            tag = parts[0] if parts else ""
            if tag == "REGISTER":
                if key is not None or len(parts) != 3:
                    raise RuntimeError("malformed REGISTER record from CopyQ")
                key_bytes = decode64(parts[1], "register name")
                try:
                    key = key_bytes.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise RuntimeError("v1 register name is not UTF-8") from exc
                if not REGISTER_RE.fullmatch(key):
                    raise RuntimeError(f"v1 register has unsupported name: {key!r}")
                secret = parts[2] == "1"
                blobs = []
                total = 0
                saw_tab_state = True
            elif tag == "MIME":
                if key is None or secret or len(parts) != 3:
                    raise RuntimeError("malformed MIME record from CopyQ")
                mime = decode64(parts[1], "MIME name")
                data = decode64(parts[2], "MIME payload")
                if not mime or len(mime) > MAX_MIME_NAME or b"\0" in mime:
                    raise RuntimeError(f"invalid MIME name in register {key}")
                try:
                    mime.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise RuntimeError(f"non-UTF-8 MIME name in register {key}") from exc
                total += len(data)
                if total > max_payload:
                    raise RuntimeError(f"v1 register {key} exceeds migration payload limit")
                blobs.append((mime, data))
                if len(blobs) > MAX_MIMES:
                    raise RuntimeError(f"v1 register {key} has too many MIME representations")
            elif tag == "END":
                if key is None:
                    raise RuntimeError("unexpected END record from CopyQ")
                target = reg_dir / f"{key}.clipreg"
                if secret:
                    skipped_secret += 1
                    print(f"[MIGRATE] skip secret-marked v1 register: {key}")
                elif target.exists():
                    skipped_existing += 1
                    print(f"[MIGRATE] keep existing v2 register: {key}")
                elif not blobs:
                    raise RuntimeError(f"v1 register {key} contains no external MIME data")
                else:
                    write_register(target, encode_register(blobs))
                    migrated += 1
                    print(f"[MIGRATE] v1 CopyQ register -> v2: {key} ({len(blobs)} MIME type(s))")
                key = None
                secret = False
                blobs = []
                total = 0
            elif line:
                raise RuntimeError(f"unexpected CopyQ migration output: {line[:120]!r}")

        stderr = proc.stderr.read()
        rc = proc.wait(timeout=5)
        if rc != 0:
            raise RuntimeError(f"CopyQ export failed (exit {rc}): {stderr.strip()}")
        if key is not None:
            raise RuntimeError("truncated CopyQ register export")
        if not saw_tab_state:
            raise RuntimeError("CopyQ export returned no migration state")

        write_done(done, migrated, skipped_existing, skipped_secret)
        if migrated or skipped_existing or skipped_secret:
            print(
                f"[MIGRATE] complete: migrated={migrated}, "
                f"existing={skipped_existing}, secret-skipped={skipped_secret}"
            )
            print("[MIGRATE] original CopyQ tab is intentionally left untouched")
        else:
            print("[MIGRATE] no v1 CopyQ registers found")
        return 0
    except Exception as exc:
        proc.kill()
        try:
            proc.wait(timeout=1)
        except subprocess.TimeoutExpired:
            pass
        eprint(f"ERROR: ClipReg v1 migration failed: {exc}")
        return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Migrate ClipReg v1 CopyQ registers to v2 files")
    parser.add_argument("--force", action="store_true", help="re-scan CopyQ even if migration marker exists")
    parser.add_argument("--max-payload-mib", type=int, default=256)
    args = parser.parse_args()
    if args.max_payload_mib < 1 or args.max_payload_mib > 4096:
        parser.error("--max-payload-mib must be between 1 and 4096")
    data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    return migrate(data_home, args.max_payload_mib * 1024 * 1024, args.force)


if __name__ == "__main__":
    raise SystemExit(main())
