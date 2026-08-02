# libxtm Development & Implementation Master Plan

## 1. Executive Summary & Vision

**libxtm** (Extreme Terrain Matrix Codec) is a high-performance C++20 library, command-line tool (`xtm`), and Python package (`xtm`) designed for domain-specific terrain elevation compression, random-access storage, spatial indexing, and streaming.

The primary target dataset for benchmarking is **Copernicus DEM GLO-30** (e.g., tile `N27_00_E086_00`), with an architectural design that remains source-agnostic, format-independent, and extensible across diverse terrain types (mountains, plateaus, coastal regions, urban DSMs).

### Primary Engineering Objectives
1. **Domain-Specific Codec**: A compression engine exploiting 2D spatial continuity, elevation correlation, and geometric properties of terrain (achieving maximum compression efficiency while preserving vertical accuracy and surface geometry).
2. **High-Performance Container (.xtm / .xtem)**: A single-file or tile-directory container enabling sub-millisecond random-access region decoding (`decode_region`), spatial R-Tree / Quadtree indexing, and implicit global grid topology.
3. **Multi-Platform Ecosystem**: C++20 core engine with zero-copy C++ API, Python bindings (`pybind11`/`nanobind`) for research & NumPy workflows, and a CLI application (`xtm`).
4. **Measurement-Driven Development**: An incremental roadmap (V0 to V12) where every feature must justify its bit savings and runtime cost against baseline compressors (e.g., GDAL COG DEFLATE, Zstd).

---

## 2. System Architecture & Modular Design

The system enforces clear boundaries between I/O, core data representations, predictors/transforms, entropy coders, and container formats.

```text
                                Applications
                                     │
                    ┌────────────────┼────────────────┐
                    ▼                ▼                ▼
                 xtm CLI         Python API        C++ API
                    │                │                │
                    └────────────────┼────────────────┘
                                     ▼
                                  libxtm
                                     │
        ┌────────────────────────────┼────────────────────────────┐
        ▼                            ▼                            ▼
  Codec Engine                 Terrain Core                Container & Index
        │                            │                            │
  ├── Ingest & Validation      ├── Canonical TerrainView    ├── Fixed Header
  ├── Quantization / Fixed     ├── Bounding Box / CRS       ├── Spatial Index
  ├── Predictors (6 types)     ├── Bit-width representation ├── Tile Directory
  ├── Partitioning (Quadtree)  └── Geometry / Normals       └── Superblock Streams
  ├── Reversible Wavelets
  ├── Context Modeling
  └── Entropy Coding (rANS)
        │
  ┌─────┴─────┐
  ▼           ▼
 CPU        CUDA
```

---

## 3. Data Flow & Compression Lifecycle

Each elevation sample passes through a multi-stage deterministic transformation pipeline:

```text
[Stage 1: Raw DEM Ingest]        Float32 elevation grid (e.g. 3600 × 3600)
           │
           ▼
[Stage 2: Quantization / Fixed]   Convert Float32 to Fixed-Point Integer:
                                 q(z) = round(z / scale)
           │
           ▼
[Stage 3: Spatial Partitioning]  Hierarchical Quadtree partitioning into blocks
                                 (512×512 down to 64×64)
           │
           ▼
[Stage 4: Adaptive Prediction]   Evaluate Left, Above, Average, Gradient, JPEG-LS, Plane
                                 Compute residual: R = Z - P(Z)
           │
           ▼
[Stage 5: Wavelet Decorrelation] CDF 5/3 Integer Lifting Wavelet (L1, L2, L3 subbands)
           │
           ▼
[Stage 6: Symbol Mapping]        ZigZag encoding: signed ints -> unsigned symbols
           │
           ▼
[Stage 7: Context & Entropy]     Subband/Magnitude context modeling + rANS / Zstd coder
           │
           ▼
[Stage 8: Container Packaging]   Serialize bitstreams into .xtm superblocks with spatial index
```

---

## 4. Phase-by-Phase Development Strategy (V0 to V12)

