#!/usr/bin/env python3
import os
import subprocess
import time
import csv
import re
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed

def process_tile(cat, tile, cat_dir):
    tile_path = os.path.join(cat_dir, tile)
    raw_size = os.path.getsize(tile_path) / (1024 * 1024)
    
    quant_tif = tile_path.replace(".tif", "_quant.tif")
    deflate_tif = tile_path.replace(".tif", "_deflate.tif")
    zstd_tif = tile_path.replace(".tif", "_zstd.tif")
    xtm_file = tile_path.replace(".tif", ".xtm")
    
    # Create quantized baseline (scale 0.01)
    subprocess.run(["gdal_calc.py", "-A", tile_path, f"--outfile={quant_tif}", "--calc=numpy.round(A * 100).astype(numpy.int32)", "--type=Int32", "--quiet", "--overwrite"], check=True)
    
    # DEFLATE
    subprocess.run(["gdal_translate", quant_tif, deflate_tif, "-co", "COMPRESS=DEFLATE", "-co", "PREDICTOR=2", "-q"], check=True)
    size_deflate = os.path.getsize(deflate_tif)
    
    # ZSTD
    subprocess.run(["gdal_translate", quant_tif, zstd_tif, "-co", "COMPRESS=ZSTD", "-co", "PREDICTOR=2", "-q"], check=True)
    size_zstd = os.path.getsize(zstd_tif)
    
    # XTM Encode
    start = time.time()
    res = subprocess.run(["build/xtm", "encode", tile_path, "-o", xtm_file, "--scale", "0.01"], check=True, capture_output=True, text=True)
    time_xtm = time.time() - start
    size_xtm = os.path.getsize(xtm_file)
    
    # XTM Decode
    xtm_decode_out = tile_path.replace(".tif", "_decode.tif")
    start = time.time()
    subprocess.run(["build/xtm", "decode", xtm_file, "-o", xtm_decode_out], check=True, capture_output=True)
    time_decode_xtm = time.time() - start
    try:
        os.remove(xtm_decode_out)
    except FileNotFoundError:
        pass
    
    # Parse XTM diagnostics
    wavelet_pct = 0.0
    pred_pcts = {i: 0.0 for i in range(11)}
    
    wv_match = re.search(r"Wavelet Transform applied to \d+ blocks \(([\d\.]+)\%\)", res.stdout)
    if wv_match:
        wavelet_pct = float(wv_match.group(1))
        
    for p_idx in range(11):
        p_match = re.search(rf"Predictor {p_idx}: \d+ blocks \(([\d\.]+)\%\)", res.stdout)
        if p_match:
            pred_pcts[p_idx] = float(p_match.group(1))
            
    # Calculate samples using gdalinfo (no python dependency)
    info = subprocess.run(["gdalinfo", tile_path], capture_output=True, text=True, check=True).stdout
    size_match = re.search(r"Size is (\d+), (\d+)", info)
    if size_match:
        samples = int(size_match.group(1)) * int(size_match.group(2))
    else:
        samples = 3600 * 3600 # Fallback
    
    bps_deflate = (size_deflate * 8) / samples
    bps_zstd = (size_zstd * 8) / samples
    bps_xtm = (size_xtm * 8) / samples
    
    # Clean up
    try:
        os.remove(quant_tif)
        os.remove(deflate_tif)
        os.remove(zstd_tif)
        os.remove(xtm_file)
    except FileNotFoundError:
        pass
    
    return {
        "cat": cat, "tile": tile, "raw_size": raw_size,
        "size_deflate": size_deflate, "size_zstd": size_zstd, "size_xtm": size_xtm,
        "bps_deflate": bps_deflate, "bps_zstd": bps_zstd, "bps_xtm": bps_xtm,
        "time_xtm": time_xtm, "time_decode_xtm": time_decode_xtm,
        "wavelet_pct": wavelet_pct, "pred_pcts": pred_pcts
    }

