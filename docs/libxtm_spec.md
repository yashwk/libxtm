# libxtm: Architecture & Technical Specification

## 1. Overview

**libxtm** is a C++ library and associated tooling for efficient storage, compression, decompression, analysis, and streaming of large digital elevation models (DEMs).

The initial development target is the **Copernicus DEM GLO-30** dataset, but libxtm is intended to remain independent of Copernicus, GeoTIFF, GDAL, or any particular terrain source.

The project consists of three primary interfaces:

```text
                         Applications
                              │
             ┌────────────────┼────────────────┐
             │                │                │
             ▼                ▼                ▼
          xtm CLI         Python API        C++ API
             │                │                │
             └────────────────┼────────────────┘
                              ▼
                           libxtm
                              │
                 ┌────────────┴────────────┐
                 ▼                         ▼
             CPU backend              CUDA backend
```

The C++ library is the authoritative implementation. The CLI and Python package are front ends over the same library rather than independent implementations.

The project has two related objectives:
1. Develop an efficient terrain-specific compression codec.
2. Develop XTM as a practical format for storing and randomly accessing large terrain datasets.

Compression ratio is important, but the format should not sacrifice decoding speed, random access, scalability, or terrain quality merely to achieve the smallest possible archive.

---

## 2. Motivation

Global elevation datasets contain enormous amounts of spatial redundancy. Terrain elevations are not independent samples; neighboring samples are strongly correlated because terrain generally changes continuously across space.

A sequence such as:

```text
4200
4204
4208
4212
4216
```

contains much less information than storing five unrelated numbers would imply. A terrain-aware compressor can model the underlying spatial structure and encode only deviations from that structure.

General-purpose compressors operate primarily on the byte representation of the data. **libxtm** instead operates on the terrain itself.

Conceptually:

```text
                 DEM
                  │
                  ▼
    Understand spatial structure
                  │
                  ▼
           Predict terrain
                  │
                  ▼
        Encode prediction error
                  │
                  ▼
    Transform remaining structure
                  │
                  ▼
            Entropy encode
```

The entropy coder should not be responsible for discovering the geometry of the terrain.

---

## 3. Initial Dataset

The first development and benchmark dataset is **Copernicus DEM GLO-30**.

The initial test tile is:
`Copernicus_DSM_COG_10_N27_00_E086_00_DEM.tif`

It covers approximately:

```text
Latitude:   27° N → 28° N
Longitude:  86° E → 87° E
```

This places it in highly mountainous Himalayan terrain, making it deliberately challenging for compression.

### Tile Properties

| Property | Value |
|---|---|
| **Dimensions** | 3600 × 3600 |
| **Samples** | 12,960,000 |
| **Sample type** | Float32 |
| **CRS** | EPSG:4326 |
| **Angular spacing** | 1/3600° |
| **Coverage** | 1° × 1° |
| **Layout** | COG (Cloud Optimized GeoTIFF) |
| **Compression** | DEFLATE |
| **TIFF predictor** | 3 (Floating Point) |

At approximately 27.5° latitude, the physical tile dimensions are roughly:

```text
North–South   ~111 km
East–West     ~99 km
Area          ~11,000 km²
```

The horizontal sample spacing is approximately 30 m.

The uncompressed elevation raster occupies:

$$\text{Uncompressed Size} = 12,960,000 \times 4\text{ bytes} \approx 51.84\text{ MB}$$

before container metadata or additional products.

The existing source is already DEFLATE compressed with a floating-point predictor, so comparisons against the original GeoTIFF are comparisons against an existing compressed representation rather than raw Float32 storage.

---

## 4. Scope

libxtm should eventually support:

- Lossless terrain compression
- Error-bounded terrain compression
- Configurable vertical precision
- Adaptive terrain prediction
- Reversible transforms
- Entropy coding
- Random-access decompression
- Region-of-interest decoding
- Multiresolution terrain
- CPU implementations
- GPU-accelerated implementations
- Streaming large datasets
- C++ applications
- Python/NumPy workflows
- Benchmarking and codec research

The format should not assume that its input originated as GeoTIFF.

---

## 5. Compression Philosophy

The core model is:

$$Z = P + R$$

where:
- $Z$ is the actual elevation
- $P$ is a deterministic prediction
- $R$ is the residual

Therefore:

$$R = Z - P$$

During decompression:

$$Z = P + R$$

The predictor does not have to predict perfectly. It only needs to reduce the entropy of the resulting residual distribution.

### Example

```text
Actual:      4200  4205  4211  4216  4221
Prediction:  4200  4205  4210  4216  4222
Residual:       0     0    +1     0    -1
```

The residual stream is substantially easier to encode than the original elevations.

---

## 6. Lossless vs Error-Bounded Compression

libxtm should support both modes.

### 6.1 Exact Lossless Mode

The goal is:

$$\text{Decode}(\text{Encode}(Z)) = Z$$

for every source sample.

For Float32 input, exact mode means reconstructing the original Float32 values exactly according to the codec’s defined canonical representation. No quantization is permitted.

