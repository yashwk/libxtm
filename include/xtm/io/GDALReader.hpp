#pragma once
#include "xtm/Terrain.hpp"
#include <string>

namespace xtm::io {

TerrainBuffer read_gdal(const std::string& path);

}
