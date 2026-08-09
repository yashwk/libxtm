# libxtm

`libxtm` (eXtended Terrain Model) is a C++20 library and command-line utility for advanced compression, encoding, decoding, and analysis of terrain/elevation data. It provides a specialized `.xtm` format designed for efficient storage and access of geospatial terrain data.

## Features

- **Specialized Terrain Compression**: Implements predictive coding and wavelet transforms optimized for terrain data.
- **Predictors**: Offers various prediction models to minimize data redundancy, including Least Squares, JPEG-LS, Adaptive Gradient, Local Slope, and more.
- **Quadtree Partitioning**: Supports hierarchical data representation using quadtrees for efficient spatial querying and compression.
- **Geospatial Support**: Leverages GDAL for robust I/O, seamlessly handling formats like GeoTIFF and preserving spatial references/geotransforms.
- **Region of Interest (ROI) Decoding**: Decode only a specific spatial region from the `.xtm` file without uncompressing the entire dataset.
- **Comprehensive Analyzer**: Built-in tools for extracting terrain statistics and evaluating compression metrics.

## Architecture & Codebase Structure

The project is structured into multiple functional modules:

- `apps/xtm/`: Contains the entry point and command implementations for the `xtm` CLI executable.
- `include/xtm/`: Public headers, including core data structures (`TerrainBuffer`, `TerrainView`, `GeoTransform`).
- `src/analyzer/`: Implements terrain statistics extraction and predictor selection logic.
- `src/coding/`: Provides entropy coding (Range Coder) and Context Modeling mechanisms.
- `src/container/`: Manages the binary representation and headers of the `.xtm` format.
- `src/io/`: GDAL-based readers and writers for handling external geospatial formats.
- `src/partition/`: Logic for spatial partitioning, including Quadtree implementations.
- `src/predictor/`: A suite of predictive algorithms (e.g., `Above`, `Average`, `Gradient`, `JpegLs`, `LeastSquares`, `Left`, `LocalSlope`, `Plane`).
- `src/terrain/`: Core terrain operations including data quantization.
- `src/transform/`: Domain transforms such as Wavelet encoding.

## Requirements

To build and use `libxtm`, you will need:

- **C++ Compiler** with C++20 support (GCC, Clang, or MSVC)
- **CMake** >= 3.20
- **GDAL** library
- **Threads**
- (Optional) **GoogleTest** (fetched automatically via CMake if tests are enabled)

## Building the Project

`libxtm` uses CMake as its build system. You can compile it as follows:

```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j
```

### Build Options

- `-DBUILD_SHARED_LIBS=ON`: Build as a shared library instead of a static one.
- `-DBUILD_TESTS=ON/OFF`: Enable or disable building unit tests (default is `ON`).
- `-DENABLE_ASAN=ON`: Enable Address Sanitizer for debugging memory issues.
- `-DENABLE_TSAN=ON`: Enable Thread Sanitizer for debugging threading issues.

## Usage

The project builds an executable named `xtm` which can be used to interact with terrain data.

### Commands

#### Analyze
Run the terrain analyzer to evaluate a dataset and print relevant statistics.
```bash
xtm analyze <input.tif> [--scale <value>]
```

#### Encode
Compress an input raster (e.g., GeoTIFF) into the `xtm` format.
```bash
xtm encode <input.tif> -o <output.xtm> [--scale <value>]
```

#### Decode
Decompress an `xtm` file back into a raster format. You can optionally decode a specific bounding region.
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
