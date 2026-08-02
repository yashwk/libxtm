# libxtm Code Review: Suggestions & Improvements

Independent review of the libxtm codebase (commit `7283bd3`), based on reading the full C++ source, CLI, tests, and tooling. Severity legend: **HIGH** (bug/UB/data corruption), **MEDIUM** (robustness/perf/quality), **LOW** (polish).

---

## 1. Correctness & Robustness

### 1.1 HIGH — Unbounded predictor index read in decoder
`apps/xtm/DecodeCmd.cpp:146`

```cpp
uint32_t predictor_idx = br.read_bits(8);
const predictor::Predictor* predictor = predictors_list[predictor_idx];
```

The 8-bit index is read from the file with no bounds check against `predictors_list.size()` (11). A corrupt or malicious `.xtm` file yields out-of-bounds access → UB. The encoder (`EncodeCmd.cpp:256`) writes the index as 8 bits, so values ≥ 11 are never produced by a correct encoder, but decode must still validate.

**Fix:** clamp/validate before indexing:

```cpp
if (predictor_idx >= predictors_list.size()) {
    throw std::runtime_error("Corrupt XTM: invalid predictor index");
}
```

### 1.2 HIGH — No magic-byte / version validation on read
`src/container/IO.cpp:65` — `XtmReader` opens the file, reads the header, and immediately trusts it. There is no check that the stream starts with the `XTM\0` magic, that the format version is supported, or that index offsets are within the file. Any random file with a nonzero `index_offset` field will be "parsed".

**Fix:** in `Header::read`, verify magic bytes (and version range); in `XtmReader`, sanity-check `index_offset < file_size` and `num_entries * entry_size + index_offset <= file_size`. Check `stream_.bad()` after every read and throw.

### 1.3 HIGH — Static shared `FrequencyTable` (latent data race)
`src/coding/RangeCoder.cpp:132,148`

```cpp
static FrequencyTable uniform_bit(2); // 0 and 1 equally probable
```

Two function-local statics share mutable state. `encode_value` / `decode_value` are currently **dead code** (no call sites anywhere in the tree), so no race manifests today — but anyone using them from the multithreaded encode path (workers in `EncodeCmd.cpp:164`) will get a data race and nondeterministic output.

**Fix:** delete the dead functions entirely, or make the table an instance member / parameter.

### 1.4 MEDIUM — `-ffast-math` globally in Release
`CMakeLists.txt:54` — `-O3 -march=native -mtune=native -ffast-math`. The integer codec core is safe, but `-ffast-math` silently breaks IEEE guarantees (denormals, NaN, signed-zero, FMA contraction) in the float paths: `PlanePredictor` / `LeastSquaresPredictor` least-squares fits and `quantize` rounding. This is a correctness landmine for future float work and makes debug-vs-release results differ.

**Fix:** drop `-ffast-math`, or apply it only to explicitly vetted translation units (e.g., per-target `target_compile_options`), not globally.

### 1.5 MEDIUM — Benchmark parser reports fabricated zeros
`utils/benchmark_suite.py:44-47` parses `rf"Predictor {p_idx}: \d+ blocks ([\d\.]+%)"`, but `EncodeCmd.cpp` prints `- Gradient:\n    Usage: …%` (no "Predictor N" format). Every regex misses, so **all predictor % columns in `docs/benchmark_analysis.md` are 0.0%** — the current report's "Gradient % / Other Preds %" data is meaningless.

**Fix:** parse the actual `Usage:` lines, or change `EncodeCmd` to emit a machine-readable summary (e.g., JSON or the "Predictor N: X blocks (Y%)" format).

### 1.6 HIGH — Context model not persisted in the container (found via E2E testing)

The arithmetic coder's context model (`Simple` vs `Extended`, `--context`) is chosen at encode time but **never written to the `.xtm` header**. `DecodeCmd` defaults to `Simple` unless the user re-passes `--context`, so `xtm encode --context extended` followed by `xtm decode` silently produces **fully garbage output** (verified: 65527/65536 pixels wrong, values ~2^31). The encoder and decoder otherwise agree bit-for-bit; only the context sets diverge.

