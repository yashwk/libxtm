#!/usr/bin/env python3
import os
import sys
import subprocess
import re
import argparse
from typing import List, Tuple

def get_dimensions(tif_path: str) -> Tuple[int, int]:
    result = subprocess.run(["gdalinfo", tif_path], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"gdalinfo failed on {tif_path}")
    
    match = re.search(r"Size is (\d+), (\d+)", result.stdout)
    if match:
        return int(match.group(1)), int(match.group(2))
    raise RuntimeError(f"Could not parse size from {tif_path}")

def run_decode_roi(xtm_bin: str, xtm_path: str, size: int, center_x: int, center_y: int) -> float:
    # Calculate region coords
    rx = max(0, center_x - size // 2)
    ry = max(0, center_y - size // 2)
    rw = size
    rh = size
    
    out_tif = "roi_temp_out.tif"
    
    cmd = [xtm_bin, "decode", xtm_path, "-o", out_tif, "--region", str(rx), str(ry), str(rw), str(rh)]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        # Parse output for "Decoded X blocks in Y seconds."
        match = re.search(r"in ([\d\.]+) seconds", result.stdout)
        if match:
            latency = float(match.group(1)) * 1000.0 # to ms
            return latency
        else:
            print("Failed to parse latency from stdout:")
            print(result.stdout)
            return -1.0
    finally:
        if os.path.exists(out_tif):
            os.remove(out_tif)

def main():
    parser = argparse.ArgumentParser(description="Benchmark ROI Decode Latency")
    parser.add_argument("--xtm-bin", default="./build/xtm", help="Path to xtm binary")
    parser.add_argument("--input", required=True, help="Path to input TIF file")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.xtm_bin):
        print(f"Error: xtm binary not found at {args.xtm_bin}")
        sys.exit(1)
        
    width, height = get_dimensions(args.input)
    print(f"Dataset {args.input} is {width}x{height}")
    
    xtm_path = "roi_benchmark_temp.xtm"
    print("Encoding full dataset...")
    subprocess.run([args.xtm_bin, "encode", args.input, "-o", xtm_path], capture_output=True, check=True)
    
    center_x = width // 2
    center_y = height // 2
    
    sizes = [128, 256, 512, 1024, 2048]
    
    print("\nROI Decode Latency Results:")
    print("-" * 50)
    print(f"{'ROI Size':<15} | {'Latency (ms)':<15} | {'Speed (Mpx/s)':<15}")
    print("-" * 50)
    
    for s in sizes:
        if s > width or s > height:
            break
        lat_ms = run_decode_roi(args.xtm_bin, xtm_path, s, center_x, center_y)
        mpx_s = ((s * s) / (lat_ms / 1000.0)) / 1000000.0 if lat_ms > 0 else 0
        print(f"{s}x{s:<11} | {lat_ms:<15.2f} | {mpx_s:<15.2f}")
        
    print("-" * 50)
    
    if os.path.exists(xtm_path):
        os.remove(xtm_path)

if __name__ == "__main__":
    main()
