# libxtm Code Review — Suggestions & Improvements

Independent review of the libxtm codebase (commit `c2a27de`, "reduce memory usage"), based on reading the full C++ source, CLI, tests, and tooling. Severity legend: **HIGH** (bug/UB/data corruption), **MEDIUM** (robustness/perf/quality), **LOW** (polish).

A prior review (commit `7283bd3`) was already implemented and its fixes are in the tree; §0 summarizes the current state before the new findings.

---

## 0. Status of the Previous Review

Implemented & verified (Release, `-Werror` clean, 42/42 ctest passing):

| Item | Status |
|---|---|
| Predictor-index bounds guard in `DecodeCmd` | done |
| Magic/version/index-region validation in `XtmReader`/`Header` | done |
| `static FrequencyTable` race → per-call instances | done |
| `-ffast-math` removed from Release | done |
| Machine-readable `Predictor N: …` stats + parser | done |
| Context model persisted in header (format v2) + decode derivation | done |
| `BlockView::row_data` in all predictors + selector | done |
| Wavelet scratch buffer reuse | done |
| Binary-search `ArithmeticDecoder::decode` | done |
| Shared `extract_subbands` (encode↔decode lockstep) | done |
| Centralized `PredictorBank` | done |
| Corrupt-file, quantize, E2E round-trip tests (41→42) | done |
| `xtm info` / `xtm verify` commands | added |
| Per-block CRC32 (format v3) | added |

Still open from the previous review: §2.1 (allocations per `encode`), §2.4 (selector re-encode/copies), §2.6 (quadtree re-selection), §2.7 (entropy via sort+copy), §3.3 (Analyzer monolith), §3.4 (peak RAM), §3.6 (CLI surface, partially done), benchmark tooling refinement.

---

## 2. New Findings — Correctness & Robustness

### 2.1 HIGH — CRS/EPSG is never read, stored, or written
`src/io/GDALReader.cpp` copies the geotransform and NoData but **never calls `GetProjectionRef()`/`GetProjection()`**. `EncodeCmd.cpp` never assigns `header.epsg_crs`, so every `.xtm` file has `epsg_crs = 0`, and `GDALWriter.cpp` skips `SetProjection` when `epsg_crs == 0`. **Every re-exported TIFF is missing its CRS** — fatal for the geospatial use case, and invisible to the current tests (which never check projections).

**Fix:** read the SRS from the source dataset in `read_gdal` (store an `int32_t epsg_crs` on `TerrainBuffer`), serialize it (already a header field), and set it in `EncodeCmd`. Add a round-trip test asserting the EPSG code survives encode→decode.

### 2.2 HIGH — Decoded `.xtm` is not a floating-point-lossless codec (spec 6.1 mismatch)
The "lossless" mode is only lossless w.r.t. the **quantized integer grid**. `--scale 1.0`=meter precision rounds away sub-meter data irreversibly. The spec explicitly promises "exact lossless Float32 mode." If that is a requirement, the container needs a flag for raw-int32/float preservation (or wrapper-level bitstreams). At minimum, document that XTM is error-bounded, not exact.

### 2.3 HIGH — `PlanePredictor::decode` and `LeastSquaresPredictor` read unvalidated parameter counts
`Plane.cpp:75-77` indexes `encoded.parameters[0..2]` and `LeastSquares.cpp:104` checks `>= 3` but `Plane` does not. A corrupt/truncated/malicious block (parameter count is 8-bit from the file) → OOB read → UB. The decoder's `predictor_idx` guard (§1) covers the predictor, not its params.

**Fix:** validate `parameters.size()` against the expected count at the very start of every `decode()` (not just LSM), or validate in `DecodeCmd` after reading `num_params` per predictor.

### 2.4 HIGH (latent) — `max_levels` derivation is duplicated in 3 places
`Selector.cpp:78`, `Analyzer.cpp:336`, `DecodeCmd.cpp:195` each recompute `max_levels = min(w,h) ≥ 2^levels` logic. The encoder writes only the 1-bit wavelet flag, so a drift in this formula silently corrupts every wavelet block. Fix: a single shared `coding::max_wavelet_levels(width, height)`.

