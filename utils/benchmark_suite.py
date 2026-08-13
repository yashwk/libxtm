#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = [
#     "psutil",
# ]
# ///
import os
import subprocess
import time
import csv
import re
import argparse
import platform
import psutil
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor, as_completed

def get_metadata():
    metadata = {
        "os": platform.system() + " " + platform.release(),
        "cpu": platform.processor(),
        "ram_gb": round(psutil.virtual_memory().total / (1024**3), 2),
        "gdal_version": "Unknown"
    }
    
    if platform.system() == "Linux":
        try:
            with open("/proc/cpuinfo", "r") as f:
                for line in f:
                    if "model name" in line:
                        metadata["cpu"] = line.split(":")[1].strip()
                        break
        except Exception:
            pass
            
    try:
        gdal_info = subprocess.run(["gdalinfo", "--version"], capture_output=True, text=True, check=True).stdout
        metadata["gdal_version"] = gdal_info.strip()
    except Exception:
        pass
        
    return metadata

def run_command_with_time(cmd):
    """Run a command and parse peak RSS using /usr/bin/time -v"""
    sys_name = platform.system()
    if sys_name == "Linux":
        time_cmd = ["/usr/bin/time", "-v"]
    elif sys_name == "Darwin":
        time_cmd = ["/usr/bin/time", "-l"]
    else:
        start = time.time()
        res = subprocess.run(cmd, check=True, capture_output=True, text=True)
        return res.stdout, time.time() - start, 0.0

    full_cmd = time_cmd + cmd
    start = time.time()
    res = subprocess.run(full_cmd, capture_output=True, text=True)
    duration = time.time() - start
    
    if res.returncode != 0:
        print(f"Command failed: {' '.join(full_cmd)}\nError: {res.stderr}")
        res.check_returncode()
        
    peak_ram_mb = 0.0
    if sys_name == "Linux":
        match = re.search(r"Maximum resident set size \(kbytes\):\s+(\d+)", res.stderr)
        if match:
            peak_ram_mb = float(match.group(1)) / 1024.0
    elif sys_name == "Darwin":
        match = re.search(r"\s+(\d+)\s+maximum resident set size", res.stderr)
        if match:
            peak_ram_mb = float(match.group(1)) / (1024.0 * 1024.0)
            
    return res.stdout, duration, peak_ram_mb

