#!/usr/bin/env bash
#
# Builds volt-dump-transform inside an ubuntu:24.04 container (glibc 2.39 — the
# same base the ClusterDaemon image uses) and copies the resulting binary into
# the daemon's vendored bin dir. This is the portable-build entry point: a binary
# compiled against a NEWER host glibc fails to load in the daemon container with
# `GLIBC_x.yy not found`, so we pin the build glibc to the runtime's. oneTBB is
# shared, but the daemon image already ships libtbb.so.12, so the dynamic dep
# resolves at runtime. Re-run this whenever tools/dump_transform.cpp or the
# CoreToolkit sources it links change.
#
# Usage:  packages/CoreToolkit/tools/build-dump-transform.sh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORETOOLKIT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ECOSYSTEM_DIR="$(cd "${CORETOOLKIT_DIR}/../.." && pwd)"
DAEMON_BIN_DIR="${ECOSYSTEM_DIR}/app/ClusterDaemon/src/modules/analysis/infrastructure/bin"

echo "==> Building volt-dump-transform in ubuntu:24.04 (glibc 2.39, matches the daemon image)"

# Build inside the container as root (a bare ubuntu:24.04 needs apt to install
# the toolchain + conan). The CoreToolkit checkout is mounted read-write; conan
# writes a CMakeUserPresets.json into /src and leaves a build-docker/ dir — the
# container installs the binary into the daemon bin dir, then cleans build-docker
# and restores CMakeUserPresets.json from git BEFORE exiting (while it still has
# root), so the host is left clean and no root-owned artifacts remain.
DAEMON_BIN_IN_CONTAINER="/daemon-bin"
docker run --rm \
    -v "${CORETOOLKIT_DIR}:/src" \
    -v "${DAEMON_BIN_DIR}:${DAEMON_BIN_IN_CONTAINER}" \
    -w /src \
    ubuntu:24.04 \
    bash -euo pipefail -c '
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq
        apt-get install -y -qq --no-install-recommends \
            build-essential cmake git python3 python3-pip pipx ca-certificates >/dev/null
        pipx install conan >/dev/null 2>&1 || pip3 install --break-system-packages conan >/dev/null
        export PATH="/root/.local/bin:${PATH}"
        conan profile detect --force >/dev/null
        rm -rf /src/build-docker
        conan install /src --output-folder=/src/build-docker --build=missing \
            -s compiler.cppstd=17 -o "boost/*:without_stacktrace=True" >/dev/null
        # Configure with the conan toolchain directly (NOT --preset): the host
        # CMakeUserPresets.json is mounted in /src and would collide with the
        # container-generated one ("Duplicate preset: conan-release").
        cmake -S /src -B /src/build-docker/build/Release \
            -G "Unix Makefiles" \
            -DCMAKE_TOOLCHAIN_FILE=/src/build-docker/build/Release/generators/conan_toolchain.cmake \
            -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
            -DCMAKE_BUILD_TYPE=Release >/dev/null
        cmake --build /src/build-docker/build/Release --target volt-dump-transform -j"$(nproc)"
        BIN=/src/build-docker/build/Release/volt-dump-transform
        # sanity: it must run in THIS glibc-2.39 environment
        "$BIN" 2>&1 | head -1 || true
        # install into the daemon bin dir (mounted), then clean up everything the
        # build left in the mounted source tree while we still have root.
        install -m 0755 "$BIN" "'"${DAEMON_BIN_IN_CONTAINER}"'/volt-dump-transform"
        rm -rf /src/build-docker
        if [ -d /src/.git ] || git -C /src rev-parse --git-dir >/dev/null 2>&1; then
            git -C /src checkout -- CMakeUserPresets.json 2>/dev/null || true
        fi
    '

INSTALLED="${DAEMON_BIN_DIR}/volt-dump-transform"
if [[ ! -x "${INSTALLED}" ]]; then
    echo "ERROR: build did not install ${INSTALLED}" >&2
    exit 1
fi
echo "==> Installed glibc-2.39 binary -> ${INSTALLED}"

