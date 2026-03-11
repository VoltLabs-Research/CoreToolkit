#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GITHUB_ORG="${GITHUB_ORG:-https://github.com/voltlabs-research}"
WORK_DIR="${WORK_DIR:-./voltlabs}"
CONAN_OPTS=(--build=missing -o "hwloc/*:shared=True")
SUPPORTED_CMAKE_VERSION="3.20.0"
SUPPORTED_CONAN_VERSION="2.0.0"
APT_UPDATED=0
BUILD_OUTPUT_DIR="${BUILD_OUTPUT_DIR:-${2:-${SCRIPT_DIR}/out}}"

PACKAGES=(
    CoreToolkit

    # Plugins
    StructureIdentification
    AtomicStrain
    CentroSymmetryParameter
    ClusterAnalysis
    CoordinationAnalysis
    DisplacementsAnalysis
    ElasticStrain
    GrainSegmentation
    OpenDXA
    
    # App-related
    SpatialAssembler
    HeadlessRasterizer
    LammpsIO

    # Volt
    Volt
    ClusterDaemon
    VoltSDK
)

log() {
    printf '[install] %s\n' "$*"
}

die() {
    printf '[install] error: %s\n' "$*" >&2
    exit 1
}

has_command() {
    command -v "$1" >/dev/null 2>&1
}

version_ge() {
    if has_command dpkg; then
        dpkg --compare-versions "$1" ge "$2"
    else
        [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" = "$2" ]
    fi
}

require_supported_os() {
    [ -f /etc/os-release ] || die 'unable to detect the operating system (/etc/os-release not found)'
    # shellcheck disable=SC1091
    . /etc/os-release

    if [[ "${ID:-}" == "ubuntu" || "${ID:-}" == "debian" ]]; then
        return
    fi

    if [[ " ${ID_LIKE:-} " == *' debian '* ]]; then
        return
    fi

    die "this installer currently supports Ubuntu/Debian only (detected: ${PRETTY_NAME:-unknown})"
}

configure_privilege_escalation() {
    if [[ ${EUID} -eq 0 ]]; then
        SUDO=()
        return
    fi

    has_command sudo || die 'sudo is required to install system packages on Ubuntu/Debian'
    SUDO=(sudo)
}

package_installed() {
    dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
}

apt_update_once() {
    if [[ ${APT_UPDATED} -eq 0 ]]; then
        log 'updating apt package index'
        "${SUDO[@]}" apt-get update
        APT_UPDATED=1
    fi
}

install_apt_packages() {
    local packages=("$@")
    if [[ ${#packages[@]} -eq 0 ]]; then
        return
    fi

    apt_update_once
    log "installing system packages: ${packages[*]}"
    "${SUDO[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${packages[@]}"
}

ensure_base_packages() {
    local required_packages=(
        build-essential
        ca-certificates
        cmake
        git
        pkg-config
        python3
        python3-pip
    )
    local missing_packages=()
    local package

    for package in "${required_packages[@]}"; do
        if ! package_installed "$package"; then
            missing_packages+=("$package")
        fi
    done

    install_apt_packages "${missing_packages[@]}"
}

ensure_cmake() {
    local cmake_version
    has_command cmake || die 'cmake is required but was not found after installing system packages'
    cmake_version="$(cmake --version | awk 'NR==1 {print $3}')"

    version_ge "$cmake_version" "$SUPPORTED_CMAKE_VERSION" || die "cmake >= $SUPPORTED_CMAKE_VERSION is required, found $cmake_version"
    log "cmake $cmake_version detected"
}

ensure_cxx23_compiler() {
    local compiler="${CXX:-}"
    local tmpdir

    if [[ -z "$compiler" ]]; then
        if has_command g++; then
            compiler='g++'
        elif has_command clang++; then
            compiler='clang++'
        else
            die 'a C++ compiler is required but neither g++ nor clang++ is available'
        fi
    fi

    tmpdir="$(mktemp -d)"
    cat > "$tmpdir/test.cpp" <<'CPP'
int main() { return 0; }
CPP

    if ! "$compiler" -std=c++23 "$tmpdir/test.cpp" -o "$tmpdir/test" >/dev/null 2>&1; then
        rm -rf "$tmpdir"
        die "a C++23-capable compiler is required, but '$compiler' could not compile a minimal program with -std=c++23"
    fi

    rm -rf "$tmpdir"
    log "C++23 compiler detected: $compiler"
}

ensure_conan() {
    local conan_version

    export PATH="$HOME/.local/bin:$PATH"

    if ! has_command conan; then
        log 'installing Conan with pip'
        python3 -m pip install --user --upgrade conan
    fi

    has_command conan || die 'conan is required but was not found after installation'
    conan_version="$(conan --version | awk '{print $3}')"

    version_ge "$conan_version" "$SUPPORTED_CONAN_VERSION" || die "conan >= $SUPPORTED_CONAN_VERSION is required, found $conan_version"
    conan profile detect --force >/dev/null
    log "Conan $conan_version detected"
}

ensure_docker() {
    has_command docker || die 'docker is required to build the algorithms'
}

clone_repositories() {
    local repo

    mkdir -p "$WORK_DIR"
    log 'cloning repositories'

    for repo in "${PACKAGES[@]}"; do
        if [[ -d "$WORK_DIR/$repo" ]]; then
            log "$repo already exists, skipping clone"
        else
            git clone --depth 1 "$GITHUB_ORG/$repo.git" "$WORK_DIR/$repo"
        fi
    done
}

build_packages() {
    local build_extra_args=()
    local dockerfile_path="${WORK_DIR}/CoreToolkit/Dockerfile.build"

    # Check for --no-cache anywhere in args
    for arg in "$@"; do
        if [[ "$arg" == "--no-cache" ]]; then
            build_extra_args+=(--no-cache)
        fi
    done

    mkdir -p "${BUILD_OUTPUT_DIR}"
    [[ -f "$dockerfile_path" ]] || die "Dockerfile not found at $dockerfile_path"

    local pkg

    log 'building packages'
    for pkg in "${PACKAGES[@]}"; do
        log "$pkg"

        DOCKER_BUILDKIT=1 docker build \
            -f "$dockerfile_path" \
            --build-arg "ALGORITHM=${pkg}" \
            --output "type=local,dest=${BUILD_OUTPUT_DIR}" \
            "${build_extra_args[@]}" \
            "${WORK_DIR}"
    done
}

main() {
    require_supported_os
    configure_privilege_escalation
    ensure_base_packages
    ensure_cmake
    ensure_cxx23_compiler
    ensure_conan
    ensure_docker
    clone_repositories
    build_packages
    log 'done'
}

main "$@"