Predictors, residual transforms, wavelets, entropy coding, and container operations must all be reversible.

### 6.2 Error-Bounded Mode

For many terrain applications, the numerical precision contained in Float32 is far beyond the actual useful precision of a ~30 m DEM.

Instead, select a vertical quantization interval $q$ and encode:

$$Q(z) = \text{round}\left(\frac{z}{q}\right)$$

The reconstructed elevation is:

$$\hat{z} = q \cdot Q(z)$$

For nearest-value quantization, the bound is:

$$|z - \hat{z}| \le \frac{q}{2}$$

Example quantization steps:

```text
q = 0.01 m
q = 0.10 m
q = 0.25 m
q = 0.50 m
q = 1.00 m
q = 2.00 m
```

The rest of the codec remains lossless with respect to the quantized representation. Thus, intentional information loss occurs at one clearly defined stage.

---

## 7. Float32 Precision

The source raster uses Float32, but values printed with many decimal places should not be interpreted as having that many meaningful decimal digits.

Float32 provides approximately 24 bits of significand precision, corresponding to roughly seven significant decimal digits.

At Himalayan elevations around 4,000–8,000 m, Float32 can represent vertical increments on the order of sub-millimeters. That numerical precision does not imply equivalent measurement accuracy.

The effective useful precision depends on:
- DEM acquisition method
- Vertical accuracy
- Horizontal resolution
- Terrain roughness
- Application
- Rendering scale
- Slope and curvature requirements

Therefore libxtm should empirically investigate the relationship between vertical precision, compressed size, and terrain feature preservation.

---

## 8. Fixed-Point Representation

For error-bounded modes, decimal elevations should be converted into integer fixed-point values before most compression stages.

For $0.01\text{ m}$ precision:

$$4231.37\text{ m} \longrightarrow 423137 \quad (\text{scale} = 0.01\text{ m})$$

For $0.1\text{ m}$ precision:

$$4231.4\text{ m} \longrightarrow 42314 \quad (\text{scale} = 0.1\text{ m})$$

The codec can then operate primarily on integers.

### Benefits
- Deterministic arithmetic
- Exact residual reconstruction
- Simpler predictors
- Reversible transforms
- Easier entropy modeling
- Reduced floating-point noise
- Easier CPU/GPU equivalence

---

## 9. Local Reference Elevation

Absolute terrain height often contains information that does not need to be repeated for every sample.

A block containing:

```text
5000.12
5000.18
5000.31
5000.45
```

could use:

$$\text{base} = 5000.00$$

and represent the values relative to that reference.

At centimeter precision:

```text
12
18
31
45
```

Prediction then operates on these local values.

Whether explicit local offsets improve final entropy after prediction should be measured rather than assumed.

---

## 10. Proposed Compression Pipeline

The working pipeline is:

```text
Input DEM
   │
   ▼
1. Ingest + Validation
   │
   ▼
2. Canonical Representation
   │
   ▼
3. Optional Precision Conversion
   │
   ▼
4. Terrain Analysis
   │
   ▼
5. Adaptive Spatial Partitioning
   │
   ▼
6. Adaptive Predictor Selection
   │
   ▼
7. Residual Generation
   │
   ▼
8. Residual Modeling / Decorrelation
   │
   ▼
9. Reversible Wavelet Transform
   │
   ▼
10. Context / Symbol Modeling
   │
   ▼
11. Entropy Coding
   │
   ▼
12. XTM Container
```

Not every stage is guaranteed to remain in the final codec. Each stage must demonstrate measurable benefit.

---

## 11. Ingest and Validation

The input layer reads source data and converts it into libxtm’s canonical terrain representation.

For GeoTIFF sources, GDAL can handle:
- Raster decoding
- Dimensions
- Sample types
- Geotransforms
- CRS
- NoData values
- Metadata

TIFF-specific storage details should not propagate into the compression core.

```text
GeoTIFF ──► GDAL adapter ──► Terrain ──► libxtm codec
```

Other sources can provide `Terrain` directly.

---

## 12. Canonical Terrain Representation

A simplified conceptual C++ representation:

```cpp
struct TerrainView {
    const float* data;

    std::uint32_t width;
    std::uint32_t height;

    GeoTransform transform;
};
```

The real representation may additionally contain:
- Sample format
- Vertical scale
- Geographic extent
- NoData representation
- CRS identifier
- Memory layout

The compression core should operate on this representation rather than GDAL objects.

---

## 13. Terrain Analysis

Before designing the final codec, libxtm needs to characterize real terrain statistically.

For each tile and potentially each region, calculate:

### Elevation Statistics
- Minimum elevation ($z_{\min}$)
- Maximum elevation ($z_{\max}$)
- Mean elevation ($\mu_z$)
- Standard deviation ($\sigma_z$)

### First Differences

**Horizontal:**
$$D_x(x,y) = Z(x,y) - Z(x-1,y)$$

**Vertical:**
$$D_y(x,y) = Z(x,y) - Z(x,y-1)$$

### Second Differences
Useful for understanding curvature and predictor performance.