def process_tile(cat, tile, cat_dir, xtm_bin, keep_temp):
    tile_path = os.path.join(cat_dir, tile)
    raw_size = os.path.getsize(tile_path) / (1024 * 1024)
    
    quant_tif = tile_path.replace(".tif", "_quant.tif")
    deflate_tif = tile_path.replace(".tif", "_deflate.tif")
    zstd_tif = tile_path.replace(".tif", "_zstd.tif")
    lerc_tif = tile_path.replace(".tif", "_lerc.tif")
    xtm_file = tile_path.replace(".tif", ".xtm")
    
    try:
        # Create quantized baseline (scale 0.01)
        subprocess.run(["gdal_calc.py", "-A", tile_path, f"--outfile={quant_tif}", "--calc=numpy.round(A * 100).astype(numpy.int32)", "--type=Int32", "--quiet", "--overwrite"], check=True)
        
        # DEFLATE
        subprocess.run(["gdal_translate", quant_tif, deflate_tif, "-co", "COMPRESS=DEFLATE", "-co", "PREDICTOR=2", "-q"], check=True)
        size_deflate = os.path.getsize(deflate_tif)
        
        # ZSTD
        subprocess.run(["gdal_translate", quant_tif, zstd_tif, "-co", "COMPRESS=ZSTD", "-co", "PREDICTOR=2", "-q"], check=True)
        size_zstd = os.path.getsize(zstd_tif)
        
        # LERC_ZSTD (Max Error 0.0 since we already quantized to int)
        subprocess.run(["gdal_translate", quant_tif, lerc_tif, "-co", "COMPRESS=LERC_ZSTD", "-co", "MAX_Z_ERROR=0.0", "-q"], check=True)
        size_lerc = os.path.getsize(lerc_tif)
        
        # XTM Encode
        stdout, time_xtm, ram_encode = run_command_with_time([xtm_bin, "encode", tile_path, "-o", xtm_file, "--scale", "0.01"])
        size_xtm = os.path.getsize(xtm_file)
        
        # XTM Decode
        xtm_decode_out = tile_path.replace(".tif", "_decode.tif")
        _, time_decode_xtm, ram_decode = run_command_with_time([xtm_bin, "decode", xtm_file, "-o", xtm_decode_out])
        
        if not keep_temp:
            try:
                os.remove(xtm_decode_out)
            except FileNotFoundError:
                pass
        
        # Parse XTM diagnostics
        wavelet_pct = 0.0
        pred_pcts = {i: 0.0 for i in range(7)}
        
        wv_match = re.search(r"Wavelet Transform applied to [\d,]+ blocks \(([\d\.]+)\%\)", stdout)
        if wv_match:
            wavelet_pct = float(wv_match.group(1))
            
        # Predictor Statistics table rows are parsed from the line prefix
        # only: one "  <name> <blocks> <pct>%" row per predictor, sorted by
        # usage. Block counts carry thousands separators.
        pred_names = {0: "Gradient", 1: "Left", 2: "JPEG-LS", 3: "Polynomial",
                      4: "GAP (CALIC)", 5: "Least Squares"}
        for p_idx, p_name in pred_names.items():
            p_match = re.search(
                rf"^  {re.escape(p_name)}\s+[\d,]+\s+([\d\.]+)\%",
                stdout, re.MULTILINE)
            if p_match:
                pred_pcts[p_idx] = float(p_match.group(1))
                
        # Calculate samples using gdalinfo
        info = subprocess.run(["gdalinfo", tile_path], capture_output=True, text=True, check=True).stdout
        size_match = re.search(r"Size is (\d+), (\d+)", info)
        if size_match:
            samples = int(size_match.group(1)) * int(size_match.group(2))
        else:
            samples = 3600 * 3600 # Fallback
        
        bps_deflate = (size_deflate * 8) / samples
        bps_zstd = (size_zstd * 8) / samples
        bps_lerc = (size_lerc * 8) / samples
        bps_xtm = (size_xtm * 8) / samples
        
    finally:
        # Clean up
        if not keep_temp:
            for f in [quant_tif, deflate_tif, zstd_tif, lerc_tif, xtm_file]:
                try:
                    if os.path.exists(f):
                        os.remove(f)
                except Exception:
                    pass
        
    return {
        "cat": cat, "tile": tile, "raw_size": raw_size,
        "size_deflate": size_deflate, "size_zstd": size_zstd, "size_lerc": size_lerc, "size_xtm": size_xtm,
        "bps_deflate": bps_deflate, "bps_zstd": bps_zstd, "bps_lerc": bps_lerc, "bps_xtm": bps_xtm,
        "time_xtm": time_xtm, "time_decode_xtm": time_decode_xtm,
        "ram_encode": ram_encode, "ram_decode": ram_decode,
        "wavelet_pct": wavelet_pct, "pred_pcts": pred_pcts
    }