### 2.5 MEDIUM — `BitReader` silently returns 0 past end-of-stream
`BitStream.hpp:46-58` returns `0` indefinitely after the buffer is exhausted (documented as "happens during Arithmetic decoder flushing"). Combined with the container's CRC32, corruption is *detected* at the container layer *after* the arithmetic decoder has already produced garbage — so the error is confusing ("CRC mismatch") and the decoder can also spin consuming zeros for a truncated-but-checksummed stream. Consider a `fail()`/`underflow()` flag checked by `ArithmeticDecoder::decode` (no bit-injection beyond flush).

### 2.6 MEDIUM — Encode output is not byte-deterministic across thread counts
Workers append block bitstreams to `XtmWriter` in completion order, so `encode --threads 1` vs `--threads 16` produce different byte layouts (same decoded pixels). For reproducible builds/tests/CI, sort-and-emit block payloads by (superblock, block) before `finalize()`, or pre-compute the layout and let workers write into fixed slots.

### 2.7 MEDIUM — Div-by-zero on empty grids
`EncodeCmd.cpp:359` computes `(double)wavelet_blocks / total_blocks` with no `total_blocks == 0` guard. Also `run_decode`/`run_encode` don't clamp `--region` / `--scale` (negative `scale` → `1/scale` negative → broken quantization; a negative `rx/ry` casts to huge `uint32` in the ROI copy loop).

### 2.8 MEDIUM — Analyze `global_predictors` are not really global
The "Global" predictor entropies in `Analyzer.cpp` are computed per 512×512 superblock and merged, so they are superblock-conditional (each block is entropy-coded assuming its own boundary context). Reports label this "Global" and compare it against "Local(64)" — apples vs. quasi-apples.

---

## 3. New Findings — Architecture & Maintainability

### 3.1 MEDIUM — The pipeline is triplicated
The same stage sequence (quantize → quadtree → selector → wavelet → symbols → coder → container) is hand-written in `EncodeCmd.cpp`, `DecodeCmd.cpp`, and re-implemented again with instrumentation inside `Analyzer.cpp` (plus a fourth mirror in `tests/unit/test_codec_roundtrip.cpp`). Every new stage or tuning knob must be edited in all three, inviting the §2.4-class drift.