### Histograms
Measure distributions of:
- Elevation
- Horizontal differences
- Vertical differences
- Predictor residuals
- Transformed coefficients

### Entropy
Estimate Shannon entropy:

$$H(X) = -\sum_{x} p(x) \log_2 p(x)$$

and report results as `bits/sample`. This becomes the main compression metric.

---

## 14. Predictors

A predictor is a deterministic function that estimates the current sample from already known information.

Consider a 2×2 spatial neighborhood:

```text
A  B
C  X
```

where `X` is the sample being encoded.

A predictor computes:

$$P(X) = f(A, B, C, \dots)$$

and stores the residual:

$$R = X - P(X)$$

The decoder runs the same predictor and reconstructs:

$$X = P(X) + R$$

---

## 15. Predictor Candidates

The initial predictor bank should remain small and measurable.

### Left
$$P(X) = C$$

### Above
$$P(X) = B$$

### Average
$$P(X) = \left\lfloor \frac{B + C}{2} \right\rfloor$$

with deterministic integer rounding.

### Gradient
$$P(X) = B + C - A$$

This models a locally planar gradient.

### JPEG-LS Predictor
A nonlinear edge-aware predictor that avoids some gradient predictor failures around discontinuities:

$$P(X) = \begin{cases} \min(B, C) & \text{if } A \ge \max(B, C) \\ \max(B, C) & \text{if } A \le \min(B, C) \\ B + C - A & \text{otherwise} \end{cases}$$

### Plane Predictor
Approximate local terrain as:

$$z = ax + by + c$$

and encode deviations from the plane.

### Higher-Order Predictors
Potential later experiments include quadratic surfaces:

$$z = ax^2 + by^2 + cxy + dx + ey + f$$

These should only be retained if residual reduction exceeds parameter and computational cost.

---

## 16. Adaptive Predictor Selection

Different terrain regions favor different predictors. A flat region, smooth mountain slope, ridge, valley, and cliff should not necessarily use the same model.

For each block, candidate predictors are evaluated. For predictor $P$:

$$R_P = Z - P(Z)$$

Estimate total cost:

$$C(P) = C_{\text{ID}} + C_{\text{parameters}} + C_{\text{residual}}$$

Then select:

$$P^* = \arg\min_P C(P)$$

The goal is not to identify terrain semantically; the goal is to choose the representation requiring the fewest encoded bits.

---

## 17. Adaptive Spatial Partitioning

Terrain complexity varies spatially. A fixed block size may waste metadata in smooth regions while failing to model complicated regions accurately.

A hierarchical partition can begin with large blocks (e.g. 512 × 512) and recursively split:

```text
512 × 512
   │
   └── 256 × 256
        │
        └── 128 × 128
             │
             └── 64 × 64
```

The split decision should ultimately optimize:

$$C = C_{\text{model}} + C_{\text{residual}} + C_{\text{partition}}$$

Split only when:

$$C_{\text{children}} < C_{\text{parent}}$$

This prevents the partitioner from improving prediction while making the final bitstream larger. A minimum block size around 64 × 64 is a reasonable initial experiment, not a fixed specification.

---

## 18. Residual Generation

After predictor selection:

$$R(x,y) = Z(x,y) - P(x,y)$$

Instead of elevations in the thousands of meters, the resulting stream may consist largely of values around zero.

### Example

```text
Actual:      4231  4235  4238  4244
Prediction:  4230  4234  4239  4242
Residual:      +1    +1    -1    +2
```

Residual entropy, not residual magnitude alone, determines compressibility.

---

## 19. Residual Decorrelation

Prediction may not remove all spatial correlation. Residuals themselves can contain patterns, for example:

```text
0  0  0  1  1  1  0  0  0
```

A secondary reversible transform may reduce this structure. Whether this stage is worthwhile should be established experimentally.

---

## 20. Wavelet Transform

Wavelet decomposition remains a major candidate. Instead of necessarily transforming raw terrain, the current working hypothesis is to test wavelets primarily on predictor residuals.

A 2D transform produces:

```text
        Residual
           │
           ▼
    ┌──────┬──────┐
    │  LL  │  LH  │
    ├──────┼──────┤
    │  HL  │  HH  │
    └──────┴──────┘
```

The LL component can recursively decompose.

For lossless operation, the transform must be exactly reversible, likely through integer lifting.

Transform modes can be adaptive:
- `0`: No transform
- `1`: Wavelet level 1
- `2`: Wavelet level 2
- `3`: Wavelet level 3

The encoder chooses whichever produces the smallest total representation. Wavelets are not assumed to improve every block.

---

## 21. Context Modeling

After prediction and transforms, coefficient distributions may depend strongly on context.

Possible context variables include:
- Wavelet subband
- Transform level
- Predictor type
- Neighboring coefficient magnitudes
- Previous residual magnitude

An initial model might use:

$$\text{context} = (\text{subband}, \text{magnitudeClass})$$

rather than a highly complex model.

The number of contexts should remain controlled because context tables themselves introduce memory, initialization, and computational costs.

---