def run_benchmark():
    parser = argparse.ArgumentParser(description="libxtm Benchmark Suite")
    parser.add_argument("--data-dir", default="data", help="Directory containing tile categories")
    parser.add_argument("--xtm-bin", default="build/Release/bin/xtm", help="Path to xtm binary")
    parser.add_argument("--out-dir", default="tests/benchmark", help="Output directory for reports")
    parser.add_argument("--keep-temp", action="store_true", help="Keep temporary TIFF and XTM files")
    args = parser.parse_args()

    data_dir = args.data_dir
    output_csv = os.path.join(args.out_dir, "benchmark_results.csv")
    output_md = os.path.join(args.out_dir, "benchmark_analysis.md")
    
    os.makedirs(args.out_dir, exist_ok=True)
    
    if not os.path.exists(data_dir):
        print(f"Error: Data directory '{data_dir}' not found.")
        return
        
    categories = [d for d in os.listdir(data_dir) if os.path.isdir(os.path.join(data_dir, d))]
    
    tasks = []
    for cat in sorted(categories):
        cat_dir = os.path.join(data_dir, cat)
        tiles = sorted([f for f in os.listdir(cat_dir) if f.endswith(".tif") and "quant" not in f and "deflate" not in f and "zstd" not in f and "lerc" not in f and "decode" not in f])
        for tile in tiles:
            tasks.append((cat, tile, cat_dir))
            
    if not tasks:
        print("No TIFF tiles found to benchmark.")
        return
            
    print(f"Starting parallel benchmark on {len(tasks)} tiles using {os.cpu_count()} workers...")
    
    cat_stats = defaultdict(lambda: {"count": 0, "deflate_bps": 0.0, "zstd_bps": 0.0, "lerc_bps": 0.0, "xtm_bps": 0.0, 
                                     "xtm_time": 0.0, "wavelet_pct": 0.0, "pred_grad": 0.0, "pred_other": 0.0,
                                     "xtm_decode_time": 0.0, "ram_encode": 0.0, "ram_decode": 0.0})
                                     
    all_results = []
    
    with open(output_csv, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(["Category", "Tile", "DEFLATE BPS", "ZSTD BPS", "LERC BPS", "XTM BPS", "Encode Time (s)", "Decode Time (s)", "Peak RAM Encode (MB)", "Peak RAM Decode (MB)", "Wavelet %", "Gradient %", "Other %"])
        
        with ProcessPoolExecutor() as executor:
            futures = [executor.submit(process_tile, c, t, d, args.xtm_bin, args.keep_temp) for c, t, d in tasks]
            
            for future in as_completed(futures):
                res = future.result()
                print(f"Finished {res['cat']}/{res['tile']} - XTM: {res['bps_xtm']:.2f} bps, Time: {res['time_xtm']:.2f}s, RAM: {res['ram_encode']:.1f} MB")
                
                grad_pct = res['pred_pcts'].get(0, 0.0) # Index 0 is GradientPredictor
                other_pct = sum(res['pred_pcts'].get(i, 0.0) for i in range(1, 7))
                
                res['grad_pct'] = grad_pct
                res['other_pct'] = other_pct
                all_results.append(res)
                
                writer.writerow([
                    res['cat'], res['tile'], 
                    f"{res['bps_deflate']:.2f}", f"{res['bps_zstd']:.2f}", f"{res['bps_lerc']:.2f}", f"{res['bps_xtm']:.2f}",
                    f"{res['time_xtm']:.2f}", f"{res['time_decode_xtm']:.2f}", f"{res['ram_encode']:.1f}", f"{res['ram_decode']:.1f}", 
                    f"{res['wavelet_pct']:.1f}", f"{grad_pct:.1f}", f"{other_pct:.1f}"
                ])
                f.flush()
                
                cat = res['cat']
                cat_stats[cat]["count"] += 1
                cat_stats[cat]["deflate_bps"] += res['bps_deflate']
                cat_stats[cat]["zstd_bps"] += res['bps_zstd']
                cat_stats[cat]["lerc_bps"] += res['bps_lerc']
                cat_stats[cat]["xtm_bps"] += res['bps_xtm']
                cat_stats[cat]["xtm_time"] += res['time_xtm']
                cat_stats[cat]["xtm_decode_time"] += res['time_decode_xtm']
                cat_stats[cat]["ram_encode"] += res['ram_encode']
                cat_stats[cat]["ram_decode"] += res['ram_decode']
                cat_stats[cat]["wavelet_pct"] += res['wavelet_pct']
                cat_stats[cat]["pred_grad"] += grad_pct
                cat_stats[cat]["pred_other"] += other_pct

    # Sort results for deterministic markdown output
    all_results.sort(key=lambda x: (x['cat'], x['tile']))
    
    metadata = get_metadata()

    # Write Markdown Report
    with open(output_md, 'w') as md:
        md.write("# Comprehensive Benchmark Analysis\n\n")
        md.write("This report benchmarks libxtm against GDAL COG DEFLATE, ZSTD, and LERC on fully quantized terrain data (Scale 0.01).\n\n")
        
        md.write("## System Environment\n")
        md.write(f"- **OS**: `{metadata['os']}`\n")
        md.write(f"- **CPU**: `{metadata['cpu']}`\n")
        md.write(f"- **RAM**: `{metadata['ram_gb']} GB`\n")
        md.write(f"- **GDAL**: `{metadata['gdal_version']}`\n\n")
        
        md.write("## Per-File Results\n\n")
        md.write("| Category | Tile | DEFLATE | ZSTD | LERC | **XTM** | Margin vs LERC | Wavelet % | Gradient % | Other Preds % | Encode (s) | Decode (s) | RAM (MB) |\n")
        md.write("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        
        for res in all_results:
            margin = ((res['bps_lerc'] - res['bps_xtm']) / res['bps_lerc']) * 100.0 if res['bps_lerc'] else 0
            md.write(f"| {res['cat']} | {res['tile']} | {res['bps_deflate']:.2f} | {res['bps_zstd']:.2f} | {res['bps_lerc']:.2f} | **{res['bps_xtm']:.2f}** "
                     f"| -{margin:.1f}% | {res['wavelet_pct']:.1f}% | {res['grad_pct']:.1f}% | {res['other_pct']:.1f}% "
                     f"| {res['time_xtm']:.2f} | {res['time_decode_xtm']:.2f} | {max(res['ram_encode'], res['ram_decode']):.0f} |\n")
                     
        md.write("\n## Summary by Category\n\n")
        md.write("| Category | Tiles | DEFLATE | ZSTD | LERC | **XTM** | Margin vs LERC | Wavelet % | Gradient % | Other Preds % | Encode (s) | Decode (s) | RAM (MB) |\n")
        md.write("|---|---|---|---|---|---|---|---|---|---|---|---|---|\n")
        
        total = {"count": 0, "def": 0, "zst": 0, "lerc": 0, "xtm": 0, "wv": 0, "grad": 0, "other": 0, "time_e": 0, "time_d": 0, "ram": 0}
        
        for cat in sorted(cat_stats.keys()):
            s = cat_stats[cat]
            c = s["count"]
            if c == 0: continue
            
            margin = ((s["lerc_bps"]/c - s["xtm_bps"]/c) / (s["lerc_bps"]/c)) * 100.0 if s["lerc_bps"] else 0
            
            md.write(f"| {cat} | {c} | {s['deflate_bps']/c:.2f} | {s['zstd_bps']/c:.2f} | {s['lerc_bps']/c:.2f} | **{s['xtm_bps']/c:.2f}** "
                     f"| -{margin:.1f}% | {s['wavelet_pct']/c:.1f}% | {s['pred_grad']/c:.1f}% | {s['pred_other']/c:.1f}% "
                     f"| {s['xtm_time']/c:.2f} | {s['xtm_decode_time']/c:.2f} | {max(s['ram_encode']/c, s['ram_decode']/c):.0f} |\n")
                     
            total["count"] += c
            total["def"] += s["deflate_bps"]
            total["zst"] += s["zstd_bps"]
            total["lerc"] += s["lerc_bps"]
            total["xtm"] += s["xtm_bps"]
            total["wv"] += s["wavelet_pct"]
            total["grad"] += s["pred_grad"]
            total["other"] += s["pred_other"]
            total["time_e"] += s["xtm_time"]
            total["time_d"] += s["xtm_decode_time"]
            total["ram"] += max(s["ram_encode"], s["ram_decode"])
            
        if total["count"] > 0:
            c = total["count"]
            margin = ((total["lerc"]/c - total["xtm"]/c) / (total["lerc"]/c)) * 100.0 if total["lerc"] else 0
            md.write(f"| **GLOBAL AVG** | **{c}** | **{total['def']/c:.2f}** | **{total['zst']/c:.2f}** | **{total['lerc']/c:.2f}** | **{total['xtm']/c:.2f}** "
                     f"| **-{margin:.1f}%** | **{total['wv']/c:.1f}%** | **{total['grad']/c:.1f}%** | **{total['other']/c:.1f}%** | **{total['time_e']/c:.2f}** | **{total['time_d']/c:.2f}** | **{total['ram']/c:.0f}** |\n")
            
    print(f"Benchmarking complete! Results saved to {output_md}")

if __name__ == "__main__":
    run_benchmark()
