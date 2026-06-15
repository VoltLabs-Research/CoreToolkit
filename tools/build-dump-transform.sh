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

# Build inside the container. We install the toolchain + conan, then configure
# and build only the volt-dump-transform target. The CoreToolkit checkout is
# mounted read-write so conan/cmake artifacts land in a container-only build dir
# (build-docker) that never collides with the host's build-local.
docker run --rm \
    -v "${CORETOOLKIT_DIR}:/src" \
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
        cmake --preset conan-release \
            -S /src -B /src/build-docker/build/Release >/dev/null
        cmake --build /src/build-docker/build/Release --target volt-dump-transform -j"$(nproc)"
        # sanity: it must run in THIS glibc-2.39 environment
        /src/build-docker/build/Release/volt-dump-transform 2>&1 | head -1 || true
    '

BUILT_BINARY="${CORETOOLKIT_DIR}/build-docker/build/Release/volt-dump-transform"
if [[ ! -x "${BUILT_BINARY}" ]]; then
    echo "ERROR: build did not produce ${BUILT_BINARY}" >&2
    exit 1
fi

mkdir -p "${DAEMON_BIN_DIR}"
cp "${BUILT_BINARY}" "${DAEMON_BIN_DIR}/volt-dump-transform"
chmod +x "${DAEMON_BIN_DIR}/volt-dump-transform"
echo "==> Installed glibc-2.39 binary -> ${DAEMON_BIN_DIR}/volt-dump-transform"