## 22. Symbol Representation

Signed residuals may be mapped to nonnegative integers (ZigZag encoding).

For example:

```text
 0  ──►  0
-1  ──►  1
+1  ──►  2
-2  ──►  3
+2  ──►  4
...
```

Zero runs may also be represented separately when transformed data contains sufficiently many zeros.

Possible decomposition:

```text
coefficients
   │
   ├── zero / nonzero flag
   ├── magnitude
   └── sign
```

Whether this outperforms direct signed-symbol entropy coding must be benchmarked.

---

## 23. Entropy Coding

Candidate entropy coders include:
- **rANS** (Asymmetric Numeral Systems)
- **Range coding**
- **General-purpose compression** (e.g. Zstd) as a reference

rANS is currently a strong candidate because it offers:
- High decode throughput
- Compression close to arithmetic coding
- Pure integer implementation
- SIMD optimization opportunities
- Suitability for independent streams

Zstd should be retained as a baseline. If an elaborate context model plus rANS provides negligible improvement over Zstd, the additional complexity may not be justified.

---

## 24. Random Access

XTM should not optimize only for whole-file compression. A terrain application may need a small geographic region from a very large dataset.

Therefore:

```text
Global terrain
      │
      ▼
Spatial index
      │
      ▼
Required tiles
      │
      ▼
Required blocks
      │
      ▼
Decode
```

Large independently decodable units, or **superblocks**, provide a compromise between compression efficiency and random access.

Within a superblock, smaller adaptive blocks can share models and state. Across superblocks, decoding remains independent.

---

## 25. Cross-Tile Prediction

Adjacent 1° tiles describe a continuous terrain surface. Their boundaries are strongly correlated. Future versions may exploit this redundancy.

However, arbitrary dependency chains such as:

```text
Tile A ──► Tile B ──► Tile C ──► Tile D
```

would damage random access. Cross-tile prediction should therefore be investigated at the region/superblock level while maintaining bounded decoding dependencies.

This is a later optimization and is not required for the initial codec.

---

## 26. Multiresolution Terrain

XTM should eventually support terrain at multiple levels of detail.

This enables:
- Global rendering
- Regional rendering
- Local detailed terrain
- Reduced I/O
- Fast previews
- Terrain streaming

Conceptually:

```text
World
  │
  ├── Coarse terrain (LOD 0)
  │
  ├── Medium terrain (LOD 1)
  │
  └── Full-resolution terrain (LOD 2)
```

A multiresolution representation may eventually integrate naturally with the wavelet hierarchy rather than storing independent raster pyramids. This requires further research.

---

## 27. XTM Container

The codec and container should remain conceptually separate. The **codec** converts terrain into compressed streams; the **XTM format** organizes those streams for storage and retrieval.

A conceptual structure:

```text
XTM File
│
├── Fixed Header
│   ├── Magic Bytes ("XTM\0")
│   ├── Format Version
│   ├── Flags
│   └── Codec Version
│
├── Dataset Metadata
│   ├── CRS
│   ├── Dimensions (width, height)
│   ├── Resolution (x_scale, y_scale)
│   ├── Geographic Extent
│   ├── Sample Representation (Float32, Int16, etc.)
│   └── Precision Mode
│
├── Spatial Index (R-Tree / Quadtree)
│
├── Tile Directory
│   ├── Tile ID
│   ├── Byte Offset
│   ├── Compressed Size
│   └── Block Metadata
│
└── Data Blocks
    ├── Tile 0
    │   ├── Partition Structure
    │   ├── Predictor Metadata
    │   ├── Transform Metadata
    │   └── Entropy Streams
    └── Tile 1 ...
```

Geographic information that can be mathematically derived should not be redundantly stored for every tile.

---

## 28. Decoder Pipeline

The decoder is the inverse of the encoder:

```text
XTM Stream
   │
   ▼
Read header / index
   │
   ▼
Locate required superblocks
   │
   ▼
Entropy decode
   │
   ▼
Symbol reconstruction
   │
   ▼
Inverse wavelet transform
   │
   ▼
Inverse residual transform
   │
   ▼
Predictor reconstruction
   │
   ▼
Canonical samples
   │
   ▼
Inverse scale (if required)
   │
   ▼
Output Terrain
```

The decoder should ultimately support both full tile and regional decoding:

```cpp
// Full decode
auto terrain = decoder.decode();

// Region decode
auto terrain = decoder.decode_region(bounds);
```

The latter is essential for large terrain datasets.

---

## 29. GPU Acceleration

Much of the pipeline is highly parallel. A 3600 × 3600 tile contains nearly 13 million samples, making it suitable for GPU processing.

GPU-friendly stages include:
- Precision conversion
- Statistical calculations
- Gradient computations
- Predictor evaluation
- Residual generation
- Wavelet transforms
- Histogram generation
- Block metrics
- Entropy estimation

Each terrain sample or block can often be processed independently.

---

## 30. Predictor Search on GPU

Adaptive prediction is particularly attractive for GPU acceleration.

For each block:

```text
                     Block
                       │
       ┌───────────────┼───────────────┐
       ▼               ▼               ▼
     Left          Gradient          Plane
       │               │               │
       ▼               ▼               ▼
     Cost            Cost            Cost
       └───────────────┼───────────────┘
                       ▼
                   Select Best
```

Candidate predictors can be evaluated in parallel across blocks, and costs reduced to select the optimal representation.

---

## 31. Wavelets on GPU

2D wavelets involve regular horizontal and vertical operations and are well suited to GPU execution.

Conceptually:

```text
VRAM Buffer
   │
   ▼
Horizontal Transform
   │
   ▼
Vertical Transform
   │
   ▼
LL / LH / HL / HH Subbands
```

Shared memory and tiled execution may provide significant throughput improvements.

---

## 32. CPU/GPU Division

Not every operation needs GPU acceleration.

A mature architecture division:

```text
CPU Operations
  ├── File I/O
  ├── Metadata parsing
  ├── Job scheduling
  └── Container writing
        │
        ▼
GPU Operations
  ├── Quantization
  ├── Statistical calculation
  ├── Predictor search
  ├── Residual generation
  ├── Wavelet transforms
  └── Model statistics calculation
        │
        ▼
CPU / GPU
  └── Entropy coding (rANS / Zstd)
        │
        ▼
Output XTM Bitstream
```

Entropy coding can initially remain CPU-side. Parallel rANS implementations can be investigated later.

---

## 33. CPU First

Despite GPU potential, the first implementation should be CPU-only.

Development order:

```text
Correct CPU Implementation
            │
            ▼
 Reference Results
            │
            ▼
 Round-Trip Tests
            │
            ▼
 Benchmark & Profile
            │
            ▼
  GPU Acceleration
```

The CPU implementation provides the reference behavior against which CUDA implementations can be tested. Correctness takes priority over acceleration.

---

## 34. Project Architecture

libxtm should be developed as:
1. A C++ library (core engine)
2. A command-line executable (`xtm`)
3. Python bindings (`xtm` Python package)

```text
                    ┌─────────────┐
                    │   xtm CLI   │
                    └──────┬──────┘
                           │
                           ▼
Python API ───────────► libxtm ◄────────── C++ Applications
                           │
                  ┌────────┴────────┐
                  ▼                 ▼
                 CPU               CUDA
```

No codec implementation should be duplicated across interfaces.

---

## 35. C++ API

The high-level API should support standard encoding:

```cpp
#include <xtm/xtm.hpp>

xtm::EncodeOptions options;
options.mode = xtm::Mode::Lossless;
options.backend = xtm::Backend::Auto;

xtm::encode("terrain.tif", "terrain.xtm", options);
```

Decoding:

```cpp
auto terrain = xtm::decode("terrain.xtm");
```

Region decoding:

```cpp
auto terrain = decoder.decode_region(bounds);
```

Applications should also be able to compress terrain already in memory without passing through GDAL.

---

## 36. Command-Line Interface

The executable is named `xtm`.

### Commands

```bash
xtm analyze terrain.tif
xtm encode terrain.tif terrain.xtm
xtm decode terrain.xtm terrain.tif
xtm info terrain.xtm
xtm verify terrain.xtm terrain.tif
xtm benchmark terrain.tif
```

### Encoding Options

```bash
xtm encode input.tif output.xtm --lossless
xtm encode input.tif output.xtm --precision 0.1
xtm encode input.tif output.xtm --backend cpu
xtm encode input.tif output.xtm --backend cuda
xtm encode input.tif output.xtm --threads 16
```

The CLI should primarily:
1. Parse arguments
2. Construct libxtm options
3. Call libxtm
4. Report results

Compression logic does not belong in `main.cpp`.

---

## 37. Python Bindings

Python bindings are important for both users and codec development.

### High-Level API

```python
import xtm

xtm.encode(
    "terrain.tif",
    "terrain.xtm",
    precision=0.1,
    backend="cuda"
)
```

Decoding:

```python
dem = xtm.decode("terrain.xtm")

print(dem.shape)  # (3600, 3600)
print(dem.dtype)  # float32
```

The bindings can be implemented using **nanobind** or **pybind11**.

---

## 38. Python Research API

Python should expose individual experimental components for research:

```python
import xtm

dem = xtm.load("himalaya.tif")

residual = xtm.predict(dem, predictor="gradient")
print("Entropy:", xtm.entropy(residual))
```

### Predictor Experiments

```python
for predictor in xtm.predictors():
    residual = xtm.predict(dem, predictor)
    print(f"{predictor:12s}: {xtm.entropy(residual):.3f} bits/sample")
```

### Block Experiments

```python
for block_size in [64, 128, 256, 512]:
    result = xtm.analyze(dem, block_size=block_size)
    print(f"Block {block_size:3d}: {result.bits_per_sample:.3f} bits/sample")
```

This makes Python the research environment while C++ remains the core implementation.

---

## 39. Zero-Copy Python Integration

Large terrain arrays should not be copied unnecessarily.

