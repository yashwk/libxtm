# libxtm Development & Implementation Master Plan

## 1. Executive Summary & Vision

**libxtm** (Extreme Terrain Matrix Codec) is a high-performance C++20 library and command-line tool (`xtm`) designed for domain-specific terrain elevation compression, random-access storage, spatial indexing, and streaming. 

The primary engineering objectives are:
1. **Domain-Specific Codec**: A compression engine exploiting 2D spatial continuity, elevation correlation, and geometric properties of terrain (achieving maximum compression efficiency while preserving vertical accuracy and surface geometry).
2. **High-Performance Container (.xtm / .xtem)**: A single-file or tile-directory container enabling sub-millisecond random-access region decoding (`decode_region`), spatial Quadtree indexing, and implicit global grid topology.
3. **Multi-Platform Ecosystem**: C++20 core engine with zero-copy C++ API and a CLI application (`xtm`). Python bindings are planned for research workflows.
4. **Measurement-Driven Development**: An incremental roadmap (V0 to V12) where every feature justifies its bit savings and runtime cost against baseline compressors (e.g., GDAL COG DEFLATE, Zstd).

---

## 2. Current Project Status

The core C++ engine is highly mature and stable. We have successfully implemented through **Phase V10** (random access and ROI decoding) and integrated advanced V12 architectural features like the **Split-Precision (Meter/Precision) Architecture**.

**Core accomplishments:**
- **Zero-allocation streaming pipeline**: Block symbols are streamed directly to the 64-bit Arithmetic Coder without intermediate vectors.
- **Split-Precision Architecture**: Sub-meter grids dynamically split the predictor workload into meter-scale Base models and localized Precision models, bypassing expensive wavelet subbands.
- **Robust v4 Header**: Variable-sized headers securely house full 6-parameter GeoTransforms and length-prefixed WKT projection strings, making libxtm a fully compliant geospatial container.
- **100% Deterministic Reproducibility**: Parallel threaded encodes guarantee identical byte output regardless of scheduling.
- **Verification Suite**: 50+ unit and integration tests (including ASan/UBSan memory checks) assert perfect mathematical lossless round-tripping for floats, ints, scaling, and ROIs.

---

## 3. System Architecture & Modular Design

The system enforces clear boundaries between I/O, core data representations, predictors/transforms, entropy coders, and container formats.

```text
                                 Applications
                                      │
                    ┌─────────────────┴─────────────────┐
                    ▼                                   ▼
                 xtm CLI                             C++ API
                    │                                   │
                    └─────────────────┬─────────────────┘
                                      ▼
                                   libxtm
                                      │
        ┌─────────────────────────────┼─────────────────────────────┐
        ▼                             ▼                             ▼
  Codec Engine                  Terrain Core                 Container & Index
        │                             │                             │
  ├── Ingest & Validation       ├── Canonical TerrainView     ├── Fixed Header (v4)
  ├── Quantization / Fixed      ├── Bounding Box / CRS        ├── Spatial Index (CRC32)
  ├── Predictors (7 types)      ├── Bit-width representation  ├── Tile Directory
  ├── Partitioning (Quadtree)   └── Geometry / Normals        └── Superblock Streams
  ├── Reversible Wavelets
  ├── Context Modeling (M/P)
  └── Entropy Coding (Arith)
        │
  ┌─────┴─────┐
  ▼           ▼
 CPU        CUDA (Planned)
```

---

## 4. Data Flow & Compression Lifecycle

Each elevation sample passes through a multi-stage deterministic transformation pipeline:

```text
[Stage 1: Raw DEM Ingest]        Float64 elevation grid via GDAL
           │
           ▼
[Stage 2: Quantization / Fixed]   Convert Float64 to Fixed-Point Integer:
                                 q(z) = round(z / scale)
           │
           ▼
[Stage 3: Spatial Partitioning]  Hierarchical Quadtree partitioning into blocks
                                 (512×512 down to 64×64)
           │
           ▼
[Stage 4: Adaptive Prediction]   Evaluate 7 Predictors (Left, Above, Gradient, JPEG-LS, 
                                 Plane, Least Squares, Gap) per block.
                                 Split-Precision mode for sub-meter data.
           │
           ▼
[Stage 5: Wavelet Decorrelation] CDF 5/3 Integer Lifting Wavelet (Optional per-block)
           │
           ▼
[Stage 6: Context Modeling]      Map integer residuals to ContextStreams (Meter/Precision)
                                 and estimate magnitude classes.
           │
           ▼
[Stage 7: Entropy Coding]        Zero-allocation streaming into 64-bit Arithmetic Encoder
           │
           ▼
[Stage 8: Container Packaging]   Serialize bitstreams into .xtm superblocks with spatial index
```