**Fix:** extract library-level `XtmEncoder::encode(grid)`, `XtmDecoder::decode(roi)` and `xtm::encode_analyze(grid)::analyze` that take an `Options {scale, context_model, pipeline_order, disable_quadtree}`. CLI commands become thin argument→options wrappers (the spec's intent — "compression logic does not belong in main.cpp").

### 3.2 MEDIUM — Analyzer is a 736-line monolith
`analyze_terrain()` contains one giant worker lambda with ~25 thread-local accumulators merged under a single mutex. This is already the source of subtle aggregation bugs (fixed in V10.5). Extract a `SuperblockStats` aggregate type with `.merge(SuperblockStats&)` and a `analyze_leaf()` helper, so the merge block disappears.

### 3.3 MEDIUM — String-name-based predictor classification
`Selector.cpp` prunes the predictor candidates by comparing `name()` strings (`"Average"`, `"Left"`, …). A rename silently changes behavior and there is no compile-time check. Use the positional bank ordering or add `Predictor::kind()` (a stable enum); see §3.4.

### 3.4 HIGH (format) — Predictor IDs are positional, not stable
`PredictorBank::ordered()` defines the ID→predictor mapping purely by vector order. Inserting/removing a predictor shifts every subsequent ID and breaks decode of existing files with the newer reader. Make `PredictorId` an explicit enum (0 = Left, 1 = Above, …), write the enum value, and resolve via a registry keyed by enum — with the current layout frozen so old files still decode.

### 3.5 MEDIUM — Global quadtree cost model double-counts the leaf flag
Both branches of `partition_recursive` add the 1-bit split flag — the "penalty" that cancels in the split comparison (see `pipeline.md` §6) — so the aggressor in the V5 benchmark is real but tiny (0.01 bpp). Not a bug; note it as intentional bookkeeping.

### 3.6 MEDIUM — `PredictorSelector` still evaluates ~11 predictors × re-encodes winner
Unchanged from prior (§2.1, §2.6): each quadtree node allocates 2 vectors per `encode()`, the classifier's min/max scan plus 11 full encodes, and the wavelet trial copies the winner's residuals. Profiled aggregate these are the dominant encode cost (benchmark: ~12 s/tile). Priority work once the §3.1 refactor lands.

---

## 4. LOW — Code Quality & Dead Code

| Item | Location |
|---|---|
| `early_exit_threshold_` parameter stored but never read | `Selector.hpp:24` / `Selector.cpp:10` |
| `encode_value`/`decode_value` dead (no call sites) | `RangeCoder.hpp:53-54`, `RangeCoder.cpp:131-163` |
| `SampleType` enum always `Float32`; `BoundingBox` never used | `Types.hpp:6-17` |
| `QuadtreeStats::max_depth/avg_depth` declared, never populated | `Analyzer.hpp:92-93` |
| `TransformEvaluationStats` predicted_gain hardcoded ±0.5 | `Analyzer.cpp:720-726` |
| `is_split` serialized never; `encoded.total_bits` should not be recomputed at container layer | `Quadtree.hpp:21` |
| `FLAG_USE_WAVELET` set nowhere (per-block flag used instead) — Header still declares it | `Header.hpp:13` |
| `--context extended/ext` on decode can still silently produce garbage if overridden from the header | `DecodeCmd.cpp:69-74` |
| `test.xtm` (5.3 MB) untracked artifact at repo root; `build/` contains `temp.xtm`, `temp_decode.tif` | repo root |
| `verify` writes to `/tmp/xtm_verify_temp.tif` (race if concurrent) | `VerifyCmd.cpp:55` |
| `Analyzer` uses ~22 `std::unordered_map`s as residual histograms ~1 GB peak for a 3600×3600 tile | `Analyzer.cpp:154-155` |

---

## 5. Testing Gaps

| Gap | Impact |
|---|---|
| CRS/EPSG round-trip test | Would catch §2.1 immediately (0 tests today) |
| CLI-level E2E (`build/xtm encode → decode → verify`) inside `ctest` | The only one, `VerifyCmd`, is manual/CLI-only |
| **Deterministic output test** (encode 1 vs N threads → same bytes) | catches §2.6 |
| Corrupt-`Plane`-params / truncated-block tests | catches §2.3/§2.5 |
| ROI decode == cropped full-decode test | ensures `--region` is exactly a sub-decoding |
| Large-tile memory/peak test (`-DENABLE_ASAN` + RSS bound) | guards §2.8 |
| MSan/UBSan preset | catches the Plane OOB on debug |
| `verify` against a `--scale 1.0` source | documents non-lossless expectations |

---

## 6. Benchmark Tooling

- Parser (§1.1 previous §3.1) works; the checked-in `docs/benchmark_analysis.md` predates the “decode time” column and has all-0 Gradient/Other columns in the per-file table rows — regenerate with the current suite (which already captures `time_decode_xtm`).
- Add peak-RAM via `/usr/bin/time -v` wrapper (guards §2.8).
- Pin tile list via a manifest (data is gitignored, so the set churns); record CPU/compiler/GDAL metadata in the header.
- The suite uses `gdal_calc`/`gdal_translate` in a hard-coded `data/` layout — parametrize tile path + binary path.

---

## 7. Recommended Priority Order

1. **§2.1 CRS persistence** — one line in both reader/encoder + one test; fixes first genuine data loss.
2. **§2.3 Plane params guard + §2.5 BitReader EOF flag** — the two remaining corrupt-file UB paths.
3. **§3.1 refactor Encoder/Decoder + analyzer on top** — unlocks 3.2, 3.6 and kills §2.4 duplication.
4. **§3.4 stable PredictorId** — before any further predictor-work, prevents a silent format break.
5. **§2.6 deterministic encode order + thread-count determinism test** — cheap, surprising savings in CI dev cycles.
6. **§2.8→ histogram/entropy-per-`select()`** via streaming histograms — the encode-throughput win.
7. Feature work (multiresolution, rANS, python) — last, with the above in place.

---

## 8. Final Note

The codebase is in good shape: all 42 tests pass on Release, and the previous HIGH items are closed. The remaining work is dominated (a) correctness gaps from the geospatial boundary (CRS, exactness, corrupt-file param validation), (b) one big refactor (split pipeline into `Encoder`/`Decoder` classes; extract the analyzer on top), and (c) performance work in the selection loop that accounts for the observed 7–13 s encode times. None of the recommendations change the (excellent) locked-step symmetry between encode and decode; each should be landed with a round-trip test.