```text
Avoid:   NumPy buffer ──► Copy ──► std::vector ──► Codec
Prefer:  NumPy buffer ──► TerrainView ──► libxtm
```

The binding passes pointer, shape, stride, and sample type directly into C++.

Future CUDA support could potentially integrate directly with GPU-resident arrays from systems such as CuPy or PyTorch.

---

## 40. GDAL Integration

GDAL belongs at the I/O boundary:

```text
GeoTIFF ──► GDAL ──► Terrain ──► libxtm
```

The codec should not depend conceptually on TIFF structures. This allows future input from:
- Copernicus DEM
- SRTM
- LiDAR-derived rasters
- Procedural terrain
- Application memory
- NumPy arrays
- Custom raster formats

---

## 41. Proposed Repository Structure

```text
libxtm/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
│
├── include/
│   └── xtm/
│       ├── xtm.hpp
│       ├── Encoder.hpp
│       ├── Decoder.hpp
│       ├── Terrain.hpp
│       ├── Options.hpp
│       └── Error.hpp
│
├── src/
│   ├── Encoder.cpp
│   ├── Decoder.cpp
│   │
│   ├── terrain/
│   │   ├── Terrain.cpp
│   │   └── Quantization.cpp
│   │
│   ├── predictor/
│   │   ├── Predictor.hpp
│   │   ├── Left.cpp
│   │   ├── Gradient.cpp
│   │   ├── JpegLs.cpp
│   │   └── Plane.cpp
│   │
│   ├── partition/
│   │   └── Quadtree.cpp
│   │
│   ├── transform/
│   │   └── Wavelet.cpp
│   │
│   ├── entropy/
│   │   ├── Model.cpp
│   │   └── Rans.cpp
│   │
│   ├── container/
│   │   ├── Reader.cpp
│   │   ├── Writer.cpp
│   │   └── Index.cpp
│   │
│   └── io/
│       └── GDAL.cpp
│
├── backends/
│   ├── cpu/
│   │   ├── Predictor.cpp
│   │   └── Wavelet.cpp
│   │
│   └── cuda/
│       ├── Predictor.cu
│       ├── Statistics.cu
│       └── Wavelet.cu
│
├── apps/
│   └── xtm/
│       ├── main.cpp
│       ├── EncodeCommand.cpp
│       ├── DecodeCommand.cpp
│       ├── AnalyzeCommand.cpp
│       └── BenchmarkCommand.cpp
│
├── bindings/
│   └── python/
│       ├── module.cpp
│       └── CMakeLists.txt
│
├── tests/
│   ├── predictor/
│   ├── transform/
│   ├── codec/
│   └── roundtrip/
│
├── benchmarks/
├── tools/
└── python/
    ├── examples/
    └── notebooks/
```

---

## 42. xtm analyze Command

The first development tool is the terrain analyzer.

### Usage
```bash
xtm analyze Copernicus_DSM_COG_10_N27_00_E086_00_DEM.tif
```

### Output Report Format

```text
SOURCE METADATA
  Dimensions:               3600 x 3600
  Samples:                  12,960,000
  Sample type:              Float32
  Raw size:                 51.84 MB
  Compressed source size:   38.42 MB
  CRS:                      EPSG:4326
  Resolution:               0.000277778 deg (~30 m)

ELEVATION STATISTICS
  Minimum:                  210.50 m
  Maximum:                  8848.86 m
  Mean:                     4231.45 m
  Std Dev:                  1120.30 m
  Unique values:            8,432,109
  Shannon Entropy:          18.42 bits/sample

FLOAT PRECISION
  Fractional distribution:  [... breakdown ...]
  Float32 bit structure:    [... breakdown ...]
  Effective precision:      0.01 m
  Quantization error:       0.002 m

SPATIAL DIFFERENCES
  ΔX entropy:               7.84 bits/sample
  ΔY entropy:               7.91 bits/sample
  Second-diff entropy:      6.12 bits/sample
  Gradient distribution:    [... breakdown ...]
  Local variance:           [... breakdown ...]

PREDICTOR PERFORMANCE (ENTROPY)
  Left:                     7.84 bits/sample
  Above:                    7.91 bits/sample
  Average:                  7.20 bits/sample
  Gradient:                 6.12 bits/sample
  JPEG-LS:                  5.95 bits/sample
  Plane:                    5.80 bits/sample

TRANSFORM PERFORMANCE
  None:                     5.80 bits/sample
  Wavelet L1:               4.90 bits/sample
  Wavelet L2:               4.45 bits/sample
  Wavelet L3:               4.20 bits/sample

COMPRESSION RESULTS SUMMARY
  Raw Float32 size:         51.84 MB
  Original GeoTIFF size:    38.42 MB
  XTM compressed size:      18.20 MB
  Bitrate:                  3.85 bits/sample
  Compression Ratio:        2.85:1 vs Raw (2.11:1 vs GeoTIFF)
  Encode Throughput:        145.2 MB/s
  Decode Throughput:        410.8 MB/s
```

---

## 43. Precision Analysis