---

## 5. Phase-by-Phase Development Strategy

### ✅ Phase V0: Foundation, Ingest & Terrain Analyzer Tooling
- Built the core build system (CMake, C++20).
- Created `TerrainView`, `TerrainBuffer`, GDAL adapter for reading COG GeoTIFF rasters.
- Delivered `xtm analyze` CLI command producing a complete statistical diagnostic report.

### ✅ Phase V1: Basic Deterministic Predictors
- Implemented first-order spatial predictors operating on fixed-point terrain grids (Left, Above, Average, Gradient).
- Verified zero residual loss in round-tripping.

### ✅ Phase V2: Advanced & Surface-Fitting Predictors
- Implemented JPEG-LS, Plane (Least Squares 3-param), and Least Squares (6-param quadratic) predictors.

### ✅ Phase V3: Fixed-Block Spatial Partitioning
- Divided raster tiles into fixed-size grid blocks to evaluate local predictor performance vs global predictor performance.

### ✅ Phase V4: Adaptive Predictor Selection Engine
- Enabled each block to choose its optimal predictor dynamically based on rate-cost estimation.

### ✅ Phase V5: Adaptive Hierarchical Partitioning (Quadtree)
- Implemented recursive quadtree spatial partitioning starting from 512×512 down to 64×64 based on split-cost heuristics.

### ✅ Phase V6: Reversible Integer Wavelet Transforms
- Implemented 2D discrete wavelet transforms (CDF 5/3 lifting scheme) operating on predictor residuals. Verified 100% exact integer reconstruction.

### ✅ Phase V7 & V8: High-Throughput Entropy Coding (Arithmetic + Split-Precision)
- Developed an optimized 64-bit Arithmetic Coder and a zero-allocation streaming Context Modeler.
- Discarded legacy Wavelet subbands in favor of a simpler ContextStream (Meter vs Precision) to dramatically cut down RAM usage and processing time.

### ✅ Phase V9: XTM Container Serialization (.xtm)
- Finalized v4 binary file layout.
- Variable Header (Magic, Version, GeoTransform, WKT String, Flags, Scale).
- Per-block CRC32 index for data integrity validation.

### ✅ Phase V10: Random Access & Region-of-Interest Decoding
- Implemented spatial index and superblock layout for fast bounded region queries (`xtm decode --region`). 
- Verified that ROI decoding output matches a cropped full-decode mathematically.

### ⏳ Phase V11: CUDA GPU Acceleration Backend (Pending)
- **Goals**: Accelerate computationally intensive stages using CUDA kernels.
- **Accelerated Operations**:
  - Parallel predictor evaluation across blocks.
  - Parallel 2D CDF 5/3 wavelet lifting in GPU shared memory.

### ⏳ Phase V12: Advanced Features & Python Bindings (Pending)
- **Goals**: Implement multiresolution pyramids (LODs via LL subband extraction).
- **Ecosystem**: Python bindings via `nanobind` or `pybind11` for direct NumPy grid integration and data-science workflows.

---

## 6. Testing & Quality Assurance Plan

### 6.1 Strict Round-Trip Lossless Verification
For all lossless compression pipelines, the system must satisfy:
$$\text{Decode}(\text{Encode}(X)) = X$$
Our `ctest` suite guarantees 100% accuracy on flat synthetic surfaces, planar ramps, random noise, and real Copernicus DEM grids.

### 6.2 CI-Ready Memory Safety
Both `dev` and `release` CMake presets are established. `dev` inherently runs with `ASan` (AddressSanitizer) and `UBSan` (UndefinedBehaviorSanitizer) to preemptively catch Out-Of-Bounds exceptions, undefined mathematical casts, and memory leaks.

### 6.3 Performance & Regression Benchmarks
- Encoding throughput (MB/s).
- Decoding throughput (MB/s).
- Compression ratio vs GDAL COG DEFLATE and Zstd.

---

## 7. Immediate Next Steps & Execution Milestones
1. **Performance Profiling**: The `PredictorSelector` still evaluates all 7 predictors, triggering heavy copies when copying winning residuals. A major refactor is needed to evaluate entropy in-place and avoid redundant encoding loops, aiming to drop tile encode times from ~10s to ~2s.
2. **CUDA Pre-work**: Begin abstracting the block traversal engine to support GPU device offloading.
