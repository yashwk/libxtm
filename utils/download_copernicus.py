#!/usr/bin/env python3
import argparse
import subprocess
import sys
import os

def format_coordinate(value, prefix_pos, prefix_neg, digits):
    """
    Format the coordinate into Copernicus DEM naming convention.
    """
    prefix = prefix_pos if value >= 0 else prefix_neg
    return f"{prefix}{abs(int(value)):0{digits}d}_00"

def download_tile(lat, lon, output_dir):
    """
    Constructs the S3 URI and downloads the tile using aws cli.
    """
    lat_str = format_coordinate(lat, 'N', 'S', 2)
    lon_str = format_coordinate(lon, 'E', 'W', 3)
    
    # Copernicus DEM GLO-30 folder and filename convention
    # e.g., Copernicus_DSM_COG_10_N27_00_E086_00_DEM
    base_name = f"Copernicus_DSM_COG_10_{lat_str}_{lon_str}_DEM"
    s3_uri = f"s3://copernicus-dem-30m/{base_name}/{base_name}.tif"
    
    # Ensure output directory exists
    os.makedirs(output_dir, exist_ok=True)
    
    output_path = os.path.join(output_dir, f"{base_name}.tif")
    
    print(f"Downloading from: {s3_uri}")
    print(f"Output to: {output_path}")
    
    try:
        # Use --no-sign-request since Copernicus DEM is a public dataset on AWS
        cmd = [
            "aws", "s3", "cp", 
            s3_uri, 
            output_path, 
            "--no-sign-request"
        ]
        subprocess.run(cmd, check=True)
        print("Download complete!")
    except subprocess.CalledProcessError as e:
        print(f"Error downloading tile: {e}", file=sys.stderr)
        print("Are you sure this tile exists? (Ocean tiles usually don't exist)", file=sys.stderr)
        sys.exit(1)
    except FileNotFoundError:
        print("Error: 'aws' CLI tool is not installed or not in PATH.", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Download Copernicus DEM GLO-30 tiles via AWS S3")
    
    lat_group = parser.add_mutually_exclusive_group(required=True)
    lat_group.add_argument("--northing", "-n", type=float, help="Latitude (Northing) e.g., 27 for N27")
    lat_group.add_argument("--southing", "-s", type=float, help="Latitude (Southing) e.g., 27 for S27")
    
    lon_group = parser.add_mutually_exclusive_group(required=True)
    lon_group.add_argument("--easting", "-e", type=float, help="Longitude (Easting) e.g., 86 for E086")
    lon_group.add_argument("--westing", "-w", type=float, help="Longitude (Westing) e.g., 86 for W086")
    
    parser.add_argument("--output", "-o", type=str, default="data", help="Output directory (default: data/)")
    
    args = parser.parse_args()
    
    lat = args.northing if args.northing is not None else -args.southing
    lon = args.easting if args.easting is not None else -args.westing
    
    download_tile(lat, lon, args.output)
