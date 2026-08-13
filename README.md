# libxtm

`libxtm` is a C++ library and command-line utility for compression, encoding, decoding, and analysis of terrain/elevation data. It provides a specialized `.xtm` format designed for efficient storage and random-access retrieval of geospatial terrain data.

## Features

- **Specialized Terrain Compression**: predictive coding tailored for terrain data, with optional wavelet coding.
- **Predictors**: six deterministic models — Left, Gradient, JPEG-LS, GAP, Polynomial, and Least Squares — chosen per block by a cost estimator.
- **Quadtree Partitioning**: 512×512 → 64×64 hierarchical partitioning driven by a rate-cost split rule.
- **Split-Precision Coding**: sub-meter scales encode meter and precision planes independently.
- **Geospatial Support**: GDAL-based I/O with full geotransform and WKT projection preservation.
- **Region of Interest (ROI) Decoding**: decode only a bounding region from the `.xtm` file without uncompressing the dataset.
- **Comprehensive Analyzer**: terrain statistics, predictor performance, and compression diagnostics via `xtm analyze`.
## Architecture


- `apps/xtm/`    CLI — encode, decode, analyze, info, verify
- `include/`     public API
- `src/`
  - `terrain`     quantization, NoData inpainting
  - `partition`   quadtree 512→64 partitioning, block views
  - `predictor`   predictor models (Left, Gradient, JpegLs, Gap, Polynomial, LeastSquares)
  - `analyzer`    per-block predictor selection + statistics
  - `transform`   CDF 5/3 integer wavelet
  - `coding`      arithmetic coder, context modeling, pipeline
  - `container`   .xtm format (header, block index, CRC32)
  - `io`          GDAL reader/writer
- `tests/`       CTest suite


The encoder and the analyzer share one pipeline implementation — `for_each_superblock` (`src/coding/Pipeline.cpp`) owns superblock slicing, quadtree partitioning, and predictor selection for both, on top of the generic `parallel_for_superblocks` worker pool. See `docs/pipeline.md` for the full technical reference.

## Requirements

To build and use `libxtm`, you will need:

- **C++20 compiler** (GCC or MSVC)
- **CMake** >= 3.21 and **Ninja** (when using the presets)
- **GDAL** library
- (Optional) **GoogleTest** (fetched automatically via CMake if tests are enabled and GTest is not found system-wide)
- (Optional) **Python >= 3.10 dev headers** — only when building the Python bindings (`-DXTM_BUILD_PYTHON=ON`); nanobind is fetched automatically via CMake

## Building the Project

`libxtm` uses CMake. Two presets are provided in `CMakePresets.json`:

| Preset | Build dir | Type | Flags |
|---|---|---|---|
| `dev` | `build/dev` | Debug | TSan + UBSan, export compile commands |
| `release` | `build/release` | Release | `-O3 -march=native -mtune=native`, LTO, export compile commands |

```bash
# Configure, build, and test with a preset (Ninja required)
cmake --preset release
cmake --build --preset release
ctest --preset release
```

Without presets, a plain configure works too (defaults to Release if no build type is given):

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

Build behavior worth knowing:

- **Default build type**: `Release` if none is specified.
- **Warnings as errors**: `-Wall -Wextra -Wpedantic` + `-Werror` (GCC, via `XTM_WERROR`) or `/W4 /WX` (MSVC).
- **Release optimizations**: `-O3 -march=native -mtune=native -ftree-vectorize` plus LTO when supported (GCC).
- **ccache** is used automatically when detected (zero-config; skipped silently otherwise).
- **Output layout**: executables in `build/bin` (`xtm`), libraries in `build/lib` (`libxtm_core`).
- **Compile database**: with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (on in both presets), `compile_commands.json` is symlinked to the project root for IDE/clangd.
- **Install**: `cmake --install build` (GNUInstallDirs; installs `xtm_core`, the `xtm` binary, and headers).

### Build Options

| Option | Default | Description |
|---|---|---|
| `-DBUILD_SHARED_LIBS=ON` | `OFF` | Build `xtm_core` as a shared library instead of static |
| `-DBUILD_TESTING=ON/OFF` | `ON` | Build the CTest suite; GoogleTest v1.18.0 is fetched from GitHub if not found system-wide |
| `-DENABLE_TSAN=ON` | `OFF` | Thread Sanitizer |
| `-DENABLE_UBSAN=ON` | `OFF` | Undefined Behavior Sanitizer |
| `-DXTM_BUILD_PYTHON=ON` | `OFF` | Build Python bindings (nanobind); module lands in `<build>/lib` |
| `-DCMAKE_BUILD_TYPE=...` | `Release` | `Debug` or `Release` |

Run the unit tests with `ctest` (via `ctest --preset dev|release`, or plain `ctest` inside the build directory); the suite covers lossless round-trips, ROI-vs-full-decode equality, thread determinism, and container/header corruption handling.

## Usage

The project builds an executable named `xtm`:

```
Usage: xtm <command> [options]

Commands:
  analyze   Evaluate terrain statistics and compression diagnostics
  encode    Compress a raster into the .xtm format
  decode    Decompress an .xtm file (optionally a bounding region) to a raster
  info      Show .xtm file metadata
  verify    Verify a decode against source data
```

### Analyze

```bash
xtm analyze <input.tif> [--scale <value>] [--wavelet]
```

### Encode

Compress an input raster (e.g., GeoTIFF) into the `xtm` format:

```bash
xtm encode <input.tif> -o <output.xtm> [--scale <value>] [--pipeline predictor|wavelet] [--context simple|extended] [--disable-quadtree]
```

### Decode

Decompress an `xtm` file back into a raster. Optionally decode a bounding region:

