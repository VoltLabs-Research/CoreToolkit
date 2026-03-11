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