def run_benchmark():
    data_dir = "data"
    output_csv = "benchmark_results.csv"
    output_md = "docs/benchmark_analysis.md"
    
    categories = [d for d in os.listdir(data_dir) if os.path.isdir(os.path.join(data_dir, d))]
    
    tasks = []
    for cat in sorted(categories):
        cat_dir = os.path.join(data_dir, cat)
        tiles = sorted([f for f in os.listdir(cat_dir) if f.endswith(".tif") and "quant" not in f and "deflate" not in f and "zstd" not in f])
        for tile in tiles:
            tasks.append((cat, tile, cat_dir))
            
    print(f"Starting parallel benchmark on {len(tasks)} tiles using {os.cpu_count()} workers...")
    
    cat_stats = defaultdict(lambda: {"count": 0, "deflate_bps": 0.0, "zstd_bps": 0.0, "xtm_bps": 0.0, 
                                     "xtm_time": 0.0, "wavelet_pct": 0.0, "pred_grad": 0.0, "pred_other": 0.0})
                                     
    all_results = []
    
    with open(output_csv, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Category", "Tile", "DEFLATE BPS", "ZSTD BPS", "XTM BPS", "Encode Time (s)", "Decode Time (s)", "Wavelet %", "Gradient %", "Other %"])
        
        with ProcessPoolExecutor() as executor:
            futures = [executor.submit(process_tile, c, t, d) for c, t, d in tasks]
            
            for future in as_completed(futures):
                res = future.result()
                print(f"Finished {res['cat']}/{res['tile']} - XTM: {res['bps_xtm']:.2f} bps, Time: {res['time_xtm']:.2f}s")
                
                grad_pct = res['pred_pcts'].get(0, 0.0) # Index 0 is GradientPredictor
                other_pct = sum(res['pred_pcts'].get(i, 0.0) for i in range(1, 11))
                
                res['grad_pct'] = grad_pct
                res['other_pct'] = other_pct
                all_results.append(res)
                
                writer.writerow([
                    res['cat'], res['tile'], 
                    f"{res['bps_deflate']:.2f}", f"{res['bps_zstd']:.2f}", f"{res['bps_xtm']:.2f}",
                    f"{res['time_xtm']:.2f}", f"{res['time_decode_xtm']:.2f}", f"{res['wavelet_pct']:.1f}", f"{grad_pct:.1f}", f"{other_pct:.1f}"
                ])
                f.flush()
                
                cat = res['cat']
                cat_stats[cat]["count"] += 1
                cat_stats[cat]["deflate_bps"] += res['bps_deflate']
                cat_stats[cat]["zstd_bps"] += res['bps_zstd']
                cat_stats[cat]["xtm_bps"] += res['bps_xtm']
                cat_stats[cat]["xtm_time"] += res['time_xtm']
                cat_stats[cat]["xtm_decode_time"] = cat_stats[cat].get("xtm_decode_time", 0.0) + res['time_decode_xtm']
                cat_stats[cat]["wavelet_pct"] += res['wavelet_pct']
                cat_stats[cat]["pred_grad"] += grad_pct
                cat_stats[cat]["pred_other"] += other_pct

    # Sort results for deterministic markdown output
    all_results.sort(key=lambda x: (x['cat'], x['tile']))

    # Write Markdown Report
    with open(output_md, 'w') as md:
        md.write("# Comprehensive Benchmark Analysis\n\n")
        md.write("This report benchmarks libxtm against GDAL COG DEFLATE and ZSTD on fully quantized terrain data (Scale 0.01).\n\n")
        
        md.write("## Per-File Results\n\n")
        md.write("| Category | Tile | DEFLATE | ZSTD | **XTM** | Margin vs ZSTD | Wavelet % | Gradient % | Other Preds % | Encode Time | Decode Time |\n")
        md.write("|---|---|---|---|---|---|---|---|---|---|---|\n")
        
        for res in all_results:
            margin = ((res['bps_zstd'] - res['bps_xtm']) / res['bps_zstd']) * 100.0
            md.write(f"| {res['cat']} | {res['tile']} | {res['bps_deflate']:.2f} | {res['bps_zstd']:.2f} | **{res['bps_xtm']:.2f}** "
                     f"| -{margin:.1f}% | {res['wavelet_pct']:.1f}% | {res['grad_pct']:.1f}% | {res['other_pct']:.1f}% "
                     f"| {res['time_xtm']:.2f}s | {res['time_decode_xtm']:.2f}s |\n")
                     
        md.write("\n## Summary by Category\n\n")
        md.write("| Category | Tiles | Avg DEFLATE | Avg ZSTD | **Avg XTM** | Margin vs ZSTD | Avg Wavelet % | Avg Gradient % | Avg Other Preds % | Avg Encode Time | Avg Decode Time |\n")
        md.write("|---|---|---|---|---|---|---|---|---|---|---|\n")
        
        total = {"count": 0, "def": 0, "zst": 0, "xtm": 0, "wv": 0, "grad": 0, "other": 0, "time_encode": 0, "time_decode": 0}
        
        for cat in sorted(cat_stats.keys()):
            s = cat_stats[cat]
            c = s["count"]
            if c == 0: continue
            
            margin = ((s["zstd_bps"]/c - s["xtm_bps"]/c) / (s["zstd_bps"]/c)) * 100.0
            
            md.write(f"| {cat} | {c} | {s['deflate_bps']/c:.2f} | {s['zstd_bps']/c:.2f} | **{s['xtm_bps']/c:.2f}** "
                     f"| -{margin:.1f}% | {s['wavelet_pct']/c:.1f}% | {s['pred_grad']/c:.1f}% | {s['pred_other']/c:.1f}% "
                     f"| {s['xtm_time']/c:.2f}s | {s['xtm_decode_time']/c:.2f}s |\n")
                     
            total["count"] += c
            total["def"] += s["deflate_bps"]
            total["zst"] += s["zstd_bps"]
            total["xtm"] += s["xtm_bps"]
            total["wv"] += s["wavelet_pct"]
            total["grad"] += s["pred_grad"]
            total["other"] += s["pred_other"]
            total["time_encode"] += s["xtm_time"]
            total["time_decode"] += s["xtm_decode_time"]
            
        if total["count"] > 0:
            c = total["count"]
            margin = ((total["zst"]/c - total["xtm"]/c) / (total["zst"]/c)) * 100.0
            md.write(f"| **GLOBAL AVG** | **{c}** | **{total['def']/c:.2f}** | **{total['zst']/c:.2f}** | **{total['xtm']/c:.2f}** "
                     f"| **-{margin:.1f}%** | **{total['wv']/c:.1f}%** | **{total['grad']/c:.1f}%** | **{total['other']/c:.1f}%** | **{total['time_encode']/c:.2f}s** | **{total['time_decode']/c:.2f}s** |\n")
            
    print(f"Benchmarking complete! Results saved to {output_md}")

if __name__ == "__main__":
    run_benchmark()