```bash
xtm decode <input.xtm> -o <output.tif> [--region x y w h]
```

### Precision model

XTM is an **error-bounded** codec, not a bit-exact Float32 codec. Elevations are
quantized to fixed-point integers before coding, so every sample round-trips
within `|z - z_hat| <= scale / 2` units (the CLI default scale is 1.0, i.e.
meter precision; pass `--scale 0.01` for centimeter precision at the cost of
larger files). The quantization is lossless with respect to the quantized
integer grid: decode reproduces the exact grid the encoder produced.

Note: the experimental `--pipeline wavelet` mode is only valid with
`--scale >= 1.0`.

## C++ API

Alongside the CLI, `libxtm` exposes a high-level file-oriented API in the
`xtm::api` namespace (`include/xtm/Api.hpp`). The CLI commands are thin
wrappers around these functions — the API is the single source of truth for
the encode/decode/analyze/info/verify flows.

| Function | Purpose |
|---|---|
| `encode_file(input, output, options)` | Compress any GDAL-readable raster into `.xtm` |
| `decode_file(input, output, options)` | Decompress to GeoTIFF, optionally a bounding region |
| `analyze_file(input, options, analyzer_options)` | Run the selection pipeline and return the decision report |
| `info_file(xtm_path)` | Read the `.xtm` header and block index without decoding payloads |
| `verify_file(xtm_path, tif_path)` | Checksum-only verification (empty `tif_path`) or decode-vs-source comparison |
| `version()` | Version string |

```cpp
#include <xtm/Api.hpp>

// Encode with centimeter precision.
xtm::api::EncodeResult result = xtm::api::encode_file(
    "input.tif", "output.xtm",
    {0.01, xtm::coding::ContextModel::Simple,
     xtm::analyzer::PipelineType::Predictor, /*disable_quadtree=*/false,
     /*num_threads=*/0}); // 0 = hardware concurrency

// Decompress only a bounding region (x, y, width, height).
xtm::api::decode_file("output.xtm", "region.tif",
                      {128, 128, 512, 512, 0});
```

Option structs mirror the CLI flags:

- `EncodeOptions` — 
  - `precision`,
  - `context_model` (`Simple`/`Extended`),
  - `pipeline_type` (`Predictor`/`Wavelet`), 
  - `disable_quadtree`,
  - `num_threads`.
- `DecodeOptions` — 
  - `region_x`/`region_y`/`region_width`/`region_height`
    (width or height `0` = full grid),
  - `num_threads`.

Results: 
- `EncodeResult` (dimensions, `total_blocks`, `predictor_counts`
mapping frozen PredictorId → block count, `output_bytes`, per-stage ms
timings), 
- `DecodeResult` (`blocks_decoded`, dimensions), `FileInfo`
(header + block index, `block_count`, `total_payload_bytes`), `VerifyResult`
(`passed`, counts of checked/mismatched pixels).

Error contract: invalid options throw `std::invalid_argument`; I/O, codec, and
corrupt-file failures throw `std::runtime_error`.

## Python Bindings

The same file-level surface is available from Python via nanobind — no numpy,
no runtime pip dependencies. The module can be built in-tree and used through
`PYTHONPATH` (as wired into CTest):

```bash
cmake -S . -B build/release -DXTM_BUILD_PYTHON=ON
cmake --build build/release --parallel
PYTHONPATH=build/release/lib python3 -c "import xtm; print(xtm.version())"
```

or installed as a wheel (build machine needs cmake, ninja, and GDAL dev
headers, matching the C++ requirements):

```bash
pip install .
```

The artifact lands at `<build>/lib/xtm.abi3.so` (`STABLE_ABI`, portable
across CPython ≥ 3.12). A plain-assert test script
(`tests/python/test_xtm_bindings.py`) is wired into CTest as
`python_bindings` and runs automatically with `ctest` when the option is on.

```python
import xtm

# Encode a raster (centimeter precision) and inspect the result.
res = xtm.encode("grand_canyon.tif", "gc.xtm", precision=0.01)
print(res.total_blocks, res.output_bytes, res.predictor_counts)

# Metadata without decoding payloads.
info = xtm.info("gc.xtm")
print(info.width, info.height, info.precision, info.transform)

# Decode a bounding region (x, y, width, height); region=None = full grid.
xtm.decode("gc.xtm", "gc_roi.tif", region=(0, 0, 512, 512))

# Checksum-only verify, or decode-vs-source comparison when tif is given.
v = xtm.verify("gc.xtm", "grand_canyon.tif")
assert v.passed
```

Functions (all run with the GIL released):

| Function | Signature |
|---|---|
| `encode` | `(input, output, precision=1.0, pipeline="predictor", context="simple", disable_quadtree=False, num_threads=0) -> EncodeResult` |
| `decode` | `(input, output, region=None, num_threads=0) -> DecodeResult` |
| `analyze` | `(input, precision=1.0, wavelets=False, num_threads=0) -> AnalysisReport` |
| `info` | `(path) -> FileInfo` |
| `verify` | `(xtm, tif=None) -> VerifyResult` |
| `version` | `() -> str` |

Notes:

- Predictor IDs are frozen: 0=Gradient, 1=Left, 2=JpegLs, 3=Polynomial,
  4=Gap, 5=LeastSquares (`EncodeResult.predictor_counts` keys).
- Invalid options raise `ValueError`; I/O, codec, and corrupt-file failures
  raise `RuntimeError` (C++ exceptions mapped in `bindings/xtm.cpp`).
- Do **not** build the module with the `dev` preset (TSan+UBSan) — Python is
  not TSan-instrumented; use the `release` preset.