The project will be built incrementally in 13 distinct phases. Each phase establishes a working, benchmarked milestone before proceeding to the next.

```text
V0 ──► V1 ──► V2 ──► V3 ──► V4 ──► V5 ──► V6 ──► V7 ──► V8 ──► V9 ──► V10 ──► V11 ──► V12
```

### Phase V0: Foundation, Ingest & Terrain Analyzer Tooling
- **Goals**: Build the core build system (CMake, C++20), canonical memory structures (`TerrainView`, `TerrainBuffer`), GDAL ingest adapter, and `xtm analyze` CLI command.
- **Key Modules**:
  - `include/xtm/Terrain.hpp`: Canonical header containing data pointer, shape, stride, resolution, CRS, NoData value.
  - `src/io/GDALReader.cpp`: GDAL adapter for reading COG GeoTIFF rasters into `TerrainBuffer`.
  - `src/analyzer/Analyzer.cpp`: Empirical calculator for:
    - Min, Max, Mean, StdDev, Unique Value Count.
    - Shannon Entropy $H(X) = -\sum p(x) \log_2 p(x)$.
    - First spatial differences $\Delta X, \Delta Y$ and second differences.
    - Float32 bit-level fractional structure analysis.
- **Deliverables**: `xtm analyze <input.tif>` producing a complete statistical diagnostic report.

### Phase V1: Basic Deterministic Predictors
- **Goals**: Implement first-order spatial predictors operating on fixed-point terrain grids.
- **Predictors**:
  - **Left**: $P(X) = C$
  - **Above**: $P(X) = B$
  - **Average**: $P(X) = \lfloor (B + C) / 2 \rfloor$
  - **Gradient (Planar)**: $P(X) = B + C - A$
- **Verification**: Ensure zero residual loss in round-trip $Z = P + R \implies Z = P + (Z - P)$. Compute residual entropy for each predictor.

### Phase V2: Advanced & Surface-Fitting Predictors
- **Goals**: Implement edge-aware and parametric surface predictors.
- **Predictors**:
  - **JPEG-LS Predictor**:
    $$P(X) = \begin{cases} \min(B, C) & \text{if } A \ge \max(B, C) \\ \max(B, C) & \text{if } A \le \min(B, C) \\ B + C - A & \text{otherwise} \end{cases}$$
  - **Plane Predictor**: Fit local surface $z = ax + by + c$ per block using least squares.
  - **Quadratic Surface Predictor**: Fit $z = ax^2 + by^2 + cxy + dx + ey + f$.
- **Benchmarking**: Compare entropy reduction against parameter encoding overhead.

### Phase V3: Fixed-Block Spatial Partitioning
- **Goals**: Divide raster tiles into fixed-size grid blocks ($64 \times 64$, $128 \times 128$, $256 \times 256$, $512 \times 512$) to evaluate local predictor performance vs global predictor performance.
- **Deliverables**: Block-level residual metrics and metadata overhead analysis.

### Phase V4: Adaptive Predictor Selection Engine
- **Goals**: Enable each block to choose its optimal predictor dynamically based on rate-cost estimation.
- **Cost Function**:
  $$C(P) = C_{\text{ID}} + C_{\text{params}} + C_{\text{residual\_entropy}}$$
  $$P^* = \arg\min_{P} C(P)$$
- **Deliverables**: Predictor selection map generator and bit-rate estimator.

### Phase V5: Adaptive Hierarchical Partitioning (Quadtree)
- **Goals**: Implement recursive quadtree spatial partitioning starting from $512 \times 512$ down to $64 \times 64$.
- **Split Rule**:
  Split parent block into 4 children if and only if:
  $$\sum_{i=1}^4 C(\text{child}_i) + C_{\text{partition\_overhead}} < C(\text{parent})$$

