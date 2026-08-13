#!/usr/bin/env python3
"""Smoke test for the nanobind `xtm` module (plain asserts, no pytest).

Run by CTest with PYTHONPATH pointing at the build directory containing the
compiled module, or manually:

    PYTHONPATH=build/release/lib python3 tests/python/test_xtm_bindings.py
"""
import os
import shutil
import sys
import tempfile

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TIF = os.path.join(REPO, "data", "canyons", "grand_canyon_512.tif")

KNOWN_PREDICTORS = {
    "Gradient",
    "Left",
    "JPEG-LS",
    "Polynomial",
    "GAP (CALIC)",
    "Least Squares",
}


def main():
    assert os.path.exists(TIF), f"missing fixture {TIF}"

    import xtm

    assert xtm.version(), "version() returned empty string"
    assert xtm.__version__ == xtm.version()

    tmp = tempfile.mkdtemp(prefix="xtm_py_")
    try:
        xtm_path = os.path.join(tmp, "test.xtm")
        out_full = os.path.join(tmp, "out_full.tif")
        out_roi = os.path.join(tmp, "out_roi.tif")

        # ---- encode ----
        er = xtm.encode(TIF, xtm_path)
        assert er.width == 512 and er.height == 512
        assert er.total_blocks > 0
        assert er.output_bytes > 0
        assert er.time_load_ms > 0 and er.time_total_ms > 0
        assert isinstance(er.predictor_counts, dict)
        assert sum(er.predictor_counts.values()) == er.total_blocks
        assert os.path.exists(xtm_path)

        # ---- info ----
        info = xtm.info(xtm_path)
        assert info.width == 512 and info.height == 512
        assert abs(info.precision - 1.0) < 1e-12
        assert info.pipeline_id == 0
        assert info.block_count == er.total_blocks
        assert info.total_payload_bytes > 0
        assert info.total_payload_bytes <= er.output_bytes
        assert info.transform.origin_x != 0.0 or info.transform.origin_y != 0.0

        # ---- verify (checksum-only) ----
        v = xtm.verify(xtm_path)
        assert v.passed, v.message
        assert v.blocks_checked == info.block_count

        # ---- verify (decode vs source) ----
        v2 = xtm.verify(xtm_path, TIF)
        assert v2.passed, v2.message
        assert v2.pixels_checked == 512 * 512
        assert v2.mismatched_pixels == 0

        # ---- full decode ----
        dr = xtm.decode(xtm_path, out_full)
        assert dr.width == 512 and dr.height == 512
        assert dr.blocks_decoded == info.block_count
        assert os.path.exists(out_full)

        # ---- ROI decode ----
        # (pixel-level ROI == full-decode-crop equality and superblock
        # selectivity are covered by ApiTest.RoiDecodeEqualsFullDecodeCrop;
        # this 512x512 fixture is a single superblock, so both decodes touch
        # the same blocks.)
        dr_roi = xtm.decode(xtm_path, out_roi, region=(16, 16, 256, 256))
        assert dr_roi.width == 256 and dr_roi.height == 256
        assert dr_roi.blocks_decoded > 0
        assert os.path.exists(out_roi)

        # ---- analyze ----
        rep = xtm.analyze(TIF)
        assert rep.width == 512 and rep.height == 512
        assert rep.sample_count == 512 * 512
        assert len(rep.predictors) == 6
        assert {p.name for p in rep.predictors} == KNOWN_PREDICTORS
        assert all(p.selection_bpp >= 0.0 for p in rep.predictors)
        assert rep.total_blocks > 0
        assert rep.chosen_predictor_name in KNOWN_PREDICTORS
        assert 0 <= rep.chosen_predictor_id < 6
        assert rep.budget.total_bpp > 0.0
        assert rep.estimated_file_bytes > 0.0
        assert len(rep.residual_predictor_blocks) == 7

        # ---- kwargs / split precision encode ----
        xtm_cm = os.path.join(tmp, "cm.xtm")
        er2 = xtm.encode(
            TIF,
            xtm_cm,
            precision=0.1,
            pipeline="predictor",
            context="extended",
            disable_quadtree=True,
            num_threads=2,
        )
        assert er2.total_blocks > 0
        v3 = xtm.verify(xtm_cm, TIF)
        assert v3.passed, v3.message
        assert v3.mismatched_pixels == 0

        # ---- error cases ----
        def expect(exception_type, fn, *args, **kwargs):
            try:
                fn(*args, **kwargs)
            except exception_type:
                return
            raise AssertionError(f"expected {exception_type.__name__} from "
                                 f"{getattr(fn, '__name__', fn)}")

        expect(ValueError, xtm.encode, TIF, os.path.join(tmp, "bad_prec.xtm"),
               precision=0.0)
        expect(ValueError, xtm.encode, TIF, os.path.join(tmp, "bad_pipe.xtm"),
               pipeline="bogus")
        expect(ValueError, xtm.encode, TIF, os.path.join(tmp, "bad_ctx.xtm"),
               context="bogus")
        expect(ValueError, xtm.decode, xtm_path, os.path.join(tmp, "x.tif"),
               region=(0, 0, 100, 0))
        expect(RuntimeError, xtm.decode, os.path.join(tmp, "missing.xtm"),
               os.path.join(tmp, "x.tif"))
        expect(RuntimeError, xtm.encode, os.path.join(tmp, "missing.tif"),
               os.path.join(tmp, "x.xtm"))
        expect(RuntimeError, xtm.info, os.path.join(tmp, "missing.xtm"))

        print("ALL PYTHON BINDINGS TESTS PASSED")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())