The analyzer evaluates multiple vertical precisions:
- Exact (lossless Float32)
- 0.01 m
- 0.10 m
- 0.25 m
- 0.50 m
- 1.00 m
- 2.00 m
- 5.00 m

For each precision, measure:
- Compressed size
- `bits/sample`
- Elevation RMSE
- MAE (Mean Absolute Error)
- Maximum elevation error ($E_{\max}$)
- Slope error
- Curvature error
- Ridge/valley preservation

This produces a terrain-specific rate-distortion curve.

---

## 44. Elevation Error Metrics

For original elevation $z$ and reconstructed elevation $\hat{z}$:

$$\text{RMSE}_z = \sqrt{ \frac{1}{N} \sum_{i,j} (z_{ij} - \hat{z}_{ij})^2 }$$

$$\text{MAE} = \frac{1}{N} \sum_{i,j} |z_{ij} - \hat{z}_{ij}|$$

$$E_{\max} = \max_{i,j} |z_{ij} - \hat{z}_{ij}|$$

These metrics alone are insufficient to fully characterize terrain quality.

---

## 45. Geometric Error Metrics

Terrain applications often care more about surface shape than absolute elevation.

Slope is derived from directional gradients:

$$g_x = \frac{\partial z}{\partial x}, \quad g_y = \frac{\partial z}{\partial y}$$

Quantization modifies gradients and affects:
- Slope
- Surface normals
- Terrain shading / lighting
- Line-of-sight / visibility
- Hydrological flow routing

Curvature is even more sensitive because it involves second derivatives.

Therefore XTM quality evaluation includes geometric metrics alongside elevation RMSE.

---

## 46. Adaptive Precision (Future Direction)

Precision could eventually vary spatially across a tile:

```text
Terrain
   │
   ▼
Local Analysis
   ├── Smooth plateau  ──► 1.0 m precision
   ├── Normal terrain  ──► 0.5 m precision
   └── Sharp ridge     ──► 0.1 m precision
```

The objective is to select the coarsest representation satisfying geometric error limits.

Instead of specifying precision purely by value (e.g. `precision = 0.5m`), XTM will support quality constraints:
- Maximum elevation error
- Maximum slope error
- Maximum geometric distortion

This makes XTM an error-bounded terrain codec rather than simply a quantized raster format.

---

## 47. Compression Target

An early goal was compressing the global Copernicus DEM dataset to below ~10 GB. This remains a useful headline benchmark, but is **not a strict design requirement**.

A format that compresses the global dataset to 20–40 GB while offering:
- Fast GPU/CPU decoding
- High-performance random access
- Native multiresolution streaming
- Predictable error bounds
- Clean C++/Python integration

is substantially more useful than a 10 GB file optimized purely for archive size.

Optimization optimizes a composite utility function:

$$\text{Utility} = f(\text{compression}, \text{decodeSpeed}, \text{randomAccess}, \text{quality}, \text{complexity})$$

---

## 48. Scale of Regional Datasets

One GLO-30 tile covers:

$$\sim 10,000\text{--}12,000\text{ km}^2$$

depending on latitude. At Himalayan latitude (~27.5° N):

$$\sim 111\text{ km} \times \sim 99\text{ km}$$

The Himalayan region requires ~80–120 tiles. At ~52 MB raw elevation data per tile, 100 tiles correspond to:

$$\sim 5.2\text{ GB uncompressed Float32 data}$$

India-scale coverage spans several hundred tiles (~20–40 GB raw). This makes regional datasets practical targets for local high-performance XTM storage and experimentation.

---

## 49. Benchmark Philosophy

Every optimization must answer:

> *How many bits did this feature save, and what did it cost?*

### Primary Metric
- `bits/sample`

### Secondary Metrics
- Compression ratio
- Total compressed bytes
- Encode throughput (MB/s)
- Decode throughput (MB/s)
- Peak RAM usage
- Peak VRAM usage
- Random-access latency

### Error-Bounded Metrics
- RMSE
- MAE
- Maximum Error ($E_{\max}$)
- Slope Error
- Curvature Error

---

## 50. Development Strategy

Develop incrementally:

```text
V0  ──►  V1  ──►  V2  ──►  V3  ──►  V4  ──►  V5  ──►  V6  ──►  V7  ──►  V8  ──►  V9  ──► V10  ──► V11  ──► V12
```

### V0 — Analyzer
GeoTIFF ──► Terrain ──► Statistics
- GDAL loading
- Canonical `Terrain` representation
- Min/Max, Mean/StdDev, Histograms
- Fractional precision analysis
- Shannon entropy, first differences

### V1 — Basic Predictors
- Implement: Left, Above, Average, Gradient
- Measure residual entropy

### V2 — Predictor Benchmarking
- Add: JPEG-LS, Plane, experimental higher-order models
- Compare `bits/sample` vs execution time

### V3 — Fixed Blocks
- Apply predictors independently to blocks (64×64, 128×128, 256×256, 512×512)
- Measure metadata overhead vs residual entropy

### V4 — Adaptive Predictors
- Select optimal predictor per block based on rate-cost estimation