### Phase V6: Reversible Integer Wavelet Transforms
- **Goals**: Implement 2D discrete wavelet transforms (CDF 5/3 lifting scheme) operating on predictor residuals.
- **Subband Structure**: 2D decomposition into LL, LH, HL, HH subbands up to 3 levels deep ($L1, L2, L3$).
- **Lifting Equations**:
  $$\text{Detail: } d[n] = x[2n+1] - \left\lfloor \frac{x[2n] + x[2n+2]}{2} \right\rfloor$$
  $$\text{Smooth: } s[n] = x[2n] + \left\lfloor \frac{d[n-1] + d[n] + 2}{4} \right\rfloor$$
- **Reversibility**: Verify 100% exact integer reconstruction.

### Phase V7: Symbol Mapping & Context Modeling
- **Goals**: Map signed integer residuals to unsigned symbols using ZigZag encoding and model probability distributions per subband and magnitude class.
- **Context Vector**:
  $$\text{Context} = (\text{subband\_id}, \text{magnitude\_class}, \text{neighbor\_activity})$$

### Phase V8: High-Throughput Entropy Coding
- **Goals**: Integrate range/rANS (interleaved Asymmetric Numeral Systems) coder for symbol bitstreams.
- **Baseline Comparison**: Benchmark rANS throughput and compression ratio directly against Zstd and FSE (Finite State Entropy).

### Phase V9: XTM Container Serialization (.xtm)
- **Goals**: Define binary file layout, header serialization, block index tables, tile offsets, and stream packing.
- **Header Layout**:
  - Magic Signature (`XTM\0`, 4 bytes)
  - Version & Flags (4 bytes)
  - Global Bounding Box & EPSG CRS (24 bytes)
  - Tile Grid Dimensions & Resolution (16 bytes)
  - Index Offset Pointer (8 bytes)

### Phase V10: Random Access & Region-of-Interest Decoding
- **Goals**: Implement spatial index (R-Tree / Quadtree) and superblock layout for fast bounded region queries (`decode_region(bounds)`).
- **Latency Requirement**: Sub-10ms decoding of arbitrary sub-regions without reading the full file.

### Phase V11: CUDA GPU Acceleration Backend
- **Goals**: Accelerate computationally intensive stages using CUDA kernels.
- **Accelerated Operations**:
  - Fixed-point quantization & scale conversion.
  - Parallel predictor evaluation across blocks.
  - Parallel 2D CDF 5/3 wavelet lifting in GPU shared memory.
  - Parallel reduction for histogram generation and entropy estimation.

### Phase V12: Advanced Features & Streaming Systems
- **Goals**: Implement multiresolution pyramids (LODs via LL subband extraction), cross-tile redundancy modeling, and error-bounded adaptive precision (geometrically constrained vertical quantization).

---

## 5. Repository & Project Structure

