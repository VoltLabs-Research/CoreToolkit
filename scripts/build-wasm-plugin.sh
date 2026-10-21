#!/usr/bin/env bash
set -euo pipefail

if ! command -v emcmake >/dev/null 2>&1; then
    echo "emcmake not found. Install emsdk and source emsdk_env.sh." >&2
    exit 1
fi

# Without a plugin it builds CoreToolkit's own programs (the daemon's tools).
plugin_dir="${1:-}"
if [[ -n "$plugin_dir" && ! -d "$plugin_dir" ]]; then
    echo "usage: $0 [plugin-source-dir [dependency-dir...]]" >&2
    exit 1
fi
shift || true

here="$(cd "$(dirname "$0")/.." && pwd)"
lammpsio_dir="${VOLT_LAMMPSIO_SOURCE:-$(cd "$here/../LammpsIO" 2>/dev/null && pwd || true)}"
if [[ -z "$lammpsio_dir" || ! -d "$lammpsio_dir" ]]; then
    echo "Set VOLT_LAMMPSIO_SOURCE to the LammpsIO checkout." >&2
    exit 1
fi

build_dir="${VOLT_WASM_BUILD_DIR:-$here/build-wasm}"
install_dir="${VOLT_WASM_INSTALL_DIR:-$here/install-wasm}"
mkdir -p "$build_dir" "$install_dir"

dep_joined=""
if (($#)); then
    IFS=';'
    dep_joined="$*"
    unset IFS
fi

cmake_extra=()
if [[ -n "${VOLT_BOOST_ROOT:-}" ]]; then
    cmake_extra+=(-DVOLT_BOOST_ROOT="$VOLT_BOOST_ROOT")
fi

emcmake cmake -S "$here/cmake/wasm" -B "$build_dir" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_dir" \
    -DVOLT_PLUGIN_SOURCE="${plugin_dir:+$(cd "$plugin_dir" && pwd)}" \
    -DVOLT_CORETOOLKIT_SOURCE="$here" \
    -DVOLT_LAMMPSIO_SOURCE="$lammpsio_dir" \
    -DVOLT_DEPENDENCY_DIRS="$dep_joined" \
    -DVOLT_ENABLE_PARALLEL_DELAUNAY=OFF \
    -DVOLT_BASELINE_ARCH=off \
    -DBUILD_TESTING=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    "${cmake_extra[@]}"

cmake --build "$build_dir" --config Release --parallel
cmake --install "$build_dir" --config Release
echo "Installed wasm build to $install_dir"
