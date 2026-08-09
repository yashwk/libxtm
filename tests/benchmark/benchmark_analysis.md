# Comprehensive Benchmark Analysis

This report benchmarks libxtm against GDAL COG DEFLATE and ZSTD on fully quantized terrain data (Scale 0.01).

## Per-File Results

| Category | Tile | DEFLATE | ZSTD | **XTM** | Margin vs ZSTD | Wavelet % | Gradient % | Other Preds % | Encode Time |
|---|---|---|---|---|---|---|---|---|---|
| canyons | colca_canyon.tif | 16.15 | 16.55 | **9.53** | -42.4% | 90.6% | 0.0% | 0.0% | 12.52s |
| canyons | copper_canyon.tif | 16.14 | 16.36 | **9.64** | -41.1% | 89.5% | 0.0% | 0.0% | 12.25s |
| canyons | grand_canyon.tif | 15.25 | 15.41 | **9.37** | -39.2% | 60.4% | 0.0% | 0.0% | 12.54s |
| canyons | leaping_gorge_canyon.tif | 16.12 | 16.33 | **9.79** | -40.1% | 77.6% | 0.0% | 0.0% | 12.96s |
| coasts | big_sur_coast.tif | 14.04 | 14.05 | **8.38** | -40.3% | 47.2% | 0.0% | 0.0% | 8.72s |
| coasts | lofoten_islands_coast.tif | 2.30 | 2.32 | **1.25** | -46.2% | 16.1% | 0.0% | 0.0% | 1.05s |
| coasts | milford_sound_coast.tif | 4.96 | 4.90 | **2.74** | -44.1% | 64.7% | 0.0% | 0.0% | 2.60s |
| coasts | musandam_coast.tif | 6.34 | 6.48 | **3.69** | -43.1% | 68.1% | 0.0% | 0.0% | 3.16s |
| deserts | atacama_desert.tif | 14.34 | 14.68 | **8.36** | -43.1% | 75.8% | 0.0% | 0.0% | 11.35s |
| deserts | kutch_desert.tif | 11.75 | 11.47 | **7.42** | -35.3% | 6.9% | 0.0% | 0.0% | 7.26s |
| deserts | sahara_desert.tif | 14.12 | 14.04 | **8.79** | -37.4% | 35.1% | 0.0% | 0.0% | 9.97s |
| deserts | thar_desert.tif | 13.67 | 13.40 | **8.63** | -35.6% | 29.9% | 0.0% | 0.0% | 11.53s |
| glaciers | 79n_glacier.tif | 4.16 | 4.18 | **2.37** | -43.2% | 10.2% | 0.0% | 0.0% | 0.96s |
| glaciers | baltoro_glacier.tif | 17.75 | 18.43 | **10.68** | -42.1% | 90.6% | 0.0% | 0.0% | 13.68s |
| glaciers | malaspina_glacier.tif | 16.84 | 17.80 | **9.90** | -44.4% | 80.8% | 0.0% | 0.0% | 2.96s |
| glaciers | moreno_glacier.tif | 12.35 | 12.57 | **7.39** | -41.2% | 69.4% | 0.0% | 0.0% | 3.84s |
| hills | chin_hills.tif | 17.72 | 18.10 | **10.79** | -40.4% | 93.4% | 0.0% | 0.0% | 7.03s |
| hills | loess_plateau_hills.tif | 15.58 | 15.68 | **9.64** | -38.5% | 58.5% | 0.0% | 0.0% | 6.62s |
| hills | palouse_hills.tif | 14.61 | 14.91 | **8.90** | -40.3% | 64.7% | 0.0% | 0.0% | 5.71s |
| hills | tuscany_hills.tif | 15.47 | 15.63 | **9.64** | -38.3% | 75.1% | 0.0% | 0.0% | 5.99s |
| mountains | aconcagua_mountain.tif | 16.67 | 17.33 | **10.00** | -42.3% | 88.3% | 0.0% | 0.0% | 8.50s |
| mountains | blanc_mountain.tif | 16.76 | 17.36 | **10.25** | -41.0% | 68.6% | 0.0% | 0.0% | 8.85s |
| mountains | everest_mountain.tif | 17.79 | 18.34 | **10.83** | -40.9% | 87.8% | 0.0% | 0.0% | 12.41s |
| mountains | kenya_mountain.tif | 15.06 | 15.12 | **9.28** | -38.7% | 37.9% | 0.0% | 0.0% | 8.20s |
| plains | lower_gangetic_plains.tif | 12.14 | 11.91 | **7.86** | -34.0% | 0.0% | 0.0% | 0.0% | 6.51s |
| plains | middle_gangetic_plains.tif | 12.04 | 11.78 | **7.75** | -34.2% | 0.3% | 0.0% | 0.0% | 7.68s |
| plains | upper_gangetic_plains.tif | 12.79 | 12.53 | **8.31** | -33.7% | 1.0% | 0.0% | 0.0% | 8.34s |
| plains | western_plains.tif | 12.47 | 12.13 | **8.02** | -33.9% | 0.9% | 0.0% | 0.0% | 7.69s |
| urban | dubai_city.tif | 6.48 | 6.08 | **3.91** | -35.7% | 2.9% | 0.0% | 0.0% | 5.96s |
| urban | hong_kong_city.tif | 7.99 | 8.01 | **4.88** | -39.1% | 35.5% | 0.0% | 0.0% | 7.51s |
| urban | new_york_city.tif | 4.45 | 4.32 | **2.80** | -35.3% | 0.2% | 0.0% | 0.0% | 2.78s |
| urban | tokyo_city.tif | 11.61 | 11.56 | **7.19** | -37.8% | 33.4% | 0.0% | 0.0% | 9.81s |
| valleys | nubra_valley.tif | 17.28 | 18.01 | **10.38** | -42.4% | 91.4% | 0.0% | 0.0% | 8.37s |
| valleys | rhone_valley.tif | 16.28 | 16.76 | **10.10** | -39.7% | 61.3% | 0.0% | 0.0% | 6.82s |
| valleys | spiti_valley.tif | 16.48 | 17.20 | **9.71** | -43.6% | 92.4% | 0.0% | 0.0% | 7.44s |
| valleys | wei_river_valley.tif | 12.05 | 11.71 | **7.76** | -33.7% | 0.0% | 0.0% | 0.0% | 6.70s |
| volcanos | cotopaxi_volcano.tif | 17.42 | 17.79 | **10.66** | -40.1% | 68.5% | 0.0% | 0.0% | 9.81s |
| volcanos | fuji_volcano.tif | 16.59 | 16.97 | **10.13** | -40.3% | 78.7% | 0.0% | 0.0% | 9.54s |
| volcanos | mauna_loa_volcano.tif | 0.68 | 0.63 | **0.33** | -48.0% | 0.9% | 0.0% | 0.0% | 1.58s |
| volcanos | salado_volcano.tif | 14.94 | 15.47 | **8.57** | -44.6% | 75.0% | 0.0% | 0.0% | 8.22s |

