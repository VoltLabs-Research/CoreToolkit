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

## Reporting a plugin's progress to VOLT

When VOLT runs a plugin it sets `VOLT_PROGRESS=1`, and the plugin's progress goes out as
`@volt:progress <0..1> <text>` lines on stderr, which VOLT shows on the job. Without that
variable nothing is written, so a plugin run by hand logs what it always did.

`pluginMain` splits the run into reading the input and the analysis. A plugin with several
passes splits its analysis further with `ProgressSteps`, the way OVITO weighs its sub-steps;
each `ProgressReporter` pass fills the step it runs in, under its label:

```cpp
ProgressSteps steps({1, 6, 2});        // neighbor lists, classification, outputs
neighborFinder.prepare(positions, cell);
steps.next();
ProgressReporter progress("Classifying atoms", atomCount);
// ... progress.advance(range.size()) ...
steps.next();
Plugin::serializePluginOutput(outputBase, frame, result, config);
```

The value only ever climbs. A step without a pass moves when the step after it starts, and
two passes in one step share it: the second moves the value once it passes the first.

The text is what the current step does: a pass's label while it runs, or the one given with
`steps.next("Writing outputs")`. When a step is over its text goes with it, back to the one of
the step that was split (in a plugin's analysis, the descriptor's description). A run that
fails stops where it was; only one that succeeds ends at 1.

A plugin with its own `main()` takes the same top steps as `pluginMain`:

```cpp
JobProgress& progress = JobProgress::instance();
progress.beginInput();                        // "Reading the input frame"
// ... parse the frame ...
progress.beginAnalysis(descriptor.description);
json result = analyzer.compute(frame, outputBase);
if (result.value("is_failed", false)) return 1;
progress.finish();
```

The steps belong to the thread that runs the plugin. Splits, step changes and texts from any
other thread (an export run with `std::async`, say) are ignored, and so are the passes it
starts, so they cannot move the run's steps under it.

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

## GitHub Actions OCI Images

CoreToolkit also exposes a reusable workflow at `.github/workflows/build-plugin-oci.yml`.

Each plugin repository can call that workflow from its own `.github/workflows/publish-plugin-oci.yml`
file to:

- build the plugin against a fresh CoreToolkit checkout,
- install it into a clean staging directory,
- package that staging directory into a runnable OCI image, and
- publish the image to GHCR.

The runtime image uses `debian:bookworm-slim` by default, installs the required
runtime packages, and dispatches through `scripts/plugin-entrypoint.sh` so plugins
such as `OpenDXA` and `PatternStructureMatching` receive sensible default lattice
directories inside the image.

OCI image naming follows this pattern:

- `ghcr.io/voltlabs-research/<plugin-repo-lowercase>-oci:sha-<commit>-linux-<arch>`
- `ghcr.io/voltlabs-research/<plugin-repo-lowercase>-oci:<tag>-linux-<arch>` for tag builds
