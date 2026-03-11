# Installation

```bash
curl -sSL https://raw.githubusercontent.com/voltlabs-research/CoreToolkit/main/scripts/install.sh | bash
```

## Repository layout

When `scripts/install.sh` clones the required repositories, `WORK_DIR` points directly to the ecosystem root. By default that is `./voltlabs-ecosystem`, with this structure:

- `tools/`: `CoreToolkit`, `VoltSDK`, `SpatialAssembler`, `HeadlessRasterizer`, `LammpsIO`
- `plugins/`: `StructureIdentification`, `AtomicStrain`, `CentroSymmetryParameter`, `ClusterAnalysis`, `CoordinationAnalysis`, `DisplacementsAnalysis`, `ElasticStrain`, `GrainSegmentation`, `OpenDXA`
- `app/`: `Volt`, `ClusterDaemon`

The installer clones all of those repositories, but the current Docker-based build step compiles the algorithm/plugin repositories only.

## GitHub Actions plugin binaries

CoreToolkit now exposes a reusable workflow at `.github/workflows/build-plugin-binary.yml`.

Each plugin repository can call that workflow from its own `.github/workflows/publish-plugin-binary.yml` file to:

- build the plugin against a fresh CoreToolkit checkout,
- package the installed binary bundle, and
- publish the bundle to GHCR as an OCI artifact.

Package naming follows this pattern:

- `ghcr.io/voltlabs-research/<plugin-repo-lowercase>-binary:sha-<commit>-<os>-<arch>`
- `ghcr.io/voltlabs-research/<plugin-repo-lowercase>-binary:<tag>-<os>-<arch>` for tag builds