```text
libxtm/
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── libxtm_main_formatted.md
├── libxtm_dev.md
│
├── include/
│   └── xtm/
│       ├── xtm.hpp            # Main public header
│       ├── Encoder.hpp        # Core C++ Encoder class
│       ├── Decoder.hpp        # Core C++ Decoder class
│       ├── Terrain.hpp        # TerrainView & TerrainBuffer
│       ├── Options.hpp        # Encode/Decode configuration options
│       ├── Error.hpp          # Exception & status error codes
│       └── Types.hpp          # Fixed-point & enum definitions
│
├── src/
│   ├── Encoder.cpp
│   ├── Decoder.cpp
│   │
│   ├── analyzer/
│   │   ├── Analyzer.cpp       # Terrain statistical analysis engine
│   │   └── Statistics.cpp     # Histogram & entropy calculators
│   │
│   ├── terrain/
│   │   ├── Terrain.cpp
│   │   └── Quantization.cpp   # Fixed-point quantization & scaling
│   │
│   ├── predictor/
│   │   ├── Predictor.hpp      # Abstract base class
│   │   ├── Left.cpp
│   │   ├── Above.cpp
│   │   ├── Gradient.cpp
│   │   ├── JpegLs.cpp
│   │   └── Plane.cpp
│   │
│   ├── partition/
│   │   └── Quadtree.cpp       # Quadtree spatial block subdivision
│   │
│   ├── transform/
│   │   └── Wavelet.cpp        # 2D CDF 5/3 lifting wavelet transform
│   │
│   ├── entropy/
│   │   ├── ContextModel.cpp   # Context modeling
│   │   ├── Rans.cpp           # Interleaved rANS entropy coder
│   │   └── ZstdAdapter.cpp    # Baseline Zstd compressor
│   │
│   ├── container/
│   │   ├── Header.cpp         # Binary header serialization
│   │   ├── Reader.cpp         # XTM container reader
│   │   ├── Writer.cpp         # XTM container writer
│   │   └── SpatialIndex.cpp   # R-Tree / Quadtree index
│   │
│   └── io/
│       └── GDALReader.cpp     # GDAL raster ingestion
│
├── backends/
│   ├── cpu/
│   │   ├── PredictorCPU.cpp
│   │   └── WaveletCPU.cpp
│   │
│   └── cuda/
│       ├── Predictor.cu       # Parallel predictor search kernel
│       ├── Wavelet.cu         # 2D wavelet GPU shared memory kernel
│       └── Statistics.cu      # Fast parallel reduction statistics
│
├── apps/
│   └── xtm/
│       ├── main.cpp           # CLI entry point
│       ├── AnalyzeCmd.cpp     # 'xtm analyze'
│       ├── EncodeCmd.cpp      # 'xtm encode'
│       ├── DecodeCmd.cpp      # 'xtm decode'
│       ├── InfoCmd.cpp        # 'xtm info'
│       └── BenchmarkCmd.cpp   # 'xtm benchmark'
│
├── bindings/
│   └── python/
│       ├── module.cpp         # pybind11 / nanobind C++ bindings
│       └── CMakeLists.txt
│
├── tests/
│   ├── unit/                  # Modular component unit tests
│   ├── roundtrip/             # Strict lossless roundtrip tests
│   └── testdata/              # Test rasters & synthetic data
│
├── benchmarks/                # Google Benchmark suites
├── tools/                     # Code generator & validation scripts
└── python/
    ├── libxtm/                # Python package wrapper
    ├── examples/              # Usage examples
    └── notebooks/             # Research & visualization notebooks
```

---

## 6. Testing & Quality Assurance Plan

### 6.1 Strict Round-Trip Lossless Verification
For all lossless compression pipelines, the system must satisfy:
$$\text{Decode}(\text{Encode}(X)) = X$$
Testing will execute on:
- Flat synthetic surfaces ($z = c$)
- Constant planar ramps ($z = ax + by + c$)
- Negative elevation grids ($z < 0$)
- Discontinuous cliffs & NoData boundary masks
- Real Copernicus DEM tiles (e.g. `N27_00_E086_00`)

### 6.2 Error-Bounded Quantization Verification
For error-bounded modes with precision step $q$, reconstructed values $\hat{z}$ must satisfy:
$$|z - \hat{z}| \le \frac{q}{2}$$
In addition to elevation RMSE and MAE, geometric quality checks will measure slope error ($\Delta g$) and curvature error ($\Delta \kappa$).

### 6.3 Performance & Regression Benchmarks
Automated benchmarks will monitor:
- Compression ratio vs GDAL COG DEFLATE and Zstd.
- Bitrate in `bits/sample`.
- Encoding throughput (MB/s).
- Decoding throughput (MB/s).
- Random access latency for region queries.

---

## 7. Immediate Next Steps & Execution Milestones

1. **Initialize Workspace & Build Infrastructure**: Set up root `CMakeLists.txt`, standard compiler flags (`-O3`, `-std=c++20`), and testing frameworks.
2. **Implement Phase V0 (Analyzer)**: Write `TerrainView`, `GDALReader`, and `Analyzer` to run on `Copernicus_DSM_COG_10_N27_00_E086_00_DEM.tif`.
3. **Generate Initial Benchmark Report**: Measure raw elevation entropy, first differences ($\Delta X, \Delta Y$), Float32 bit-structure, and baseline predictor residual entropies.