**Fix (implemented):** v2 of the format adds `uint16_t context_model` to `XtmHeader` (serialized before `index_offset`). `EncodeCmd` records the model it used; `DecodeCmd` derives the model from the header unless the user explicitly overrides with `--context`. `XtmReader` rejects invalid model IDs (0/1 only). Encoded/decoded output verified pixel-exact on both a 256×256 crop and a 1200×3600 tile (0 mismatches of 4,320,000 pixels) with both models.

---

## 2. Performance (hot paths)

The encode path (`EncodeCmd.cpp`) spends its time in: predictor evaluation inside `PredictorSelector`, wavelet transform, entropy coding, and container I/O (per the pipeline-profiling output). These are the highest-leverage targets.

### 2.1 HIGH — Heap allocation per predictor `encode()`
`Predictor.hpp:10-12` — every `encode()` allocates two `std::vector`s (residuals + parameters). `Selector.cpp` calls `encode()` for every candidate predictor on every quadtree node; `Analyzer.cpp` calls it ~20× per superblock. That is millions of small heap allocations per tile.

**Fix:** thread a reusable scratch buffer through the interface, or change `encode` to fill caller-owned `std::vector<int32_t>&` outputs. Requires touching all 11 predictors, but is mechanical and preserves the round-trip contract.

### 2.2 MEDIUM — `BlockView::get` recomputes `y * width + x`
`include/xtm/partition/Block.hpp:16` / `IntGrid::get` (`Quantization.hpp:14`) — every sample access does `data[y * width + x]` with no row caching. Inner predictor loops call this 3–7× per sample.

**Fix:** add row-pointer accessors (per the optimization contract in `PROJECT.md`):

```cpp
const int32_t* row_data(uint32_t local_y) const; // grid->data + (y_offset + local_y) * grid->width + x_offset
int32_t* row_data(uint32_t local_y);
```

and refactor predictor loops to walk `row[0..width-1]`. Also note `IntGrid::get` does no bounds checking — that is by design, but a debug-only assert would catch off-by-one bugs.

### 2.3 MEDIUM — Wavelet allocates a temp vector per 1-D call
`src/transform/Wavelet.cpp:34,52` — `forward_1d`/`inverse_1d` allocate `std::vector<int32_t> temp(length)` on every invocation; `forward_2d` calls them W+H times per level, per block.

**Fix:** in-place pack/unpack without a temp (two-pointer permutation or butterfly swaps), or pass a reusable scratch buffer owned by the caller. Given `-O3`/LTO the allocation may be elided for small blocks (SSA escape analysis), but a scratch member removes the uncertainty.

### 2.4 MEDIUM — Selector re-encodes the winning predictor + copies residuals
`Selector.cpp:58,83` — the wavelet evaluation does `wv_residuals = best_result.best_encoded.residuals` (full copy), and the non-wavelet decision adds `total_bits += 1.0` but keeps the previously computed encode. Also the quick-classification min/max loop (`Selector.cpp:22-28`) is a separate full pass over the block.

**Fix:** move the winner's residuals (already `std::move`d) — `best_encoded.residuals` is moved into `wv_residuals`, so the copy is actually a move at `:92`; verify with a profiler whether a copy remains. Merge the min/max scan into the predictor loop where possible, or accept the classification pass and make it the only pre-pass.

### 2.5 MEDIUM — `FrequencyTable` O(N) per operation
`RangeCoder.cpp:14-31, 88-95` — `increment()` walks `cum_freq_` from `symbol+1` to end; `decode()` scans linearly for the symbol range. With 33 symbols and millions of symbols per block this is measurable (though small-N).

