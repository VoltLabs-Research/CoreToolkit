#!/usr/bin/env python3
"""Pack one CoreToolkit command-line tool as a self-contained release asset.

Layout inside the archive::

    bin/<tool>[.exe]
    bin/<shared libraries the tool loads at runtime>

Everything sits in one directory: the Linux binary carries RPATH $ORIGIN, the
macOS binary @loader_path, and Windows searches the executable's directory, so
no environment variable is needed to run it from an extracted bundle.

Output files written to ``--output-dir``::

    <tool>-<os>-<arch>.tar.gz
    <tool>-<os>-<arch>.tar.gz.sha256
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_plugin_bundle import _collect_runtime_files, _copy_runtime_artifact, _find_binary  # noqa: E402


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", required=True)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--install-dir", required=True, type=Path)
    parser.add_argument("--os", dest="os_slug", required=True)
    parser.add_argument("--arch", dest="arch_slug", required=True)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    install_dir: Path = args.install_dir.resolve()
    output_dir: Path = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    binary_filename = f"{args.tool}.exe" if args.os_slug == "windows" else args.tool
    binary = _find_binary(install_dir, binary_filename)
    if binary is None:
        print(f"::error::{binary_filename} not found under {install_dir}", file=sys.stderr)
        return 1

    staging = output_dir / f".staging-{args.tool}-{args.os_slug}-{args.arch_slug}"
    if staging.exists():
        shutil.rmtree(staging)
    (staging / "bin").mkdir(parents=True)
    shutil.copy2(binary, staging / "bin" / binary_filename)
    os.chmod(staging / "bin" / binary_filename, 0o755)

    for runtime_file in _collect_runtime_files(args.build_dir.resolve(), args.os_slug):
        _copy_runtime_artifact(runtime_file, staging / "bin" / runtime_file.name)

    base_name = f"{args.tool}-{args.os_slug}-{args.arch_slug}"
    archive = output_dir / f"{base_name}.tar.gz"
    with tarfile.open(archive, "w:gz", compresslevel=9) as tar:
        for entry in sorted(staging.rglob("*")):
            if entry.is_file() or entry.is_symlink():
                tar.add(entry, arcname=entry.relative_to(staging).as_posix(), recursive=False)

    digest = _sha256(archive)
    (output_dir / f"{archive.name}.sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
    shutil.rmtree(staging)

    print(f"::notice::Bundled {archive.name} ({archive.stat().st_size} bytes, sha256={digest})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
