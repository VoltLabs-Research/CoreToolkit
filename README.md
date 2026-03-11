# Installation

```bash
curl -sSL https://raw.githubusercontent.com/voltlabs-research/CoreToolkit/main/scripts/install.sh | bash
```

## Repository layout

When `scripts/install.sh` clones the required repositories, it places them under the selected working directory using this structure:

- `tools/`: `CoreToolkit`, `VoltSDK`, `SpatialAssembler`, `HeadlessRasterizer`, `LammpsIO`
- `plugins/`: `StructureIdentification`, `AtomicStrain`, `CentroSymmetryParameter`, `ClusterAnalysis`, `CoordinationAnalysis`, `DisplacementsAnalysis`, `ElasticStrain`, `GrainSegmentation`, `OpenDXA`
- `app/`: `Volt`, `ClusterDaemon`
