#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORETOOLKIT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
ECOSYSTEM_DIR="$(cd "${CORETOOLKIT_DIR}/../.." && pwd)"
DAEMON_BIN_DIR="${ECOSYSTEM_DIR}/app/ClusterDaemon/src/modules/analysis/infrastructure/bin"

echo "==> Building volt-dump-transform in ubuntu:24.04 (glibc 2.39, matches the daemon image)"

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
        cmake -S /src -B /src/build-docker/build/Release \
            -G "Unix Makefiles" \
            -DCMAKE_TOOLCHAIN_FILE=/src/build-docker/build/Release/generators/conan_toolchain.cmake \
            -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
            -DCMAKE_BUILD_TYPE=Release >/dev/null
        cmake --build /src/build-docker/build/Release --target volt-dump-transform -j"$(nproc)"
        BIN=/src/build-docker/build/Release/volt-dump-transform
        "$BIN" 2>&1 | head -1 || true
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
