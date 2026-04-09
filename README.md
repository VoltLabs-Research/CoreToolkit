# Installation

```bash
curl -sSL https://raw.githubusercontent.com/VoltLabs-Research/CoreToolkit/main/scripts/install-plugin.sh | bash -s -- OpenDXA
```

`install-plugin.sh` clones `CoreToolkit`, the minimum plugin dependency chain, installs the
required local toolchain, packages upstream local dependencies with Conan, and builds the
requested plugin locally under `build-local`.

Supported plugin targets:

- `AtomicStrain`
- `CentroSymmetryParameter`
- `ClusterAnalysis`
- `CommonNeighborAnalysis`
- `CoordinationAnalysis`
- `DisplacementsAnalysis`
- `ElasticStrain`
- `GrainSegmentation`
- `LineReconstructionDXA`
- `OpenDXA`
- `PatternStructureMatching`
- `PolyhedralTemplateMatching`
- `StructureIdentification`

Optional environment variables:

- `WORK_DIR`: ecosystem root to clone/build into. Default: `./voltlabs-ecosystem`
- `GITHUB_ORG`: GitHub organization base URL. Default: `https://github.com/VoltLabs-Research`
- `BUILD_TYPE`: local CMake build type for the target plugin. Default: `Release`

## Repository layout

When `scripts/install-plugin.sh` clones the required repositories, `WORK_DIR` points directly
to the ecosystem root. By default that is `./voltlabs-ecosystem`, with this structure:

- `tools/`: `CoreToolkit`, `VoltSDK`, `SpatialAssembler`, `HeadlessRasterizer`, `LammpsIO`
- `plugins/`: `StructureIdentification`, `AtomicStrain`, `CentroSymmetryParameter`, `ClusterAnalysis`, `CoordinationAnalysis`, `DisplacementsAnalysis`, `ElasticStrain`, `GrainSegmentation`, `OpenDXA`
- `app/`: `Volt`, `ClusterDaemon`

The plugin installer does not clone the whole stack by default. It only clones:

- `CoreToolkit`
- the requested plugin
- the local plugin repositories required by that plugin's Conan dependency chain

## GitHub Actions plugin binaries

CoreToolkit now exposes a reusable workflow at `.github/workflows/build-plugin-binary.yml`.

Each plugin repository can call that workflow from its own `.github/workflows/publish-plugin-binary.yml` file to:

- build the plugin against a fresh CoreToolkit checkout,
- package the installed binary bundle, and
- publish the bundle to GHCR as an OCI artifact.

Package naming follows this pattern:

- `ghcr.io/voltlabs-research/<plugin-repo-lowercase>-binary:sha-<commit>-<os>-<arch>`
- `ghcr.io/voltlabs-research/<plugin-repo-lowercase>-binary:<tag>-<os>-<arch>` for tag builds