**Fix:** binary search `cum_freq_` on decode (it's sorted), and lazy-normalize (`increment` without per-symbol cum-freq rebuild, rebuilding only at halving time). Also `ArithmeticEncoder` writes one bit at a time via `BitWriter::write_bit` — buffer into bytes.

### 2.6 MEDIUM — Quadtree re-selects parents and children redundantly
`src/partition/Quadtree.cpp:25,40-51` — each node runs a full `selector.select()`, and the 4 children re-run it on their (possibly re-visited) blocks. Selection dominates cost.

**Fix:** memoize selection results keyed by `(x, y, w, h)` per superblock (cheap hash of 4 ints), or evaluate children only if the parent's entropy suggests splitting is plausible.

### 2.7 LOW — `calculate_entropy` uses `std::unordered_map` per call
`Statistics.cpp` — called thousands of times during analysis/selection. A `std::sort` + linear sweep, or a histogram buffer when the range is small, is faster and cache-friendlier.

---

## 3. Architecture & Code Quality

### 3.1 MEDIUM — Subband extraction duplicated
`ContextModeler.cpp:12-39` and `DecodeCmd.cpp:197-223` implement the same subband coordinate classification. They must stay in lockstep or the codec silently corrupts data.

**Fix:** expose `coding::extract_subbands(...)` from the coding module and use it in both places.

### 3.2 MEDIUM — Hard-coded predictor lists + index-order coupling
The 11-predictor list is rebuilt in three places (`EncodeCmd.cpp:134`, `DecodeCmd.cpp:92`, `Analyzer.cpp:194`) and the 8-bit predictor ID depends on list order. Adding/removing a predictor breaks file compatibility silently.

**Fix:** introduce a stable `enum class PredictorId` with an explicit ID → instance registry/factory; write the enum ID, not a list index; decode by ID lookup.

### 3.3 MEDIUM — `Analyzer.cpp` is a 697-line monolith
`analyze_terrain` contains a huge `worker` lambda with ~25 local accumulator variables, all merged under one mutex at the end. Maintainability is the real cost here (it's already hard to verify which accumulators are thread-local vs global).

**Fix:** extract a `SuperblockStats` struct with `+=`/merge methods, and a separate `accumulate_leaf()` helper.

### 3.4 MEDIUM — Peak RAM for `analyze` on full tiles
`Analyzer.cpp` accumulates full-tile `ll_all/lh_all/hl_all/hh_all` (4 × 12.9M int32 ≈ 206 MB for a 3600×3600 tile), `global_grad_res` (~52 MB), plus 22 residual histograms (unordered_maps with millions of keys). Peak usage is well over 0.5–1 GB per tile.

**Fix:** stream per-superblock subband statistics into fixed-size aggregates (sum, sum-of-squares, counts, percentiles via histograms) instead of collecting every coefficient.

### 3.5 MEDIUM — Container lacks integrity protection
`src/container/Header.hpp` / `IO.cpp` — no per-block checksum, no format version enforcement on read (see 1.2). For a storage format, silent bit rot should be detectable.

**Fix:** add CRC32 (or xxHash) per block entry and a format version field with read-time validation.

### 3.6 LOW — `xtm` CLI surface is narrower than the spec
`apps/xtm/main.cpp` implements `analyze`, `encode`, `decode` only. The spec (`docs/libxtm_spec.md:1060-1063`) and dev plan call for `info`, `verify`, and `benchmark`. `verify` in particular is the natural E2E acceptance tool.

---

## 4. Testing Gaps

Current 26 tests cover predictor round-trips, transform, coding, container, statistics, entropy. Missing:

| Gap | Why it matters |
|---|---|
| **E2E encode→decode→compare** round trip on a real tile | The only check that quadtree + selector + wavelet flags + container + RLE nodata + predictor IDs are mutually consistent. A single misordered bit elsewhere stays invisible. |
| Golden `.xtm` fixtures | Byte-stability / compatibility guard: old decoders must read new files. |
| Corrupt-file robustness tests | 1.1 / 1.2 protections need regression tests (truncated file, bad magic, bad predictor index). |
| `quantize`/`dequantize` tests | Rounding (`std::round(val * inv_scale)`) and the nodata inpainting loop (`Quantization.cpp:30-66`) are untested. |
| ROI decode test | `--region` output must equal the corresponding crop of a full decode. |
| `AnalyzeCmd` / `EncodeCmd` / `DecodeCmd` CLI tests | Current tests only touch the library layer. |
| Multithreaded determinism test | Encode with 1 vs N threads must produce identical bytes (validates 1.3-class hazards and merge logic). |

---

## 5. Benchmark Tooling

- **Fix the parser mismatch** (1.5) — current report columns for predictor usage are all zeros.
- **Add decode throughput** — the suite measures encode time but not decode time, which the spec calls the priority metric (`libxtm_spec.md:1596`).
- **Add peak-RAM measurement** — per-tile `/usr/bin/time -v` or a wrapper, since 3.4 makes this a real concern.
- **Make tile list explicit** — `data/hills/rename_log.txt` suggests data churn; pin the benchmark to a manifest so reports are comparable across runs.
- **Record environment metadata** (CPU model, core count, compiler, flags, GDAL version) in `benchmark_results.csv`/markdown header so regressions are attributable.

---

## 6. Feature Roadmap (small wins)

1. **`xtm verify <input.xtm> <input.tif>`** — decode and diff against source; closes the E2E test gap (4) and the acceptance criteria in `ORIGINAL_REQUEST.md` at the CLI level.
2. **`xtm info <input.xtm>`** — dump header/index contents (cheap, uses existing `XtmReader`).
3. **Per-block checksums** (3.5) with a format-version bump and a migration path.
4. **rANS coder** — the spec names rANS as the preferred entropy coder with Zstd as baseline (`libxtm_spec.md:23`); the current adaptive arithmetic coder works but is a natural throughput bottleneck to revisit once 2.5 lands.
5. **Decode-path predictor-index validation** (1.1) before any other feature work — it is the one change that turns "may crash on bad input" into "reports an error".

---

## Suggested Priority Order

1. **Fix the three HIGH correctness items** (1.1, 1.2, 1.3) — small, surgical, high value.
2. **Add the E2E round-trip + corrupt-file tests** (4) so everything after is verifiable.
3. **Performance work on the encode hot path** (2.1 → 2.2 → 2.3 → 2.5) — the allocation/row-pointer/wavelet-scratch changes map directly to the predictor-optimization milestone.
4. **Refactor for maintainability** (3.1, 3.2, 3.3) — do before the predictor bank grows.
5. **Tooling fixes** (1.5, 5) — restore truthful benchmark reporting.
6. **Feature work** (6) last, with checksums + `verify` first.

## Implementation Status (2026-08-02)

Implemented and verified (Release build, `-Werror` clean, 42 tests passing):

| Item | Status |
|---|---|
| 1.1 predictor-index guard in `DecodeCmd` | done |
| 1.2 magic/version/region validation in `XtmReader` + `Header` | done |
| 1.3 `static FrequencyTable` → per-call instances | done (test-only consumers kept) |
| 1.4 `-ffast-math` removed from Release | done |
| 1.5 machine-readable `Predictor N: …` stats + parser | done |
| 1.6 context model persisted in header (v2 format) | done |
| 2.2 `BlockView::row_data` in all 11 predictors + selector | done |
| 2.3 wavelet scratch buffer | done |
| 2.5 binary-search `ArithmeticDecoder::decode` | done |
| 3.1 shared `extract_subbands` | done |
| 3.2 centralized `PredictorBank` | done |
| 4 corrupt-file + quantize + E2E round-trip tests | done (41→42 tests) |

**Behavioral-equivalence check:** encoded `.xtm` output of the refactored tree is **byte-identical** to the baseline commit on the same input (all predictor/wavelet/coder refactors are behavior-preserving). Full-tile encode→decode verified pixel-exact (0/4,320,000 mismatches).

**Pre-existing quirk (not introduced here):** `AnalyzerTest.Checkerboard` (`hh_energy_pct > 10`) fails in Debug builds but passes in Release, on both the baseline and current trees — points at an uninitialized read or `-ffast-math`-dependent path in the analyzer's wavelet energy code; worth investigating separately.

Still open (untouched, lower priority): 2.1, 2.4, 2.6, 2.7, 3.3, 3.4, 3.5, 3.6, 5 (decode throughput / RAM / manifest / env metadata), 6 (verify/info commands, checksums, rANS).