### V5 — Adaptive Partitioning
- Quadtree hierarchical block subdivision

### V6 — Reversible Wavelets
- Integer lifting wavelets (L1, L2, L3 levels) per block

### V7 — Context Modeling
- Introduce residual/coefficient context modeling

### V8 — Entropy Codec
- Implement rANS / Range Coding; benchmark against Zstd

### V9 — XTM Container
- Header, metadata, block index, streams, checksums

### V10 — Random Access
- Superblocks, spatial indexing, partial tile decoding

### V11 — CUDA Acceleration
- GPU acceleration for statistics, predictor search, wavelets, histograms

### V12 — Advanced Features
- Multiresolution pyramids, cross-tile prediction, adaptive precision

---

## 51. Testing Strategy

Lossless codec stages require strict round-trip unit testing:

$$\text{Decode}(\text{Encode}(X)) = X$$

Test cases must cover:
- Flat terrain
- Constant slopes / ramps
- Negative elevations
- Extreme value ranges
- Synthetic random noise
- Real Copernicus DEM tiles
- Tile boundary edge conditions
- Non-standard image dimensions
- Precision conversions
- Predictor block boundaries
- Transform block boundaries

CPU and CUDA implementations must be verified against identical reference outputs.

---

## 52. Initial Himalayan Benchmark

Tile `N27_00_E086_00` serves as the primary development benchmark:
- Resolution: 3600 × 3600
- Terrain: High-relief Himalayan mountains

It provides challenging terrain that exposes model weaknesses hidden by flatter areas.

Complementary benchmark terrain types:
- Flat plains / coastal regions
- Sand dunes / deserts
- Rolling hills
- Moderate mountain ranges
- Urban DSMs
- Mixed ocean/island tiles

---

## 53. Guiding Design Principles

1. **Terrain First:** Model underlying spatial geometry, not just arbitrary byte streams.
2. **Measure Everything:** Every stage must quantitatively prove its bitrate or speed benefit.
3. **CPU Reference First:** Correct CPU implementation precedes CUDA acceleration.
4. **Single Codec Engine:** CLI, Python, and C++ bindings share the exact same core C++ library.
5. **Keep I/O Separate:** GDAL is an adapter layer, not part of the compression codec.
6. **Explicit Loss Control:** Exact mode stays 100% exact; error-bounded loss is strictly controlled by explicit quantization parameters.
7. **Prioritize Random Access:** Enable direct application querying without full-file decompression.
8. **Decode Speed Matters:** Fast decompression and low latency are prioritized over marginal size gains.
9. **Avoid Premature Complexity:** Reject features whose compression gains do not justify their implementation cost.

---

## 54. Long-Term Architecture

```text
                       XTM TERRAIN SYSTEM
                          Applications
                               │
              ┌────────────────┼────────────────┐
              ▼                ▼                ▼
           C++ API         Python API        xtm CLI
              │                │                │
              └────────────────┼────────────────┘
                               ▼
                            libxtm
                               │
        ┌──────────────────────┼──────────────────────┐
        ▼                      ▼                      ▼
      Codec                 Terrain                 Index
        │                      │                      │
  ├── predictors         ├── representation     ├── tiles
  ├── partitioning       ├── precision          ├── regions
  ├── transforms         ├── metadata           └── LOD
  ├── contexts           └── geometry
  └── entropy
        │
  ┌─────┴─────┐
  ▼           ▼
 CPU        CUDA
```

The project begins as a terrain compression experiment. The architecture leaves room for it to become a high-performance terrain storage and streaming system supporting global DEM datasets, simulations, visualization, GIS workflows, and other applications requiring efficient access to large elevation fields.

---

## 55. Immediate Next Milestone

The immediate objective is **not to write the XTM compressor**, but to answer:

> *What information actually exists in the source terrain, and how compressible is that information?*

### Implementation Plan

```text
libxtm
  ├── Terrain
  ├── GDALReader
  ├── Analyzer
  └── Predictor experiments

xtm CLI
  └── analyze command

Python
  └── xtm.analyze()
```

### Analysis Requirements on `Copernicus_DSM_COG_10_N27_00_E086_00_DEM.tif`

1. Actual elevation range ($z_{\min}, z_{\max}$)
2. Float32 fractional bit structure
3. Effective source precision
4. Elevation entropy
5. $\Delta X$ entropy
6. $\Delta Y$ entropy
7. Residual entropy for basic predictors
8. Error introduced at: $0.01\text{ m}, 0.10\text{ m}, 0.25\text{ m}, 0.50\text{ m}, 1.00\text{ m}$
9. Resulting slope / geometric error
10. Estimated overall compressibility

Those measurements determine the next codec development stage.

### Iterative Methodology

```text
  Measure
     │
     ▼
Hypothesize
     │
     ▼
 Implement
     │
     ▼
 Benchmark
     │
     ▼
Keep / Discard
     │
     ▼
  Repeat
```

This measurement-driven process guides libxtm from a simple DEM analyzer toward the final XTM codec without locking the project into premature assumptions.
