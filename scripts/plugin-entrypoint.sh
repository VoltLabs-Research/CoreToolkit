#!/usr/bin/env bash
set -euo pipefail

plugin="${VOLT_PLUGIN_EXECUTABLE:-}"
if [[ -z "${plugin}" ]]; then
    echo "VOLT_PLUGIN_EXECUTABLE is not set" >&2
    exit 1
fi

args=("$@")

has_flag() {
    local flag="$1"
    local arg
    for arg in "${args[@]}"; do
        if [[ "${arg}" == "${flag}" || "${arg}" == "${flag}="* ]]; then
            return 0
        fi
    done
    return 1
}

case "${plugin}" in
    opendxa)
        default_lattice_dir="${VOLT_DEFAULT_LATTICE_DIR:-/opt/volt/share/volt/lattices}"
        if ! has_flag "--lattice-dir"; then
            args+=(--lattice-dir "${default_lattice_dir}")
        fi
        ;;
    pattern-structure-matching)
        default_pattern_lattice_dir="${VOLT_DEFAULT_PATTERN_LATTICE_DIR:-/opt/volt/share/volt/pattern-structure-matching/lattices}"
        default_reference_lattice_dir="${VOLT_DEFAULT_REFERENCE_LATTICE_DIR:-/opt/volt/share/volt/lattices}"
        if ! has_flag "--lattice-dir"; then
            args+=(--lattice-dir "${default_pattern_lattice_dir}")
        fi
        if ! has_flag "--reference-lattice-dir"; then
            args+=(--reference-lattice-dir "${default_reference_lattice_dir}")
        fi
        ;;
esac

exec "/opt/volt/bin/${plugin}" "${args[@]}"
