# scopehal-apps

## Building

Don't run `make`/`cmake --build` directly for anything beyond a trivial, isolated
check. Slow/OOM-prone rebuilds have been a recurring problem here (linking used
to OOM the machine before we set the CMakeLists.txt to auto-detect and use the
`mold` linker when installed; separately, small changes have occasionally
triggered much larger recompiles than expected, cause not yet fully diagnosed).

Use `./instrumented-build.sh ["label"]` (repo root) instead of a bare build. It
wraps a normal `cmake . && make` with logging needed to diagnose rebuild-time
issues: git HEAD/diff, `make -n` dry-runs both before and after reconfigure (to
isolate what the reconfigure step alone marks stale), mtimes of every source
file and build artifact, a memory sampler during the build, and a one-line
summary appended to `build-logs/summary.tsv` (`build-logs/run-<timestamp>/` has
the full detail per run). `build-logs/` is gitignored -- local diagnostic data
only, not for committing.

Before starting any rebuild -- instrumented or not -- check whether one is
already pending/interrupted (e.g. a prior session got told to stop mid-build)
and ask the user before kicking one off.

## IIO / SDR reference sources

`../libiio` and `../pyadi-iio` (parallel to this repo, i.e.
`ngscopeclient/libiio` and `ngscopeclient/pyadi-iio`) are checked out locally
as reference material for IIO-based SDR work (e.g. PlutoSDR/ADALM). Consult
them for the IIO C API and device/attribute semantics rather than guessing.
