## Goal Description
The `xtm analyze` command output is currently cluttered with redundant metrics, missing critical multi-stage RDO metrics (like quadtree overhead and estimated compression ratio), and suffers from an illogical hierarchy. This plan will refactor the UI into a clean, logical sequence that accurately mirrors the new RDO pipeline.

## Proposed Changes

### 1. Logical Hierarchy of Output Sections
To ensure the information is presented in a revealing and sequential manner (not a random hierarchy), the console output will follow this logical flow:
1. **Dataset Overview** (Includes formatting pixels to millions, e.g., `12.96M pixels`).
2. **Compression Summary** (Top-level look at `Estimated Compression Ratio` and overall `bits/pixel`).
3. **Block Feature Profile** (Avg Variance, Laplacian, etc.).
4. **Fast RDO Pruning Efficiency** (Block and predictor evaluation stats).
5. **Primary Predictor Performance** (Base vs Final entropies in a clean ASCII-bordered table).
6. **Residual Optimization (Second-Order RDO)** (Pass trigger rates and bit savings).
7. **Primary Residual Distribution** (Merged histogram replacing the redundant Confidence/Difficulty/Histogram outputs).
8. **Quadtree Analysis** (Includes added `quadtree_overhead_bpp` and `quadtree_overhead_pct`).
9. **Information Reduction Pipeline** (Updated to show `Raw Elevation` -> `Quadtree + Primary RDO` -> `Second-Order RDO`).
10. **Entropy Coding & Context Modeling**.
11. **Compute Analysis**.

### 2. `include/xtm/analyzer/Analyzer.hpp`
- **[MODIFY]** `Analyzer.hpp`
  - Add `double quadtree_overhead_bpp;` and `double quadtree_overhead_pct;`.
  - Add `double estimated_compression_ratio;`.
  - Add `double primary_rdo_entropy;` and `double second_order_rdo_entropy;`.
  - Remove redundant structs (`PredictorConfidence`, `PredictionDifficulty`, `CorrelationStats`).

### 3. `src/analyzer/Analyzer.cpp`
- **[MODIFY]** `Analyzer.cpp`
  - Populate the newly added overhead and ratio fields from the existing stats.
  - Compute `primary_rdo_entropy` based on `base_bits` and `second_order_rdo_entropy` based on `total_bits`.
  - Remove aggregation logic for the deleted redundant structs.

### 4. `apps/xtm/AnalyzeCmd.cpp`
- **[MODIFY]** `AnalyzeCmd.cpp`
  - Rewrite the print routines to follow the logical hierarchy defined above.
  - Merge the duplicate residual views into a single dynamically scaled block-character histogram.

## Verification Plan
### Automated Tests
Based on the `CMakePresets.json`, we will verify using both `release` and `dev` configurations utilizing all processor cores via `nproc`:

```bash
# Verify Release Build
cmake --preset release
cmake --build build/release -j$(nproc)
ctest --preset release

# Verify Debug (Dev) Build
cmake --preset dev
cmake --build build/dev -j$(nproc)
ctest --preset dev
```

### Manual Verification
Run `./build/release/bin/xtm analyze /home/yashwanth/projects/libxtm/data/canyons/grand_canyon.tif` to visually verify the refined, logical hierarchy.
