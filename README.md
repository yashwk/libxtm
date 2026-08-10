# libxtm

`libxtm` is a C++20 library and command-line utility for compression, encoding, decoding, and analysis of terrain/elevation data. It provides a specialized `.xtm` format designed for efficient storage and random-access retrieval of geospatial terrain data.

## Features

- **Specialized Terrain Compression**: predictive coding tailored for terrain data, with optional reversible CDF 5/3 wavelets.
- **Predictors**: six deterministic models — Left, Gradient, JPEG-LS, GAP, Polynomial, and Least Squares — chosen per block by a cost estimator.
- **Quadtree Partitioning**: 512×512 → 64×64 hierarchical partitioning driven by a rate-cost split rule.
- **Split-Precision Coding**: sub-meter scales encode meter and precision planes independently (`--scale 0.01` for centimeter accuracy).
- **Geospatial Support**: GDAL-based I/O (GeoTIFF etc.) with full geotransform and WKT projection preservation.
- **Region of Interest (ROI) Decoding**: decode only a bounding region from the `.xtm` file without uncompressing the dataset.
- **Deterministic Output**: parallel encodes produce byte-identical files regardless of thread scheduling.
- **Comprehensive Analyzer**: terrain statistics, predictor performance, and compression diagnostics via `xtm analyze`.
- **Error-bounded**: quantized fixed-point codec with `|z − ẑ| ≤ scale/2` guarantee (not bit-exact Float32).

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

- **C++20 compiler** (GCC, Clang, or MSVC)
- **CMake** >= 3.20 and **Ninja** (when using the presets)
- **GDAL** library
- (Optional) **GoogleTest** (fetched automatically via CMake if tests are enabled and GTest is not found system-wide)

## Building the Project

`libxtm` uses CMake. Two presets are provided in `CMakePresets.json`:

| Preset | Build dir | Type | Flags |
|---|---|---|---|
| `dev` | `build/dev` | Debug | ASan + UBSan, export compile commands |
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
- **Warnings as errors**: `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) or `/W4 /WX` (MSVC).
- **Release optimizations**: `-O3 -march=native -mtune=native` plus LTO when supported (GCC/Clang).
- **ccache** is used automatically when detected (zero-config; skipped silently otherwise).
- **Output layout**: executables in `build/bin` (`xtm`), libraries in `build/lib` (`libxtm_core`).
- **Compile database**: with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (on in both presets), `compile_commands.json` is symlinked to the project root for IDE/clangd.
- **Install**: `cmake --install build` (GNUInstallDirs; installs `xtm_core`, the `xtm` binary, and headers).

### Build Options

| Option | Default | Description |
|---|---|---|
| `-DBUILD_SHARED_LIBS=ON` | `OFF` | Build `xtm_core` as a shared library instead of static |
| `-DBUILD_TESTS=ON/OFF` | `ON` | Build the CTest suite; GoogleTest v1.16.0 is fetched from GitHub if not found system-wide |
| `-DENABLE_ASAN=ON` | `OFF` | Address Sanitizer |
| `-DENABLE_TSAN=ON` | `OFF` | Thread Sanitizer |
| `-DENABLE_MSAN=ON` | `OFF` | Memory Sanitizer |
| `-DENABLE_UBSAN=ON` | `OFF` | Undefined Behavior Sanitizer |
| `-DCMAKE_BUILD_TYPE=...` | `Release` | `Debug`, `Release`, or `RelWithDebInfo` |

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
