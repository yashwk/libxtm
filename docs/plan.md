## Goal Description
Transform the predictor selection architecture from a brute-force approach into a modern, multi-stage fast Rate-Distortion Optimization (RDO) pipeline. This will introduce block-level feature extraction to intelligently prune primary predictors, followed by a second-order RDO stage that applies advanced residual predictors (FIR, Median, Least Squares) to structured error. Finally, the `xtm analyze` command will be entirely refactored to surface these new rich block-level and residual-level statistics, making it a powerful diagnostic tool rather than a generic stat dumper.

## User Review Required
> [!WARNING]
> **Bitstream Format Change:** To support a pool of second-order predictors, we must allocate 3 bits in the bitstream header for the `ResidualPredictorId`, reducing the primary predictor ID space from 7 bits to 5 bits. This breaks backward compatibility with existing `.xtm` files.

> [!TIP]
> **Compute Overhead:** Running Least Squares and Adaptive FIR on residuals is mathematically expensive. I propose gating the most expensive residual predictors behind an `--extreme-compression` preset or conditionally disabling them if the block variance is too large.

## Open Questions
- Do you have a specific formulation in mind for the Adaptive FIR filter (e.g., a simple 3-tap spatial filter), or should I design an LMS (Least Mean Squares) adaptive filter?
- For the `Analyze` refactor, are there any specific UI formats (like terminal charts or specific CSV outputs) you'd like to see, or just a deeply enriched version of the current standard output?

## Proposed Changes

---
### Analyzer Core (Statistics & Selector)
Introduce the rich statistical structures and rebuild the `PredictorSelector` as a 6-stage pipeline.

#### [MODIFY] include/xtm/analyzer/Selector.hpp
- Add `BlockStats` struct (variance, horizontal/vertical correlation, laplacian energy).
- Add `ResidualStats` struct (% zeros, max zero run length, remaining variance).
- Add `ResidualPredictorId` enum (None, Avg, Median, AdaptiveFIR, LeastSquares).
- Update `SelectionResult` to hold the newly extracted stats and the chosen residual predictor.

#### [MODIFY] src/analyzer/Selector.cpp
- **Stage 1 (Feature Extraction):** Calculate `BlockStats` (Laplacian, Correlation).
- **Stage 2 (Fast Pruning):** Skip predictors mathematically unsuited for the block's `BlockStats`.
- **Stage 3 (Primary RDO):** Dry-run `ContextModeler` on the surviving candidates.
- **Stage 4 (Residual Extraction):** Compute `ResidualStats` on the winning primary predictor. If `% zeros > 95%`, halt and return.
- **Stage 5 (Second-Order RDO):** Execute the new Residual Predictor pool on the residuals.
- **Stage 6 (Final Selection):** Pick the absolute cheapest combination.

---
### Second-Order Predictor Pool
Introduce the new mathematical models for operating on residuals.

#### [NEW] include/xtm/predictor/ResidualPredictors.hpp
#### [NEW] src/predictor/ResidualPredictors.cpp
- Implement `AdaptiveFIRPredictor`
- Implement `MedianResidualPredictor`
- Implement `LeastSquaresResidualPredictor`
All will conform to a new or modified interface for strictly predicting upon 2D residual planes.

---
### Codec Bitstream
Update the encoder and decoder to serialize the new residual choices.

#### [MODIFY] src/coding/Encoder.cpp
- Pack the 3-bit `ResidualPredictorId` into the predictor byte (masking `0xE0`).
- Route the residuals through the chosen residual predictor before writing.

#### [MODIFY] src/coding/Decoder.cpp
- Unpack the 3-bit `ResidualPredictorId` from the predictor byte.
- Apply the inverse residual prediction before passing it to the primary predictor decoder.

---
### Analyze Command Refactor
Completely overhaul the CLI analysis tool to output the new insights.

#### [MODIFY] include/xtm/analyzer/Analyzer.hpp
- Add vectors/histograms for the new `BlockStats` and `ResidualStats` to the `AnalysisReport`.

#### [MODIFY] src/analyzer/Analyzer.cpp
- Populate the new stats during the quadtree traversal. Keep track of how many times a predictor was "Fast Pruned" vs "Evaluated but lost".

#### [MODIFY] apps/xtm/AnalyzeCmd.cpp
- Rip out the old entropy dump and replace it with a categorized narrative:
  - **Block Feature Profile:** (Average Laplacian, Dominant Directional Correlation)
  - **Pruning Efficiency:** (How much time/compute was saved by the Fast Prune heuristics)
  - **Residual Optimization:** (How often second-order FIR/Least Squares fired, and the exact bit savings they provided over the primary prediction)

## Verification Plan

### Automated Tests
1. Add new unit tests for the residual predictors:
`ctest -R ResidualPredictorTest`
2. Run the full codec roundtrip to ensure the new 3-bit header unpacking is lossless:
`ctest -R CodecRoundTripTest`

### Manual Verification
1. Run `xtm analyze grand_canyon.tif` and verify the output is radically more descriptive and explains *why* the codec is making its choices.
2. Run `xtm encode` and verify that the encoding time drops (thanks to Fast Pruning) while compression ratio increases (thanks to Residual Predictors).
