#!/usr/bin/env python3
"""Pack a plugin install tree as a Volt PluginHub bundle.

Layout produced inside the bundle::

    plugin.json
    bin/<binary>[.exe]
    lib/...        (shared libraries, optional)
    scripts/...    (Python wrappers, optional)
    share/...      (lattices and other data, optional)

Output files written to ``--output-dir``::

    <key>-<version>-<os>-<arch>.tar.zst
    <key>-<version>-<os>-<arch>.tar.zst.sha256

Plugins without an executable entrypoint (libraries) are skipped silently
and do not emit release outputs.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import shutil
import sys
import tarfile
from pathlib import Path

import zstandard


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plugin-dir", required=True, type=Path)
    parser.add_argument("--install-dir", required=True, type=Path)
    parser.add_argument("--os", dest="os_slug", required=True)
    parser.add_argument("--arch", dest="arch_slug", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    plugin_dir: Path = args.plugin_dir.resolve()
    install_dir: Path = args.install_dir.resolve()
    output_dir: Path = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    manifest_path = plugin_dir / "plugin.json"
    if not manifest_path.exists():
        print(f"::warning::No plugin.json at {manifest_path}; skipping bundle.")
        return 0

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    modifier = _first_node(manifest, "modifier", "modifier")
    entrypoint = _first_node(manifest, "entrypoint", "entrypoint")
    key = str(modifier.get("key") or "")
    version = str(modifier.get("version") or "")
    if not key or not version:
        print(f"::warning::plugin.json missing modifier.key or modifier.version; skipping.")
        return 0

    binary_name = _binary_name(entrypoint)
    if not binary_name:
        print(f"::notice::{key} has no executable entrypoint; skipping bundle.")
        return 0

    staging = output_dir / f".staging-{args.os_slug}-{args.arch_slug}"
    if staging.exists():
        shutil.rmtree(staging)
    staging.mkdir(parents=True)

    binary_filename = f"{binary_name}.exe" if args.os_slug == "windows" else binary_name
    bin_src = _find_binary(install_dir, binary_filename)
    if bin_src is None:
        print(f"::error::binary {binary_filename!r} not found under {install_dir}", file=sys.stderr)
        for path in install_dir.rglob("*"):
            if path.is_file():
                print(f"  {path.relative_to(install_dir)}", file=sys.stderr)
        return 1

    (staging / "bin").mkdir()
    shutil.copy2(bin_src, staging / "bin" / binary_filename)

    install_lib = install_dir / "lib"
    if install_lib.is_dir():
        shutil.copytree(install_lib, staging / "lib", dirs_exist_ok=True)

    install_share = install_dir / "share"
    if install_share.is_dir():
        shutil.copytree(install_share, staging / "share", dirs_exist_ok=True)

    plugin_scripts = plugin_dir / "scripts"
    if plugin_scripts.is_dir():
        shutil.copytree(plugin_scripts, staging / "scripts", dirs_exist_ok=True)

    shutil.copy2(manifest_path, staging / "plugin.json")

    base_name = f"{key}-{version}-{args.os_slug}-{args.arch_slug}"
    archive_path = output_dir / f"{base_name}.tar.zst"
    checksum_path = output_dir / f"{archive_path.name}.sha256"

    _pack(staging, archive_path)
    digest = _sha256(archive_path)
    checksum_path.write_text(f"{digest}  {archive_path.name}\n", encoding="utf-8")

    print(f"::notice::Bundled {archive_path.name} ({archive_path.stat().st_size} bytes, sha256={digest})")
    shutil.rmtree(staging)

    _emit_outputs(key, version)
    return 0


def _first_node(manifest: dict, node_type: str, data_key: str) -> dict:
    for node in manifest.get("workflow", {}).get("nodes", []) or []:
        if isinstance(node, dict) and node.get("type") == node_type:
            data = node.get("data", {}).get(data_key, {})
            if isinstance(data, dict):
                return data
    return {}


def _binary_name(entrypoint: dict) -> str:
    script = str(entrypoint.get("entrypointScript") or "")
    if script.startswith("bin/"):
        return Path(script).name
    binary = str(entrypoint.get("binaryFileName") or entrypoint.get("binary") or "")
    for suffix in ("-plugin.zip", ".zip"):
        if binary.endswith(suffix):
            return binary[: -len(suffix)]
    return Path(binary).stem if binary else ""


def _find_binary(install_dir: Path, filename: str) -> Path | None:
    direct = install_dir / "bin" / filename
    if direct.exists():
        return direct
    for candidate in install_dir.rglob(filename):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def _pack(source: Path, target: Path) -> None:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w") as tar:
        for entry in sorted(source.rglob("*")):
            if entry.is_dir():
                continue
            tar.add(entry, arcname=entry.relative_to(source).as_posix())
    raw = buffer.getvalue()
    compressor = zstandard.ZstdCompressor(level=19)
    target.write_bytes(compressor.compress(raw))


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _emit_outputs(key: str, version: str) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if not output_path:
        return
    with open(output_path, "a", encoding="utf-8") as fh:
        fh.write(f"plugin_key={key}\n")
        fh.write(f"plugin_version={version}\n")


if __name__ == "__main__":
    raise SystemExit(main())