## Summary by Category

| Category | Tiles | Avg DEFLATE | Avg ZSTD | **Avg XTM** | Margin vs ZSTD | Avg Wavelet % | Avg Gradient % | Avg Other Preds % | Avg Encode Time |
|---|---|---|---|---|---|---|---|---|---|
| canyons | 4 | 15.91 | 16.16 | **9.58** | -40.7% | 79.5% | 0.0% | 0.0% | 12.57s |
| coasts | 4 | 6.91 | 6.94 | **4.01** | -42.1% | 49.0% | 0.0% | 0.0% | 3.88s |
| deserts | 4 | 13.47 | 13.40 | **8.30** | -38.0% | 36.9% | 0.0% | 0.0% | 10.03s |
| glaciers | 4 | 12.77 | 13.25 | **7.59** | -42.7% | 62.8% | 0.0% | 0.0% | 5.36s |
| hills | 4 | 15.84 | 16.08 | **9.74** | -39.4% | 72.9% | 0.0% | 0.0% | 6.34s |
| mountains | 4 | 16.57 | 17.04 | **10.09** | -40.8% | 70.7% | 0.0% | 0.0% | 9.49s |
| plains | 4 | 12.36 | 12.09 | **7.98** | -34.0% | 0.6% | 0.0% | 0.0% | 7.55s |
| urban | 4 | 7.63 | 7.49 | **4.69** | -37.4% | 18.0% | 0.0% | 0.0% | 6.52s |
| valleys | 4 | 15.52 | 15.92 | **9.49** | -40.4% | 61.3% | 0.0% | 0.0% | 7.33s |
| volcanos | 4 | 12.41 | 12.72 | **7.42** | -41.6% | 55.8% | 0.0% | 0.0% | 7.29s |
| **GLOBAL AVG** | **40** | **12.94** | **13.11** | **7.89** | **-39.8%** | **50.7%** | **0.0%** | **0.0%** | - |
