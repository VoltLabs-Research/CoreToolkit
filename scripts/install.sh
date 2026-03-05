#!/usr/bin/env bash
set -euo pipefail

GITHUB_ORG="https://github.com/voltlabs-research"
WORK_DIR="./voltlabs"
CONAN_OPTS=(--build=missing -o "hwloc/*:shared=True")

PACKAGES=(
    CoreToolkit
    StructureIdentification
    AtomicStrain
    CentroSymmetryParameter
    ClusterAnalysis
    CoordinationAnalysis
    DisplacementsAnalysis
    ElasticStrain
    GrainSegmentation
    OpenDXA
)

mkdir -p "$WORK_DIR"

echo "cloning repositories"
for repo in "${PACKAGES[@]}"; do
    if [ -d "$WORK_DIR/$repo" ]; then
        echo "--- $repo already exists, skipping"
    else
        git clone --depth 1 "$GITHUB_ORG/$repo.git" "$WORK_DIR/$repo"
    fi
done

echo "building packages..."
for pkg in "${PACKAGES[@]}"; do
    echo "--- $pkg"
    conan create "$WORK_DIR/$pkg" "${CONAN_OPTS[@]}"
done

echo "done!"
